#pragma once

#include <cmath>
#include <cstdint>

namespace dlssnr_lab {

static constexpr float kMinScale = 0.25f;
static constexpr float kMaxScale = 2.0f;
static constexpr float kNativeScaleTolerance = 0.005f;

struct ScaleConfig {
    bool enableProxy = true;
    bool enableAnamorphic = false;
    bool processAtNativeResolution = false;
    float resolutionScale = 0.75f;
    float scaleX = 0.65f;
    float scaleY = 0.85f;
};

struct RouteDecision {
    bool usePrivatePath = false;
    bool processAtNativeResolution = false;
    bool isScalingActive = false;
    bool isNativeTolerance = true;
    float effectiveScaleX = 1.0f;
    float effectiveScaleY = 1.0f;
    uint32_t workW = 0;
    uint32_t workH = 0;
};

inline float ClampScale(float value) {
    if (!std::isfinite(value)) return 1.0f;
    if (value < kMinScale) return kMinScale;
    if (value > kMaxScale) return kMaxScale;
    return value;
}

inline bool IsNativeScale(float scale) {
    return std::fabs(scale - 1.0f) < kNativeScaleTolerance;
}

inline uint32_t ComputeWorkDim(uint32_t nativeDim, float scale) {
    if (nativeDim == 0) return 0;
    if (IsNativeScale(scale)) return nativeDim;
    float scaled = std::round(static_cast<float>(nativeDim) * scale);
    if (scaled < 64.0f) scaled = 64.0f;
    if (scaled >= 4294967296.0f) return UINT32_MAX & ~1u;
    uint32_t result = static_cast<uint32_t>(scaled) & ~1u;
    return result < 64u ? 64u : result;
}

inline RouteDecision DecideRoute(const ScaleConfig& cfg, uint32_t nativeW, uint32_t nativeH) {
    RouteDecision out;
    out.effectiveScaleX = cfg.enableAnamorphic ? ClampScale(cfg.scaleX) : ClampScale(cfg.resolutionScale);
    out.effectiveScaleY = cfg.enableAnamorphic ? ClampScale(cfg.scaleY) : ClampScale(cfg.resolutionScale);
    out.workW = ComputeWorkDim(nativeW, out.effectiveScaleX);
    out.workH = ComputeWorkDim(nativeH, out.effectiveScaleY);
    out.isNativeTolerance = IsNativeScale(out.effectiveScaleX) && IsNativeScale(out.effectiveScaleY);

    if (!cfg.enableProxy || nativeW == 0 || nativeH == 0) {
        return out;
    }

    bool nativeSized = (out.workW == nativeW && out.workH == nativeH);
    out.processAtNativeResolution = cfg.processAtNativeResolution && nativeSized;
    out.isScalingActive = !nativeSized;
    out.usePrivatePath = !out.isNativeTolerance || out.processAtNativeResolution;
    return out;
}

} // namespace dlssnr_lab
