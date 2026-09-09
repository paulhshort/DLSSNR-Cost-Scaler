// Included after the companion state and shared-memory helpers.
static void ApplyLabChange() {
    s_scaleDragging = false;
    s_dirty = true;
    s_lastChangeTick = 0;
    PushToSharedMemory(1);
}

static void DrawLabControls() {
    if (!ImGui::CollapsingHeader("Tonight: look and performance", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextWrapped("Choose a look, then adjust NR resolution independently for this game.");
    for (const auto& look : cost_scaler_lab::kLooks) {
        if (&look != &cost_scaler_lab::kLooks[0]) ImGui::SameLine();
        if (ImGui::Button(look.name)) {
            DlssnrSharedConfig config = {};
            cost_scaler_lab::ApplyLook(config, look);
            s_enableProxy = config.enableProxy != 0;
            s_enlargementMode = static_cast<int>(config.enlargementMode);
            s_transferStrength = config.transferStrength;
            s_colorStrength = config.colorStrength;
            s_sharpness = config.sharpness;
            s_processAtNativeResolution = config.processAtNativeResolution != 0;
            s_enableVrnr = config.enableVrnr != 0;
            ApplyLabChange();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("NR edit %.2fx | color %.2f | sharpening %.2f\nKeeps your resolution, input filter, depth and caller model settings.\nEnables matched residual and processing at 100%%; disables alternating frames.",
                look.transfer, look.color, look.sharpness);
        }
    }
    ImGui::TextWrapped("Wow and Bold amplify the existing NR edit. Reduce Transfer Strength if faces, shadows or highlights look exaggerated.");
    ImGui::Text("Current edit: %.2fx | color: %.2f | sharpening: %.2f", s_transferStrength, s_colorStrength, s_sharpness);

    ImGui::Spacing();
    ImGui::TextUnformatted("NR resolution - keeps your chosen look:");
    for (const auto& preset : cost_scaler_lab::kResolutions) {
        // Wrap into two short rows rather than forcing a wide overlay.
        const auto index = &preset - &cost_scaler_lab::kResolutions[0];
        if (index != 0 && index != 3) ImGui::SameLine();
        if (ImGui::Button(preset.name)) {
            DlssnrSharedConfig config = {};
            cost_scaler_lab::ApplyResolution(config, preset);
            s_enableAnamorphic = config.enableAnamorphic != 0;
            s_resolutionScale = config.resolutionScale;
            s_scaleX = config.scaleX;
            s_scaleY = config.scaleY;
            ApplyLabChange();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("X %.0f%%, Y %.0f%%: %.1f%% of input pixel count.\nPixel count is not a measured GPU time or FPS estimate.",
                preset.x * 100.0f, preset.y * 100.0f, preset.x * preset.y * 100.0f);
        }
    }
    if (s_enableAnamorphic && ImGui::SmallButton("Swap X / Y")) {
        std::swap(s_scaleX, s_scaleY);
        ApplyLabChange();
    }
    const float x = s_enableAnamorphic ? s_scaleX : s_resolutionScale;
    const float y = s_enableAnamorphic ? s_scaleY : s_resolutionScale;
    ImGui::Text("Requested NR pixels: %.1f%% | X %.0f%% / Y %.0f%%", x * y * 100.0f, x * 100.0f, y * 100.0f);
    ImGui::TextWrapped("Use the resolution sliders below for any value from 25% to 200%. Photo presets can raise frame time and VRAM use substantially.");

    const char* filters[] = { "Legacy bilinear", "Prefilter (experimental)" };
    if (ImGui::Combo("NR input filter", &s_downsampleFilter, filters, 2)) ApplyLabChange();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Prefilter averages extra samples on shrinking axes only.\nTry it for fine-detail shimmer; it may soften the model input.\nIt adds sampling work, not another NR evaluation. Native/upscale axes are unchanged.");
    }
    if (ImGui::Checkbox("Apply look controls at 100% NR", &s_processAtNativeResolution)) ApplyLabChange();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("At 100%%, keeps the working-texture and resolve path active for look controls.\nAdds texture memory and processing overhead compared with direct passthrough.\nUncheck to use the stock 100%% passthrough path.");
    }
    if (!s_processAtNativeResolution && x > 0.995f && x < 1.005f && y > 0.995f && y < 1.005f) {
        ImGui::TextWrapped("At native passthrough, the look sliders do not change the output.");
    }
    ImGui::Separator();
}
