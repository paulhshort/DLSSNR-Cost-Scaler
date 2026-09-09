#pragma once
#include "dlssnr_shared.h"

namespace cost_scaler_lab {
struct LookPreset {
    const char* name;
    float transfer;
    float color;
    float sharpness;
};

inline constexpr LookPreset kLooks[] = {
    { "Stock resolve", 1.00f, 1.00f, 0.20f },
    { "Natural",       1.00f, 0.80f, 0.05f },
    { "Wow",           1.30f, 1.00f, 0.10f },
    { "Bold",          1.50f, 1.00f, 0.05f },
};

// Look changes cannot reset the user's resolution, filter, depth, or model settings.
inline void ApplyLook(DlssnrSharedConfig& config, const LookPreset& look) {
    config.enableProxy = 1;
    config.enlargementMode = 1;
    config.transferStrength = look.transfer;
    config.colorStrength = look.color;
    config.sharpness = look.sharpness;
    config.processAtNativeResolution = 1;
    config.enableVrnr = 0;
}

struct ResolutionPreset {
    const char* name;
    float x;
    float y;
};

inline constexpr ResolutionPreset kResolutions[] = {
    { "Fast 65x65",      0.65f, 0.65f },
    { "85x65",           0.85f, 0.65f },
    { "Quality 90x85",   0.90f, 0.85f },
    { "Native 100%",     1.00f, 1.00f },
    { "Photo 125%",      1.25f, 1.25f },
};

// Resolution changes do not alter the chosen look or turn native processing on/off.
inline void ApplyResolution(DlssnrSharedConfig& config, const ResolutionPreset& preset) {
    config.enableAnamorphic = preset.x != preset.y;
    config.resolutionScale = preset.x;
    config.scaleX = preset.x;
    config.scaleY = preset.y;
}
} // namespace cost_scaler_lab
