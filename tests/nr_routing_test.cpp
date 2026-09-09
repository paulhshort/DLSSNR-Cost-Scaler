#include "../NrRouting.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using dlssnr_lab::ComputeWorkDim;
using dlssnr_lab::DecideRoute;
using dlssnr_lab::ScaleConfig;

struct ExpectedRoute {
    const char* name;
    ScaleConfig cfg;
    uint32_t nativeW;
    uint32_t nativeH;
    bool usePrivatePath;
    bool processAtNativeResolution;
    bool isScalingActive;
    bool isNativeTolerance;
    float effectiveScaleX;
    float effectiveScaleY;
    uint32_t workW;
    uint32_t workH;
};

static void Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

static void CheckRoute(const ExpectedRoute& expected) {
    auto route = DecideRoute(expected.cfg, expected.nativeW, expected.nativeH);
    std::string name(expected.name);
    Check(route.usePrivatePath == expected.usePrivatePath, name + " usePrivatePath");
    Check(route.processAtNativeResolution == expected.processAtNativeResolution, name + " processAtNativeResolution");
    Check(route.isScalingActive == expected.isScalingActive, name + " isScalingActive");
    Check(route.isNativeTolerance == expected.isNativeTolerance, name + " isNativeTolerance");
    Check(route.effectiveScaleX == expected.effectiveScaleX, name + " effectiveScaleX");
    Check(route.effectiveScaleY == expected.effectiveScaleY, name + " effectiveScaleY");
    Check(route.workW == expected.workW, name + " workW");
    Check(route.workH == expected.workH, name + " workH");
}

int main() {
    ScaleConfig defaults{};

    ScaleConfig disabled = defaults;
    disabled.enableProxy = false;
    disabled.resolutionScale = 0.50f;
    disabled.processAtNativeResolution = true;

    ScaleConfig disabledAnamorphicNative = defaults;
    disabledAnamorphicNative.enableProxy = false;
    disabledAnamorphicNative.enableAnamorphic = true;
    disabledAnamorphicNative.processAtNativeResolution = true;
    disabledAnamorphicNative.scaleX = 0.85f;
    disabledAnamorphicNative.scaleY = 0.65f;

    ScaleConfig nativeOptIn = defaults;
    nativeOptIn.resolutionScale = 1.0f;
    nativeOptIn.processAtNativeResolution = true;

    ScaleConfig nativeNearOptIn = defaults;
    nativeNearOptIn.resolutionScale = 1.004f;
    nativeNearOptIn.processAtNativeResolution = true;

    ScaleConfig scale25 = defaults;
    scale25.resolutionScale = 0.25f;

    ScaleConfig scale50 = defaults;
    scale50.resolutionScale = 0.50f;

    ScaleConfig scale65 = defaults;
    scale65.resolutionScale = 0.65f;

    ScaleConfig scale75 = defaults;
    scale75.resolutionScale = 0.75f;

    ScaleConfig scale85 = defaults;
    scale85.resolutionScale = 0.85f;

    ScaleConfig scale996 = defaults;
    scale996.resolutionScale = 0.996f;

    ScaleConfig scale1004 = defaults;
    scale1004.resolutionScale = 1.004f;

    ScaleConfig scale1006 = defaults;
    scale1006.resolutionScale = 1.006f;

    ScaleConfig scale150 = defaults;
    scale150.resolutionScale = 1.50f;

    ScaleConfig scale200 = defaults;
    scale200.resolutionScale = 2.00f;

    ScaleConfig mixedAxes = defaults;
    mixedAxes.enableAnamorphic = true;
    mixedAxes.scaleX = 1.50f;
    mixedAxes.scaleY = 0.50f;

    ScaleConfig ownerAxes = defaults;
    ownerAxes.enableAnamorphic = true;
    ownerAxes.scaleX = 0.85f;
    ownerAxes.scaleY = 0.65f;

    ScaleConfig bothAxesNativeOverride = defaults;
    bothAxesNativeOverride.enableAnamorphic = true;
    bothAxesNativeOverride.scaleX = 1.0f;
    bothAxesNativeOverride.scaleY = 1.0f;
    bothAxesNativeOverride.processAtNativeResolution = true;

    ScaleConfig tinyClamp = defaults;
    tinyClamp.resolutionScale = 0.001f;

    ScaleConfig highClamp = defaults;
    highClamp.resolutionScale = 99.0f;

    ScaleConfig nonFinite = defaults;
    nonFinite.resolutionScale = std::numeric_limits<float>::quiet_NaN();

    ScaleConfig smallOutsideTolerance = defaults;
    smallOutsideTolerance.resolutionScale = 0.99f;

    ScaleConfig zeroNative = defaults;
    zeroNative.resolutionScale = 0.25f;

    const std::vector<ExpectedRoute> cases = {
        {"proxy off wins", disabled, 3200, 1800, false, false, false, false, 0.50f, 0.50f, 1600, 900},
        {"proxy off anamorphic native opt-in wins", disabledAnamorphicNative, 3200, 1800, false, false, false, false, 0.85f, 0.65f, 2720, 1170},
        {"uniform 25", scale25, 3200, 1800, true, false, true, false, 0.25f, 0.25f, 800, 450},
        {"uniform 50", scale50, 3200, 1800, true, false, true, false, 0.50f, 0.50f, 1600, 900},
        {"uniform 65", scale65, 3200, 1800, true, false, true, false, 0.65f, 0.65f, 2080, 1170},
        {"uniform 75", scale75, 3200, 1800, true, false, true, false, 0.75f, 0.75f, 2400, 1350},
        {"uniform 85", scale85, 3200, 1800, true, false, true, false, 0.85f, 0.85f, 2720, 1530},
        {"default 75", defaults, 3200, 1800, true, false, true, false, 0.75f, 0.75f, 2400, 1350},
        {"native 100 direct", ScaleConfig{true, false, false, 1.0f, 0.65f, 0.85f}, 3200, 1800, false, false, false, true, 1.0f, 1.0f, 3200, 1800},
        {"native 100 opt-in", nativeOptIn, 3200, 1800, true, true, false, true, 1.0f, 1.0f, 3200, 1800},
        {"near native 0.996 direct", scale996, 3200, 1800, false, false, false, true, 0.996f, 0.996f, 3200, 1800},
        {"near native 1.004 direct", scale1004, 3200, 1800, false, false, false, true, 1.004f, 1.004f, 3200, 1800},
        {"near native 1.004 opt-in", nativeNearOptIn, 3200, 1800, true, true, false, true, 1.004f, 1.004f, 3200, 1800},
        {"above tolerance 1.006", scale1006, 3200, 1800, true, false, true, false, 1.006f, 1.006f, 3218, 1810},
        {"uniform 150", scale150, 3200, 1800, true, false, true, false, 1.50f, 1.50f, 4800, 2700},
        {"uniform 200", scale200, 3200, 1800, true, false, true, false, 2.00f, 2.00f, 6400, 3600},
        {"owner anamorphic 85x65", ownerAxes, 3200, 1800, true, false, true, false, 0.85f, 0.65f, 2720, 1170},
        {"mixed axes 150x50", mixedAxes, 3200, 1800, true, false, true, false, 1.50f, 0.50f, 4800, 900},
        {"both axes native override", bothAxesNativeOverride, 3200, 1800, true, true, false, true, 1.0f, 1.0f, 3200, 1800},
        {"odd native super", scale150, 1919, 1079, true, false, true, false, 1.50f, 1.50f, 2878, 1618},
        {"tiny clamps to 64", tinyClamp, 100, 100, true, false, true, false, 0.25f, 0.25f, 64, 64},
        {"high clamps to 2x", highClamp, 100, 100, true, false, true, false, 2.0f, 2.0f, 200, 200},
        {"non-finite native direct", nonFinite, 3200, 1800, false, false, false, true, 1.0f, 1.0f, 3200, 1800},
        {"small outside tolerance native dims", smallOutsideTolerance, 64, 64, true, false, false, false, 0.99f, 0.99f, 64, 64},
        {"zero native direct", zeroNative, 0, 1800, false, false, false, false, 0.25f, 0.25f, 0, 450},
    };

    for (const auto& expected : cases) {
        CheckRoute(expected);
    }

    Check(ComputeWorkDim(UINT32_MAX, 2.0f) == (UINT32_MAX & ~1u), "overflow guard returns max even dimension");

    std::cout << "nr_routing_test passed (" << (cases.size() + 1) << " cases)\n";
    return 0;
}
