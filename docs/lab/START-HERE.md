# Tonight: ShortFuse + Cost Scaler Lab

This personal build adds stronger look presets, independent resolution choices, an optional input prefilter, and look controls at 100% NR. It includes the uniform supersampling fix submitted as upstream PR #8.

**Start with your working ShortFuse setup. Upgrade only the Cost Scaler proxy and its companion.** Keep your working model, ReShade, ShortFuse, HDR, SR quality, frame generation, and Smooth Motion settings. This package contains no model or ShortFuse binary.

## Install into an existing Cost Scaler setup

1. Close the game. Extract this package outside the game directory.
2. Open PowerShell in the extracted package directory.
3. Preview the two-file upgrade. Substitute the directory that contains your current Cost Scaler DLL and INI:

```powershell
.\Install-Lab.ps1 -GameDirectory 'D:\Games\YourGame\Binaries\Win64'
```

4. Check the printed paths. Apply that upgrade:

```powershell
.\Install-Lab.ps1 -GameDirectory 'D:\Games\YourGame\Binaries\Win64' -Apply
```

The helper preserves your existing INI. It backs up the old proxy, companion if present, and INI under this extracted package's `backups` directory. It replaces only `nvngx_dlssnr.dll` and `dlssnr-companion.addon64`. It never renames or replaces `nvngx_dlssnr_real.dll`, ShortFuse, or ReShade.

The helper accepts the exact reviewed v1.0.5 proxy, the prior local supersampling-fix build, or this package's own proxy. If it reports an unknown proxy, stop and identify that file before replacing it. Do not rename an unknown DLL to force the installation.

Use the supplied lab proxy and companion together. Their configuration interface is separate from the stock companion. Do not leave duplicate companion add-ons in another ReShade add-on search directory.

## First scene: pick a look

Open ReShade with your existing menu key. Select **Cost Scaler Lab**, then **Tonight: look and performance**.

Click **Wow** first. It keeps your resolution and filter, selects matched residual, and increases the existing NR edit to 1.30x. It also enables processing at native resolution and disables alternating-frame NR. It leaves the caller's model controls and depth setting unchanged.

| Look | Edit transfer | Color transfer | Sharpening | Purpose |
| --- | --- | --- | --- | --- |
| Stock resolve | 1.00 | 1.00 | 0.20 | Known upstream resolve defaults; not proof of your exact previous settings |
| Natural | 1.00 | 0.80 | 0.05 | Less color shift and sharpening |
| Wow | 1.30 | 1.00 | 0.10 | Stronger neural lighting and color edits |
| Bold | 1.50 | 1.00 | 0.05 | More pronounced edits for comparison |

These are starting points, not measured quality rankings. Greater transfer also amplifies unwanted edits. If faces become waxy, shadows collapse, or highlights look excessive, lower **Transfer Strength**. The full 0.00-2.00 slider remains available.

Look presets do not reset X/Y resolution, the input filter, depth control, or custom model parameters. The presets add no extra neural inference pass. Any actual visual benefit still needs your game test.

## Then choose resolution for this title

Resolution presets keep the look you selected. All original uniform and X/Y sliders remain available from 25% to 200%.

| Resolution button | X / Y | NR pixel count before rounding |
| --- | --- | --- |
| Fast 65x65 | 65% / 65% | 42.25% |
| 85x65 | 85% / 65% | 55.25% |
| Quality 90x85 | 90% / 85% | 76.50% |
| Native 100% | 100% / 100% | 100% |
| Photo 125% | 125% / 125% | 156.25% |

These percentages describe NR input pixels, not FPS or milliseconds. The reference size is the incoming NR color texture; it is not necessarily the display resolution.

For Dawnwalker, start from your existing roughly 85% x 65% and choose Wow. If the frame time leaves room, try Quality 90x85, then Native 100%. Try Bold at the resolution that feels good in motion. Photo 125%, 150%, and 200% are optional comparisons with substantially greater pixel counts and possible VRAM pressure.

Your axis order was not certain. **Swap X / Y** exchanges the axes without changing the pixel count or your look. Compare foliage, hair, fences, and camera pans to choose the orientation that fits the title.

## Two new switches

**NR input filter:** Leave **Legacy bilinear** selected for the first look comparison. Then change only this selector to **Prefilter (experimental)**. It averages extra samples on shrinking axes. It can reduce some aliasing in the model input, but may also soften evidence the model uses. The model still evaluates once per normal NR call. Native and enlarged axes receive no extra filter offset.

**Apply look controls at 100% NR:** Look presets enable this. It keeps the working-texture and resolve path active at 1:1, so transfer, color, and sharpening controls can affect the result. This needs more texture memory and processing than stock native passthrough. Uncheck it to restore direct forwarding at native size. The switch does not prevent reduced-resolution or supersampling operation.

Do not use the proxy toggle as an NR-off comparison. Disabling Cost Scaler forwards to the real NR feature at the caller's native size. Use ShortFuse's NR toggle for a true NR on/off comparison.

## A short comparison that gives useful answers

1. Keep the same scene and camera. Keep HDR, DLSS-SR quality, frame generation, and other effects fixed.
2. Compare Stock resolve, Wow, and Bold at the same NR resolution. Give each change time to settle.
3. Keep your chosen look. Adjust NR resolution until camera motion and frame time feel right.
4. Compare Legacy versus Prefilter with every other setting fixed.
5. Inspect a face, foliage, bright lighting, and a dark interior. A dramatic still frame is only one part of the result.

Your prior 60+ FPS result used frame generation. Keep displayed FPS and base-render FPS separate if your overlay provides both. For a later timing diagnosis, repeat a short sequence with FG off; tonight's first visual comparison can retain the working FG setup.

The companion's **Copy Debug Info to Clipboard** includes requested filter/native-processing settings and actual work dimensions. The proxy log identifies native processing and scaled feature creation. These signals establish the selected path; they do not measure a quality gain.

## Roll back

Close the game. Use the exact backup folder printed during installation:

```powershell
.\Install-Lab.ps1 -GameDirectory 'D:\Games\YourGame\Binaries\Win64' -RestoreBackup '.\backups\YOUR-BACKUP-FOLDER'
```

Check the preview, then repeat with `-Apply`. Restore preserves a copy of the current files before restoring the old proxy, companion state, and INI. Keep the whole extracted package and its backups until testing is complete.

## Other routes and evidence limits

This Cost Scaler build does not move NR before super resolution. Your separate September 8 NR Lab package retains the OptiScaler pre-SR option and source-reference decode experiments. Test that package in its own setup if pre-SR cost reduction is the priority; do not combine it with this ShortFuse test.

For Days Gone, first verify the current ShortFuse route evaluates NR in the actual game. The current Feeder instructions exclude combining Feeder with ShortFuse. A lack of native DLSS alone does not establish which bridge is required.

**Confirmed:** source compilation, CPU control tests, shader bytecode correspondence, and software shader tests are recorded in `TEST-RESULTS.json` and `BUILD-MANIFEST.json` for the packaged commit.

**Inferred:** the prefilter can calm some model-input aliasing, and increased transfer can make existing edits more visible.

**Unknown:** RTX 4070 Ti frame times, model acceptance, VRAM use, game stability, and visual superiority. No proprietary model or game was run for this build. This is a personal experimental build, not an upstream release or an official NVIDIA configuration.
