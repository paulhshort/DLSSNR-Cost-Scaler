// Cost Scaler D3D11 WARP regression harness.
//
// Intended execution target: disposable GitHub Windows runner. This test does not
// load a game, NVIDIA NGX model, ReShade addon, proxy DLL, or supplied native
// artifact. It executes checked-in Cost Scaler compute shader bytecode through
// Microsoft's WARP software rasterizer.
//
// Compile from src/Cost-Scaler-Lab after a Visual Studio dev shell is active:
//   cl /nologo /std:c++20 /EHsc /O2 /W4 tests\cost_scaler_shader_warp.cpp /I. /link d3d11.lib d3dcompiler.lib dxgi.lib dxguid.lib

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace legacy_downsample {
#define g_DownsampleShader g_LegacyDownsampleShader
#include "fixtures/legacy_downsample_shader.h"
#undef g_DownsampleShader
}

namespace current_downsample {
#include "../Downsample_Shader.h"
}

namespace current_resolve {
#include "../Resolve_Shader.h"
}

namespace {

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) { reset(); ptr_ = other.ptr_; other.ptr_ = nullptr; }
        return *this;
    }
    T* get() const { return ptr_; }
    T** put() { reset(); return &ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    void reset() { if (ptr_) { ptr_->Release(); ptr_ = nullptr; } }
private:
    T* ptr_ = nullptr;
};

[[noreturn]] void fail(const char* msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    std::exit(1);
}

void check(bool ok, const char* msg) { if (!ok) fail(msg); }
void check_hr(HRESULT hr, const char* msg) {
    if (FAILED(hr)) {
        std::fprintf(stderr, "FAIL: %s hr=0x%08X\n", msg, static_cast<unsigned>(hr));
        std::exit(1);
    }
}

struct Float4 { float r, g, b, a; };

struct DownConstants {
    uint32_t gSrcWidth;
    uint32_t gSrcHeight;
    uint32_t gDstWidth;
    uint32_t gDstHeight;
    uint32_t gDownsampleFilter;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};
static_assert(sizeof(DownConstants) == 32, "D3D11 constant buffers must be 16-byte aligned");

struct ResolveConstants {
    uint32_t gNativeWidth;
    uint32_t gNativeHeight;
    uint32_t gWorkWidth;
    uint32_t gWorkHeight;
    float    gTransferStrength;
    float    gSharpness;
    uint32_t gEnlargementMode;
    float    gColorStrength;
    uint32_t gIsSkipFrame;
    uint32_t gHasDepth;
    uint32_t _pad0;
    uint32_t _pad1;
};
static_assert(sizeof(ResolveConstants) == 48, "D3D11 constant buffers must be 16-byte aligned");

struct ShaderBlob { const char* name; const unsigned char* bytes; size_t size; };

struct TextureSet {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
};

float rel_error(float a, float b) {
    return std::fabs(a - b) / std::max(1.0f, std::max(std::fabs(a), std::fabs(b)));
}

void require_finite(const std::vector<Float4>& values, const char* label) {
    for (size_t i = 0; i < values.size(); ++i) {
        const Float4& v = values[i];
        if (!std::isfinite(v.r) || !std::isfinite(v.g) || !std::isfinite(v.b) || !std::isfinite(v.a)) {
            std::fprintf(stderr, "FAIL: %s pixel=%zu nonfinite (%g,%g,%g,%g)\n", label, i, v.r, v.g, v.b, v.a);
            std::exit(1);
        }
    }
}

void require_close(const std::vector<Float4>& got, const std::vector<Float4>& expected, float tol, const char* label) {
    check(got.size() == expected.size(), "size mismatch");
    for (size_t i = 0; i < got.size(); ++i) {
        const float g[4] = { got[i].r, got[i].g, got[i].b, got[i].a };
        const float e[4] = { expected[i].r, expected[i].g, expected[i].b, expected[i].a };
        for (int c = 0; c != 4; ++c) {
            if (rel_error(g[c], e[c]) > tol) {
                std::fprintf(stderr, "FAIL: %s pixel=%zu channel=%d got=%g expected=%g rel=%g tol=%g\n",
                    label, i, c, g[c], e[c], rel_error(g[c], e[c]), tol);
                std::exit(1);
            }
        }
    }
}

void require_close_under_sampler_precision(const std::vector<Float4>& got, const std::vector<Float4>& expected,
                                           const std::vector<Float4>& source, uint32_t sw, uint32_t sh,
                                           const char* label) {
    check(got.size() == expected.size(), "size mismatch");

    float max_dx[4] = {};
    float max_dy[4] = {};
    for (uint32_t y = 0; y < sh; ++y) {
        for (uint32_t x = 1; x < sw; ++x) {
            const Float4 a = source[static_cast<size_t>(y) * sw + x - 1];
            const Float4 b = source[static_cast<size_t>(y) * sw + x];
            const float da[4] = { std::fabs(a.r - b.r), std::fabs(a.g - b.g), std::fabs(a.b - b.b), std::fabs(a.a - b.a) };
            for (int c = 0; c != 4; ++c) max_dx[c] = std::max(max_dx[c], da[c]);
        }
    }
    for (uint32_t y = 1; y < sh; ++y) {
        for (uint32_t x = 0; x < sw; ++x) {
            const Float4 a = source[static_cast<size_t>(y - 1) * sw + x];
            const Float4 b = source[static_cast<size_t>(y) * sw + x];
            const float da[4] = { std::fabs(a.r - b.r), std::fabs(a.g - b.g), std::fabs(a.b - b.b), std::fabs(a.a - b.a) };
            for (int c = 0; c != 4; ++c) max_dy[c] = std::max(max_dy[c], da[c]);
        }
    }

    // D3D11.3 Functional Specification 7.18.16.1 requires at least 8 fractional subtexel bits:
    // https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm
    // Compare against ideal float weights with a conservative per-image Lipschitz bound of one
    // 1/256 subtexel quantum per axis, plus an FP32 margin for these bounded test inputs.
    // Averaging two/four bilinear samples cannot increase this bound. GPU-to-GPU parity stays strict.
    constexpr float kSubtexelQuantum = 1.0f / 256.0f;
    constexpr float kFpMargin = 1.0e-5f;
    for (size_t i = 0; i < got.size(); ++i) {
        const float g[4] = { got[i].r, got[i].g, got[i].b, got[i].a };
        const float e[4] = { expected[i].r, expected[i].g, expected[i].b, expected[i].a };
        for (int c = 0; c != 4; ++c) {
            const float bound = kSubtexelQuantum * (max_dx[c] + max_dy[c]) + kFpMargin;
            const float diff = std::fabs(g[c] - e[c]);
            if (diff > bound) {
                std::fprintf(stderr, "FAIL: %s pixel=%zu channel=%d got=%g expected=%g abs=%g bound=%g dx=%g dy=%g\n",
                    label, i, c, g[c], e[c], diff, bound, max_dx[c], max_dy[c]);
                std::exit(1);
            }
        }
    }
}

Float4 px(uint32_t x, uint32_t y, uint32_t w) {
    const float xf = static_cast<float>(x);
    const float yf = static_cast<float>(y);
    return Float4{
        xf / std::max(1.0f, static_cast<float>(w - 1)),
        yf * 0.25f + xf * 0.03125f,
        ((x ^ y) & 1u) ? 1.0f : 0.0f,
        0.125f + 0.03125f * static_cast<float>((x + y) & 7u)
    };
}

std::vector<Float4> make_frame(uint32_t w, uint32_t h) {
    std::vector<Float4> out(w * h);
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) out[y * w + x] = px(x, y, w);
    return out;
}

std::vector<Float4> make_constant(uint32_t w, uint32_t h, Float4 v) {
    return std::vector<Float4>(static_cast<size_t>(w) * h, v);
}

std::vector<Float4> make_vertical_stripes(uint32_t w, uint32_t h) {
    std::vector<Float4> out(w * h);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const float v = (x & 1u) ? 1.0f : 0.0f;
            out[y * w + x] = Float4{v, v, v, 1.0f};
        }
    }
    return out;
}

float luma(const Float4& v) { return 0.2126f * v.r + 0.7152f * v.g + 0.0722f * v.b; }
float alias_energy_x(const std::vector<Float4>& img, uint32_t w, uint32_t h) {
    float e = 0.0f;
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 1; x < w; ++x) e += std::fabs(luma(img[y * w + x]) - luma(img[y * w + x - 1]));
    return e;
}

Float4 add(Float4 a, Float4 b) {
    return Float4{a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a};
}

Float4 mul(Float4 a, float s) {
    return Float4{a.r * s, a.g * s, a.b * s, a.a * s};
}

Float4 lerp(Float4 a, Float4 b, float t) {
    return add(mul(a, 1.0f - t), mul(b, t));
}

Float4 cpu_bilinear_clamp(const std::vector<Float4>& source, uint32_t sw, uint32_t sh, float u, float v) {
    u = std::min(1.0f, std::max(0.0f, u));
    v = std::min(1.0f, std::max(0.0f, v));

    const float sx = u * static_cast<float>(sw) - 0.5f;
    const float sy = v * static_cast<float>(sh) - 0.5f;
    const float fx0 = std::floor(sx);
    const float fy0 = std::floor(sy);
    const int rawX0 = static_cast<int>(fx0);
    const int rawY0 = static_cast<int>(fy0);
    const int x0 = std::max(0, std::min(static_cast<int>(sw) - 1, rawX0));
    const int y0 = std::max(0, std::min(static_cast<int>(sh) - 1, rawY0));
    const int x1 = std::max(0, std::min(static_cast<int>(sw) - 1, rawX0 + 1));
    const int y1 = std::max(0, std::min(static_cast<int>(sh) - 1, rawY0 + 1));
    const float tx = sx - fx0;
    const float ty = sy - fy0;

    const Float4 c00 = source[static_cast<size_t>(y0) * sw + x0];
    const Float4 c10 = source[static_cast<size_t>(y0) * sw + x1];
    const Float4 c01 = source[static_cast<size_t>(y1) * sw + x0];
    const Float4 c11 = source[static_cast<size_t>(y1) * sw + x1];
    return lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
}

std::vector<Float4> cpu_filter1_reference(const std::vector<Float4>& source, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh) {
    std::vector<Float4> out(static_cast<size_t>(dw) * dh);
    for (uint32_t y = 0; y < dh; ++y) {
        for (uint32_t x = 0; x < dw; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(dw);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(dh);
            if (sw <= dw && sh <= dh) {
                out[static_cast<size_t>(y) * dw + x] = cpu_bilinear_clamp(source, sw, sh, u, v);
                continue;
            }

            const float ox = sw > dw ? 0.25f / static_cast<float>(dw) : 0.0f;
            const float oy = sh > dh ? 0.25f / static_cast<float>(dh) : 0.0f;
            if (ox == 0.0f) {
                out[static_cast<size_t>(y) * dw + x] = mul(add(
                    cpu_bilinear_clamp(source, sw, sh, u, v - oy),
                    cpu_bilinear_clamp(source, sw, sh, u, v + oy)), 0.5f);
            } else if (oy == 0.0f) {
                out[static_cast<size_t>(y) * dw + x] = mul(add(
                    cpu_bilinear_clamp(source, sw, sh, u - ox, v),
                    cpu_bilinear_clamp(source, sw, sh, u + ox, v)), 0.5f);
            } else {
                Float4 sum{};
                sum = add(sum, cpu_bilinear_clamp(source, sw, sh, u - ox, v - oy));
                sum = add(sum, cpu_bilinear_clamp(source, sw, sh, u + ox, v - oy));
                sum = add(sum, cpu_bilinear_clamp(source, sw, sh, u - ox, v + oy));
                sum = add(sum, cpu_bilinear_clamp(source, sw, sh, u + ox, v + oy));
                out[static_cast<size_t>(y) * dw + x] = mul(sum, 0.25f);
            }
        }
    }
    return out;
}

class Harness {
public:
    Harness() {
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL actual{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
            D3D11_SDK_VERSION, device_.put(), &actual, context_.put());
        check_hr(hr, "D3D11CreateDevice(WARP)");
        check(actual >= D3D_FEATURE_LEVEL_11_0, "WARP feature level must support compute shaders");
        create_sampler();
    }

    void validate_downsample_reflection(const ShaderBlob& shader, bool expect_filter) {
        ComPtr<ID3D11ShaderReflection> refl;
        check_hr(D3DReflect(shader.bytes, shader.size, IID_ID3D11ShaderReflection, reinterpret_cast<void**>(refl.put())), shader.name);
        ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByName("DownConstants");
        assert_var(cb, "gSrcWidth", 0);
        assert_var(cb, "gSrcHeight", 4);
        assert_var(cb, "gDstWidth", 8);
        assert_var(cb, "gDstHeight", 12);
        if (expect_filter) assert_var(cb, "gDownsampleFilter", 16);
        assert_bind(refl.get(), "gDownSource", D3D_SIT_TEXTURE, 0);
        assert_bind(refl.get(), "gDownTarget", D3D_SIT_UAV_RWTYPED, 0);
        assert_bind(refl.get(), "gLinearClamp", D3D_SIT_SAMPLER, 0);
    }

    void validate_resolve_reflection(const ShaderBlob& shader) {
        ComPtr<ID3D11ShaderReflection> refl;
        check_hr(D3DReflect(shader.bytes, shader.size, IID_ID3D11ShaderReflection, reinterpret_cast<void**>(refl.put())), shader.name);
        ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByName("ResolveConstants");
        assert_var(cb, "gNativeWidth", 0);
        assert_var(cb, "gNativeHeight", 4);
        assert_var(cb, "gWorkWidth", 8);
        assert_var(cb, "gWorkHeight", 12);
        assert_var(cb, "gTransferStrength", 16);
        assert_var(cb, "gSharpness", 20);
        assert_var(cb, "gEnlargementMode", 24);
        assert_var(cb, "gColorStrength", 28);
        assert_var(cb, "gIsSkipFrame", 32);
        assert_var(cb, "gHasDepth", 36);
        assert_bind(refl.get(), "gSmallInput", D3D_SIT_TEXTURE, 0);
        assert_bind(refl.get(), "gSmallOutput", D3D_SIT_TEXTURE, 1);
        assert_bind(refl.get(), "gNativeColor", D3D_SIT_TEXTURE, 2);
        assert_bind(refl.get(), "gDepth", D3D_SIT_TEXTURE, 3);
        assert_bind(refl.get(), "gResolveTarget", D3D_SIT_UAV_RWTYPED, 0);
        assert_bind(refl.get(), "gLinear", D3D_SIT_SAMPLER, 0);
    }

    std::vector<Float4> downsample(const ShaderBlob& shader, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh,
                                   uint32_t filter, const std::vector<Float4>& source) {
        check(source.size() == static_cast<size_t>(sw) * sh, "downsample source size mismatch");
        TextureSet src = create_float4_texture(sw, sh, source, true, false);
        TextureSet dst = create_float4_texture(dw, dh, {}, false, true);
        DownConstants constants{sw, sh, dw, dh, filter, 0, 0, 0};
        ID3D11ShaderResourceView* srvs[1] = { src.srv.get() };
        ID3D11UnorderedAccessView* uavs[1] = { dst.uav.get() };
        dispatch(shader, &constants, sizeof(constants), srvs, 1, uavs, 1, dw, dh);
        return read_float4_texture(dw, dh, dst.tex.get());
    }

    std::vector<Float4> resolve(const ShaderBlob& shader, uint32_t native_w, uint32_t native_h, uint32_t work_w, uint32_t work_h,
                                const std::vector<Float4>& small_in, const std::vector<Float4>& small_out,
                                const std::vector<Float4>& native, const std::vector<float>& depth,
                                const ResolveConstants& constants) {
        TextureSet sin = create_float4_texture(work_w, work_h, small_in, true, false);
        TextureSet sout = create_float4_texture(work_w, work_h, small_out, true, false);
        TextureSet nat = create_float4_texture(native_w, native_h, native, true, false);
        TextureSet dep = create_r32_texture(native_w, native_h, depth, true);
        TextureSet dst = create_float4_texture(native_w, native_h, {}, false, true);
        ID3D11ShaderResourceView* srvs[4] = { sin.srv.get(), sout.srv.get(), nat.srv.get(), dep.srv.get() };
        ID3D11UnorderedAccessView* uavs[1] = { dst.uav.get() };
        ResolveConstants c = constants;
        c.gNativeWidth = native_w; c.gNativeHeight = native_h; c.gWorkWidth = work_w; c.gWorkHeight = work_h;
        dispatch(shader, &c, sizeof(c), srvs, 4, uavs, 1, native_w, native_h);
        return read_float4_texture(native_w, native_h, dst.tex.get());
    }

private:
    void assert_var(ID3D11ShaderReflectionConstantBuffer* cb, const char* name, UINT offset) {
        D3D11_SHADER_VARIABLE_DESC desc{};
        check_hr(cb->GetVariableByName(name)->GetDesc(&desc), name);
        if (desc.StartOffset != offset) {
            std::fprintf(stderr, "FAIL: %s offset=%u expected=%u\n", name, desc.StartOffset, offset);
            std::exit(1);
        }
    }

    void assert_bind(ID3D11ShaderReflection* refl, const char* name, D3D_SHADER_INPUT_TYPE type, UINT bind) {
        D3D11_SHADER_INPUT_BIND_DESC desc{};
        check_hr(refl->GetResourceBindingDescByName(name, &desc), name);
        if (desc.Type != type || desc.BindPoint != bind) {
            std::fprintf(stderr, "FAIL: %s type=%u expected=%u bind=%u expected=%u\n",
                name, static_cast<unsigned>(desc.Type), static_cast<unsigned>(type), desc.BindPoint, bind);
            std::exit(1);
        }
    }

    void create_sampler() {
        D3D11_SAMPLER_DESC desc{};
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        check_hr(device_->CreateSamplerState(&desc, sampler_.put()), "CreateSamplerState");
    }

    TextureSet create_float4_texture(uint32_t w, uint32_t h, const std::vector<Float4>& init, bool srv, bool uav) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = (srv ? D3D11_BIND_SHADER_RESOURCE : 0) | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0);
        D3D11_SUBRESOURCE_DATA sub{};
        D3D11_SUBRESOURCE_DATA* subptr = nullptr;
        if (!init.empty()) { sub.pSysMem = init.data(); sub.SysMemPitch = w * sizeof(Float4); subptr = &sub; }
        TextureSet out;
        check_hr(device_->CreateTexture2D(&desc, subptr, out.tex.put()), "CreateTexture2D(float4)");
        if (srv) check_hr(device_->CreateShaderResourceView(out.tex.get(), nullptr, out.srv.put()), "CreateSRV(float4)");
        if (uav) check_hr(device_->CreateUnorderedAccessView(out.tex.get(), nullptr, out.uav.put()), "CreateUAV(float4)");
        return out;
    }

    TextureSet create_r32_texture(uint32_t w, uint32_t h, const std::vector<float>& init, bool srv) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = srv ? D3D11_BIND_SHADER_RESOURCE : 0;
        D3D11_SUBRESOURCE_DATA sub{};
        D3D11_SUBRESOURCE_DATA* subptr = nullptr;
        if (!init.empty()) { sub.pSysMem = init.data(); sub.SysMemPitch = w * sizeof(float); subptr = &sub; }
        TextureSet out;
        check_hr(device_->CreateTexture2D(&desc, subptr, out.tex.put()), "CreateTexture2D(depth)");
        if (srv) check_hr(device_->CreateShaderResourceView(out.tex.get(), nullptr, out.srv.put()), "CreateSRV(depth)");
        return out;
    }

    void dispatch(const ShaderBlob& shader, const void* constants, size_t constant_size,
                  ID3D11ShaderResourceView** srvs, UINT srv_count, ID3D11UnorderedAccessView** uavs, UINT uav_count,
                  uint32_t width, uint32_t height) {
        ComPtr<ID3D11ComputeShader> cs;
        check_hr(device_->CreateComputeShader(shader.bytes, shader.size, nullptr, cs.put()), shader.name);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = static_cast<UINT>((constant_size + 15u) & ~15u);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> cb;
        check_hr(device_->CreateBuffer(&bd, nullptr, cb.put()), "CreateBuffer(constants)");
        context_->UpdateSubresource(cb.get(), 0, nullptr, constants, 0, 0);
        ID3D11Buffer* cbs[1] = { cb.get() };
        ID3D11SamplerState* samplers[1] = { sampler_.get() };
        context_->CSSetShader(cs.get(), nullptr, 0);
        context_->CSSetShaderResources(0, srv_count, srvs);
        context_->CSSetUnorderedAccessViews(0, uav_count, uavs, nullptr);
        context_->CSSetConstantBuffers(0, 1, cbs);
        context_->CSSetSamplers(0, 1, samplers);
        context_->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        ID3D11ShaderResourceView* null_srvs[8] = {};
        ID3D11UnorderedAccessView* null_uavs[4] = {};
        ID3D11Buffer* null_cbs[1] = {};
        ID3D11SamplerState* null_samplers[1] = {};
        context_->CSSetShader(nullptr, nullptr, 0);
        context_->CSSetShaderResources(0, 8, null_srvs);
        context_->CSSetUnorderedAccessViews(0, 4, null_uavs, nullptr);
        context_->CSSetConstantBuffers(0, 1, null_cbs);
        context_->CSSetSamplers(0, 1, null_samplers);
        context_->Flush();
    }

    std::vector<Float4> read_float4_texture(uint32_t w, uint32_t h, ID3D11Texture2D* src) {
        D3D11_TEXTURE2D_DESC desc{};
        src->GetDesc(&desc);
        desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.Usage = D3D11_USAGE_STAGING; desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        check_hr(device_->CreateTexture2D(&desc, nullptr, staging.put()), "CreateTexture2D(staging)");
        context_->CopyResource(staging.get(), src);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check_hr(context_->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped), "Map(staging)");
        std::vector<Float4> out(static_cast<size_t>(w) * h);
        for (uint32_t y = 0; y < h; ++y) std::memcpy(out.data() + y * w, static_cast<const uint8_t*>(mapped.pData) + mapped.RowPitch * y, w * sizeof(Float4));
        context_->Unmap(staging.get(), 0);
        return out;
    }

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11SamplerState> sampler_;
};

ShaderBlob legacy_down() { return { "legacy Downsample_Shader.h", legacy_downsample::g_LegacyDownsampleShader, sizeof(legacy_downsample::g_LegacyDownsampleShader) }; }
ShaderBlob current_down() { return { "current Downsample_Shader.h", current_downsample::g_DownsampleShader, sizeof(current_downsample::g_DownsampleShader) }; }
ShaderBlob current_res() { return { "current Resolve_Shader.h", current_resolve::g_ResolveShader, sizeof(current_resolve::g_ResolveShader) }; }

void test_downsample_reflection(Harness& h) {
    h.validate_downsample_reflection(legacy_down(), false);
    h.validate_downsample_reflection(current_down(), true);
}

void test_resolve_reflection(Harness& h) { h.validate_resolve_reflection(current_res()); }

void test_filter0_legacy_parity(Harness& h) {
    struct Case { uint32_t sw, sh, dw, dh; const char* label; };
    const Case cases[] = {
        {7, 5, 3, 2, "odd shrink"},
        {9, 5, 4, 5, "x shrink y native"},
        {5, 9, 5, 4, "x native y shrink"},
        {4, 4, 8, 8, "supersample legacy upscale"},
        {5, 3, 8, 2, "mixed asymmetric"},
    };
    for (const Case& c : cases) {
        auto src = make_frame(c.sw, c.sh);
        auto a = h.downsample(legacy_down(), c.sw, c.sh, c.dw, c.dh, 0, src);
        auto b = h.downsample(current_down(), c.sw, c.sh, c.dw, c.dh, 0, src);
        require_finite(b, c.label);
        require_close(b, a, 1.0e-6f, c.label);
    }
}

void test_filter1_constant_and_native_identity(Harness& h) {
    const Float4 c{0.37f, 2.5f, 8.0f, 0.42f};
    for (auto dims : { std::initializer_list<uint32_t>{6, 4, 3, 2}, std::initializer_list<uint32_t>{5, 5, 5, 5}, std::initializer_list<uint32_t>{4, 4, 8, 8} }) {
        auto it = dims.begin();
        const uint32_t sw = *it++, sh = *it++, dw = *it++, dh = *it++;
        auto src = make_constant(sw, sh, c);
        auto out = h.downsample(current_down(), sw, sh, dw, dh, 1, src);
        require_close(out, make_constant(dw, dh, c), 1.0e-6f, "filter1 preserves constants and alpha");
    }

    auto native = make_frame(6, 5);
    auto legacy = h.downsample(legacy_down(), 6, 5, 6, 5, 0, native);
    auto filtered = h.downsample(current_down(), 6, 5, 6, 5, 1, native);
    require_close(filtered, legacy, 1.0e-6f, "filter1 native dimensions must keep legacy sampling");
}

void test_cpu_bilinear_reference_clamps_raw_neighbors() {
    const std::vector<Float4> src = {
        Float4{0.0f, 0.0f, 0.0f, 0.0f},
        Float4{1.0f, 0.0f, 0.0f, 0.0f},
        Float4{0.0f, 1.0f, 0.0f, 0.0f},
        Float4{1.0f, 1.0f, 0.0f, 0.0f},
    };

    // This coordinate produces floor(u * width - 0.5) == -1. D3D clamp addressing clamps the raw
    // neighbor indices independently, so x0 and x1 both become 0. Computing x1 from the already
    // clamped x0 would incorrectly blend texel 1 at the border.
    const Float4 left_edge = cpu_bilinear_clamp(src, 2, 2, 0.10f, 0.50f);
    if (rel_error(left_edge.r, 0.0f) > 1.0e-6f || rel_error(left_edge.g, 0.5f) > 1.0e-6f) {
        fail("CPU bilinear reference must clamp raw border neighbors independently");
    }
}

void test_filter1_matches_cpu_reference(Harness& h) {
    struct Case { uint32_t sw, sh, dw, dh; const char* label; };
    const Case cases[] = {
        {20, 20, 13, 17, "filter1 both shrink 65x85"},
        {17, 13, 14, 8, "filter1 both shrink 85x65"},
        {16, 7, 5, 7, "filter1 x shrink y native"},
        {7, 16, 7, 5, "filter1 x native y shrink"},
        {8, 5, 4, 9, "filter1 x shrink y upscale"},
        {5, 8, 9, 4, "filter1 x upscale y shrink"},
    };
    for (const Case& c : cases) {
        auto src = make_frame(c.sw, c.sh);
        auto gpu = h.downsample(current_down(), c.sw, c.sh, c.dw, c.dh, 1, src);
        auto cpu = cpu_filter1_reference(src, c.sw, c.sh, c.dw, c.dh);
        require_finite(gpu, c.label);
        require_close_under_sampler_precision(gpu, cpu, src, c.sw, c.sh, c.label);
    }
}

void test_filter1_nonconstant_no_shrink_parity(Harness& h) {
    struct Case { uint32_t sw, sh, dw, dh; const char* label; };
    const Case cases[] = {
        {6, 5, 6, 5, "filter1 nonconstant native"},
        {4, 4, 8, 8, "filter1 nonconstant both upscale"},
        {5, 3, 8, 3, "filter1 nonconstant x upscale y native"},
        {3, 5, 3, 8, "filter1 nonconstant x native y upscale"},
    };
    for (const Case& c : cases) {
        auto src = make_frame(c.sw, c.sh);
        auto legacy = h.downsample(current_down(), c.sw, c.sh, c.dw, c.dh, 0, src);
        auto filtered = h.downsample(current_down(), c.sw, c.sh, c.dw, c.dh, 1, src);
        require_close(filtered, legacy, 1.0e-6f, c.label);
    }
}

void test_filter1_reduces_stripe_alias_when_shrinking(Harness& h) {
    auto stripes = make_vertical_stripes(16, 4);
    auto legacy = h.downsample(current_down(), 16, 4, 5, 4, 0, stripes);
    auto filtered = h.downsample(current_down(), 16, 4, 5, 4, 1, stripes);
    require_finite(filtered, "filter1 stripe output");
    const float legacy_energy = alias_energy_x(legacy, 5, 4);
    const float filtered_energy = alias_energy_x(filtered, 5, 4);
    if (!(filtered_energy <= legacy_energy + 1.0e-5f)) {
        std::fprintf(stderr, "FAIL: filter1 should not increase shrink-axis stripe energy legacy=%g filtered=%g\n", legacy_energy, filtered_energy);
        std::exit(1);
    }
}

void test_resolve_identity_and_gain(Harness& h) {
    const uint32_t nw = 4, nh = 4, ww = 4, wh = 4;
    auto native = make_frame(nw, nh);
    auto small_in = native;
    std::vector<float> depth(nw * nh, 0.5f);
    ResolveConstants c{};
    c.gTransferStrength = 1.0f;
    c.gSharpness = 0.0f;
    c.gEnlargementMode = 1;
    c.gColorStrength = 1.0f;
    c.gIsSkipFrame = 0;
    c.gHasDepth = 0;
    auto identity = h.resolve(current_res(), nw, nh, ww, wh, small_in, small_in, native, depth, c);
    require_close(identity, native, 1.0e-6f, "resolve no-edit identity");

    auto edited = small_in;
    for (auto& v : edited) { v.r += 0.10f; v.g += 0.05f; v.b += 0.025f; }
    c.gTransferStrength = 1.0f;
    auto gain1 = h.resolve(current_res(), nw, nh, ww, wh, small_in, edited, native, depth, c);
    c.gTransferStrength = 1.3f;
    auto gain13 = h.resolve(current_res(), nw, nh, ww, wh, small_in, edited, native, depth, c);
    c.gTransferStrength = 1.5f;
    auto gain15 = h.resolve(current_res(), nw, nh, ww, wh, small_in, edited, native, depth, c);
    for (size_t i = 0; i < native.size(); ++i) {
        const float d1 = gain1[i].r - native[i].r;
        const float d13 = gain13[i].r - native[i].r;
        const float d15 = gain15[i].r - native[i].r;
        if (!(d13 > d1 && d15 > d13)) fail("transfer gain must increase residual strength");
    }

    c.gTransferStrength = 1.0f;
    c.gColorStrength = 0.0f;
    auto luma_only = h.resolve(current_res(), nw, nh, ww, wh, small_in, edited, native, depth, c);
    for (size_t i = 0; i < native.size(); ++i) {
        const float edit_r = luma_only[i].r - native[i].r;
        const float edit_g = luma_only[i].g - native[i].g;
        const float edit_b = luma_only[i].b - native[i].b;
        if (rel_error(edit_r, edit_g) > 1.0e-5f || rel_error(edit_g, edit_b) > 1.0e-5f) fail("ColorStrength=0 should apply luma-only equal RGB edit");
    }
}

void test_resolve_depth_gate_reduces_delta(Harness& h) {
    const uint32_t w = 4, hgt = 4;
    auto native = make_constant(w, hgt, Float4{0.25f, 0.25f, 0.25f, 0.75f});
    auto small_in = native;
    auto small_out = make_constant(w, hgt, Float4{0.75f, 0.75f, 0.75f, 0.10f});
    std::vector<float> depth(w * hgt, 0.5f);
    depth[1 * w + 1] = 0.05f;
    depth[1 * w + 2] = 1.00f;
    ResolveConstants c{};
    c.gTransferStrength = 1.0f;
    c.gSharpness = 0.0f;
    c.gEnlargementMode = 1;
    c.gColorStrength = 1.0f;
    c.gIsSkipFrame = 0;
    c.gHasDepth = 0;
    auto no_depth = h.resolve(current_res(), w, hgt, w, hgt, small_in, small_out, native, depth, c);
    c.gHasDepth = 1;
    auto with_depth = h.resolve(current_res(), w, hgt, w, hgt, small_in, small_out, native, depth, c);
    const size_t idx = 1 * w + 1;
    const float no_depth_delta = std::fabs(no_depth[idx].r - native[idx].r);
    const float with_depth_delta = std::fabs(with_depth[idx].r - native[idx].r);
    if (!(with_depth_delta < no_depth_delta)) fail("depth gate should reduce residual at discontinuity");
}

int run_all_tests() {
    Harness h;
    int tests = 0;
    test_downsample_reflection(h); ++tests;
    test_resolve_reflection(h); ++tests;
    test_filter0_legacy_parity(h); ++tests;
    test_filter1_constant_and_native_identity(h); ++tests;
    test_cpu_bilinear_reference_clamps_raw_neighbors(); ++tests;
    test_filter1_matches_cpu_reference(h); ++tests;
    test_filter1_nonconstant_no_shrink_parity(h); ++tests;
    test_filter1_reduces_stripe_alias_when_shrinking(h); ++tests;
    test_resolve_identity_and_gain(h); ++tests;
    test_resolve_depth_gate_reduces_delta(h); ++tests;
    std::printf("PASS: cost_scaler_shader_warp tests=%d\n", tests);
    return 0;
}

} // namespace

int main() {
    return run_all_tests();
}
