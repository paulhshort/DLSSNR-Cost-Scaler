#include "../lab_presets.h"
#include <cstdio>
#include <cstring>

int main() {
    int failures = 0;
    int cases = 0;
    for (const auto& look : cost_scaler_lab::kLooks) {
        for (const auto& resolution : cost_scaler_lab::kResolutions) {
            DlssnrSharedConfig source = {};
            source.magic = DLSSNR_MAGIC;
            source.version = 78;
            source.downsampleFilter = 1;
            source.enableDepthAware = 1;
            source.nrIntensity = 0.77f;
            source.useCustomNR = 1;
            source.debugNativeW = 3200;
            source.debugNativeH = 1800;
            cost_scaler_lab::ApplyResolution(source, resolution);
            DlssnrSharedConfig changed = source;
            cost_scaler_lab::ApplyLook(changed, look);
            const bool controls = changed.enableProxy == 1 && changed.enlargementMode == 1 &&
                changed.transferStrength == look.transfer && changed.colorStrength == look.color &&
                changed.sharpness == look.sharpness && changed.processAtNativeResolution == 1 && changed.enableVrnr == 0;
            // Restore exactly the permitted fields before comparing every byte.
            auto restored = changed;
            restored.enableProxy = source.enableProxy;
            restored.enlargementMode = source.enlargementMode;
            restored.transferStrength = source.transferStrength;
            restored.colorStrength = source.colorStrength;
            restored.sharpness = source.sharpness;
            restored.processAtNativeResolution = source.processAtNativeResolution;
            restored.enableVrnr = source.enableVrnr;
            const bool isolatedLook = std::memcmp(&source, &restored, sizeof(source)) == 0;

            auto resized = changed;
            cost_scaler_lab::ApplyResolution(resized, cost_scaler_lab::kResolutions[0]);
            resized.enableAnamorphic = changed.enableAnamorphic;
            resized.resolutionScale = changed.resolutionScale;
            resized.scaleX = changed.scaleX;
            resized.scaleY = changed.scaleY;
            const bool isolatedResolution = std::memcmp(&changed, &resized, sizeof(changed)) == 0;
            const bool bounded = look.transfer >= 0.0f && look.transfer <= 2.0f && look.color >= 0.0f &&
                look.color <= 1.0f && look.sharpness >= 0.0f && look.sharpness <= 1.0f &&
                resolution.x >= 0.25f && resolution.x <= 2.0f && resolution.y >= 0.25f && resolution.y <= 2.0f;
            const bool passed = controls && isolatedLook && isolatedResolution && bounded;
            failures += !passed;
            ++cases;
            if (!passed) std::printf("FAIL %s / %s\n", look.name, resolution.name);
        }
    }
    std::printf("Look/resolution independence: %d/%d passed\n", cases - failures, cases);
    return failures ? 1 : 0;
}
