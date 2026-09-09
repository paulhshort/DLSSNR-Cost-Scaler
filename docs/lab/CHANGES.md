# Cost Scaler Lab changes

Personal branch based on upstream v1.0.5 commit `9bb03663d690b84ec00cdb55fb3a1a04dd881e02`, with supersampling-fix commit `7dc3dac672b1ad280292144b4edbe1833c9ea374`. The package manifest identifies the exact final lab commit.

- Added Stock resolve, Natural, Wow, and Bold look presets. They change matched-residual transfer, color transfer, and sharpening, without changing resolution, input filter, depth, or caller model controls.
- Added separate Fast 65x65, 85x65, Quality 90x85, Native 100%, and Photo 125% resolution buttons. The existing 25%-200% sliders remain available. Added an X/Y swap button.
- Added `ProcessAtNativeResolution=1` to keep working textures and resolve active at 1:1. This makes effect controls available at full NR resolution. Its default is off; the look buttons enable it.
- Added `DownsampleFilter=1`, an optional bilinear footprint prefilter on shrinking axes. It uses two samples when one axis shrinks and four when both shrink. Non-shrinking axes get no filter offset. Legacy filter 0 remains the default.
- Reused the existing single private NGX evaluate path. No additional inference pass or new model is introduced.
- Added a lab-specific configuration mapping and one canonical shared header. Install the lab proxy and lab companion as a pair.
- Added routing tests, look/resolution isolation tests, installer tests, shader reflection checks, and software shader tests. Fresh FXC output must match both embedded shader headers before packaging.
- Added an upgrade/restore helper that preserves the working INI, model, ShortFuse, and ReShade. The helper only installs into an identified existing Cost Scaler setup.

The original resolve shader is unchanged. Greater transfer amplifies its existing edit, including unwanted changes. The optional input filter operates in the caller's image values; it does not establish linear-light HDR processing or guarantee less shimmer in a game.

The lab retains inherited resource-lifetime, guide-buffer, and custom-model-control limitations documented in the separate implementation study. Its test suite does not load an NGX model or reproduce a game's D3D12 command stream. CPU/WARP success does not establish RTX frame times, VRAM use, neural compatibility, or visual quality.

Source: [personal Cost Scaler repository](https://github.com/paulhshort/DLSSNR-Cost-Scaler).
Upstream: [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler).
Supersampling contribution: [upstream PR #8](https://github.com/xenmods/DLSSNR-Cost-Scaler/pull/8).

Original Cost Scaler credits and MIT license are preserved. New lab code and the independently written software shader harness follow that license. The legacy shader test fixture is from the pinned MIT Cost Scaler source. No OptiScaler GPL source was copied into this branch.
