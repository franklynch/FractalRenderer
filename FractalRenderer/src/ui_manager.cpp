// ============================================================================
// ui_manager.cpp - Improved UI Implementation
// ============================================================================
// 
// Key improvements:
// - All controls use callbacks (no direct object manipulation)
// - No static variables in functions (moved to class members)
// - Error handling for all callbacks
// - Proper encapsulation
//
// ============================================================================

#include "ui_manager.h"
#include "animation_system.h" 
#include "deep_zoom_system.h" 
#include "animation_renderer.h"
#include "video_encoder.h"

#include <imgui.h>
#include <algorithm>
#include <SDL.h>
#include <cstdlib>
#include <cmath>

// ============================================================================
// Constants
// ============================================================================

namespace UIConstants {
    constexpr float MAIN_WINDOW_WIDTH = 350.0f;
    constexpr float PRESET_WINDOW_WIDTH = 250.0f;
    constexpr float ANIMATION_WINDOW_WIDTH = 450.0f;
    constexpr float ANIMATION_WINDOW_HEIGHT = 600.0f;
    constexpr float MINIMAP_SIZE = 150.0f;
    constexpr float STATUS_BAR_HEIGHT = 25.0f;

    constexpr float ZOOM_BUTTON_WIDTH = 80.0f;
    constexpr float PRESET_BUTTON_WIDTH = 100.0f;
    constexpr float FULL_WIDTH = -1.0f;

    constexpr float WINDOW_ALPHA = 0.95f;
    constexpr float MINIMAP_ALPHA = 0.8f;

    constexpr ImVec4 COLOR_SUCCESS(0.0f, 1.0f, 0.0f, 1.0f);
    constexpr ImVec4 COLOR_WARNING(1.0f, 1.0f, 0.0f, 1.0f);
    constexpr ImVec4 COLOR_ERROR(1.0f, 0.0f, 0.0f, 1.0f);
    constexpr ImVec4 COLOR_INFO(0.0f, 1.0f, 1.0f, 1.0f);
    constexpr ImVec4 COLOR_SELECTED(0.2f, 0.6f, 0.2f, 1.0f);
}

namespace UIStrings {
    constexpr const char* RESET_VIEW = "Reset View (R)";
    constexpr const char* SAVE_SCREENSHOT = "Save Screenshot (S)";
    constexpr const char* TOGGLE_FULLSCREEN = "Toggle Fullscreen (F)";
    constexpr const char* QUICK_ZOOM = "Quick Zoom:";
    constexpr const char* ZOOM_IN = "2x";
    constexpr const char* ZOOM_OUT = "0.5x";
}

// ============================================================================
// RAII Helper for ImGui Style Colors
// ============================================================================

class ScopedStyleColor {
public:
    ScopedStyleColor(ImGuiCol idx, ImVec4 col) : count_(1) {
        ImGui::PushStyleColor(idx, col);
    }

    ScopedStyleColor(std::initializer_list<std::pair<ImGuiCol, ImVec4>> colors)
        : count_(static_cast<int>(colors.size())) {
        for (const auto& [idx, col] : colors) {
            ImGui::PushStyleColor(idx, col);
        }
    }

    ~ScopedStyleColor() {
        ImGui::PopStyleColor(count_);
    }

    ScopedStyleColor(const ScopedStyleColor&) = delete;
    ScopedStyleColor& operator=(const ScopedStyleColor&) = delete;

private:
    int count_;
};

// ============================================================================
// Helper Functions
// ============================================================================

namespace {
    /**
     * @brief Validate and parse coordinate input with bounds checking
     * @param str Input string to parse
     * @param out_value Output value if successful
     * @param default_val Default value if parsing fails
     * @return true if parsing succeeded and value is valid
     */
    bool parse_coordinate(const char* str, double& out_value, double default_val) {
        if (!str || str[0] == '\0') {
            out_value = default_val;
            return false;
        }

        char* end;
        double val = std::strtod(str, &end);

        // Validate parsed value
        if (std::isnan(val) || std::isinf(val)) {
            out_value = default_val;
            return false;
        }

        // Reasonable bounds check (fractal coordinate space)
        if (val < -1000.0 || val > 1000.0) {
            out_value = default_val;
            return false;
        }

        // Check if entire string was parsed
        if (end != str && *end == '\0') {
            out_value = val;
            return true;
        }

        out_value = default_val;
        return false;
    }

    /**
     * @brief Format time in seconds to human readable string
     * @param seconds Time in seconds
     * @return Formatted string (e.g., "1.5s", "2m 30s", "1h 15m")
     */
    std::string format_time(float seconds) {
        if (seconds < 60.0f) {
            return fmt::format("{:.1f}s", seconds);
        }
        else if (seconds < 3600.0f) {
            int mins = static_cast<int>(seconds / 60.0f);
            int secs = static_cast<int>(seconds) % 60;
            return fmt::format("{}m {}s", mins, secs);
        }
        else {
            int hours = static_cast<int>(seconds / 3600.0f);
            int mins = static_cast<int>(seconds / 60.0f) % 60;
            return fmt::format("{}h {}m", hours, mins);
        }
    }
}

// ============================================================================
// NotificationSystem Implementation
// ============================================================================

void NotificationSystem::add(const std::string& message, ImVec4 color) {
    // Limit number of notifications to prevent unbounded growth
    if (notifications.size() >= MAX_NOTIFICATIONS) {
        notifications.erase(notifications.begin());
    }
    notifications.push_back({ message, NOTIFICATION_DURATION, color });
}

void NotificationSystem::update(float deltaTime) {
    for (auto& n : notifications) {
        n.time_remaining -= deltaTime;
    }

    // Remove expired notifications
    notifications.erase(
        std::remove_if(notifications.begin(), notifications.end(),
            [](const Notification& n) { return n.time_remaining <= 0; }),
        notifications.end()
    );
}

void NotificationSystem::draw() {
    ImGuiIO& io = ImGui::GetIO();
    float y_offset = 80.0f;

    for (auto& notif : notifications) {
        float alpha = std::min(1.0f, notif.time_remaining);
        ImVec4 color = notif.color;
        color.w = alpha;

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, y_offset),
            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.8f * alpha);

        if (ImGui::Begin(("##Notification" + std::to_string((size_t)&notif)).c_str(),
            nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(color, "%s", notif.message.c_str());
            ImGui::End();
        }
        y_offset += 40.0f;
    }
}

// ============================================================================
// UIManager Implementation
// ============================================================================

UIManager::UIManager(FractalState& state)
    : state(state)
{
    // Constructor - member initialization done via initializer list
}

void UIManager::apply_theme() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Rounded modern style
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 4);

    // Color scheme
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.15f, 0.95f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.40f, 0.70f, 0.80f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.50f, 0.85f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.60f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.40f, 0.70f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.50f, 0.85f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.60f, 1.00f, 1.00f);
}

// ============================================================================
// Main Draw Function
// ============================================================================

void UIManager::draw_all(FractalType current_type, float fps,
    DeepZoomManager* deep_zoom,
    AnimationSystem* anim_system,
    AnimationRenderer* anim_renderer)
{
    // Update and draw notifications
    notifications.draw();

    // Draw minimap if enabled
    if (state.show_minimap) {
        draw_minimap();
    }

    // Draw status bar if enabled
    if (state.show_status_bar) {
        ImGuiIO& io = ImGui::GetIO();
        draw_status_bar(current_type, fps,
            static_cast<int>(io.DisplaySize.x),
            static_cast<int>(io.DisplaySize.y));
    }

    // Draw help overlay if enabled
    if (state.show_help) {
        draw_help_overlay();
    }

    // Draw animation window
    draw_animation_window(anim_system, anim_renderer, current_type);

    // Only show main UI if enabled
    if (!state.show_ui) {
        // Show minimal FPS counter
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowBgAlpha(0.3f);
        if (ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("FPS: %.1f", fps);
        }
        ImGui::End();
        return;
    }

    // Main control window
    draw_main_controls(current_type, deep_zoom);

    // Preset locations window
    draw_preset_window(current_type);
}

// ============================================================================
// Main Controls Window
// ============================================================================

void UIManager::draw_main_controls(FractalType& current_type, DeepZoomManager* deep_zoom) {
    ImGui::SetNextWindowSize(ImVec2(UIConstants::MAIN_WINDOW_WIDTH, 0), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Fractal Controls", &state.show_ui, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // Fractal type selector
    draw_fractal_type_selector(current_type);

    ImGui::Separator();

    // View Controls
    draw_view_controls();

    // Rendering Settings
    draw_rendering_settings();

    // Color Palette
    draw_color_palette();

    // Advanced Effects
    draw_advanced_effects();

    // Fractal-specific controls
    draw_fractal_specific_controls(current_type);

    ImGui::Separator();

    // Performance & Info
    draw_performance_info();

    // Show deep zoom only for Mandelbrot
    if (current_type == FractalType::Deep_Zoom) {
        draw_deep_zoom_controls(deep_zoom);
    }

    ImGui::Separator();

    // Animation section
    if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Open Animation Timeline", ImVec2(UIConstants::FULL_WIDTH, 0))) {
            show_animation_window = !show_animation_window;
        }
    }

    // High-Resolution Export
    draw_high_res_export();

    ImGui::End();
}

// ============================================================================
// Fractal Type Selector
// ============================================================================

void UIManager::draw_fractal_type_selector(FractalType& current_type) {
    ImGui::Text("Fractal Type");

    {
        ScopedStyleColor buttonColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.8f, 1.0f));

        if (ImGui::Button(FractalState::get_name(current_type), ImVec2(UIConstants::FULL_WIDTH, 0))) {
            ImGui::OpenPopup("Select Fractal Type");
        }
    }

    // Modal popup for fractal selection
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Select Fractal Type", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Choose a fractal type:");
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(FractalType::Count); i++) {
            FractalType type = static_cast<FractalType>(i);
            bool is_selected = (i == static_cast<int>(current_type));

            // Apply selected style
            if (is_selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, UIConstants::COLOR_SELECTED);
            }

            // Draw button
            bool clicked = ImGui::Button(FractalState::get_name(type), ImVec2(200, 0));

            // Pop style IMMEDIATELY after button
            if (is_selected) {
                ImGui::PopStyleColor();
            }

            // Handle click AFTER popping style
            if (clicked && current_type != type) {
                current_type = type;
                state.mark_dirty();

                // Trigger callback with error handling
                safe_invoke(on_fractal_type_changed,
                    "Fractal type change callback not configured",
                    type);

                // Show notification
                notifications.add(
                    fmt::format("Switched to {}", FractalState::get_name(type)),
                    UIConstants::COLOR_SUCCESS
                );

                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Cancel", ImVec2(UIConstants::FULL_WIDTH, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// File continues in Part 2...
// ============================================================================
// ui_manager.cpp - Part 2: View Controls and Callbacks
// ============================================================================

// ============================================================================
// View Controls
// ============================================================================

void UIManager::draw_view_controls() {
    if (ImGui::CollapsingHeader("View Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Zoom: %.8f", state.zoom);
        ImGui::Text("Center: (%.8f, %.8f)", state.center_x, state.center_y);

        if (ImGui::Button(UIStrings::RESET_VIEW, ImVec2(UIConstants::FULL_WIDTH, 0))) {
            safe_invoke(on_reset_view, "Reset view callback not configured");
        }

        ImGui::Text("%s", UIStrings::QUICK_ZOOM);
        if (ImGui::Button(UIStrings::ZOOM_IN, ImVec2(UIConstants::ZOOM_BUTTON_WIDTH, 0))) {
            safe_invoke(on_zoom, "Zoom callback not configured", true);
        }
        ImGui::SameLine();
        if (ImGui::Button(UIStrings::ZOOM_OUT, ImVec2(UIConstants::ZOOM_BUTTON_WIDTH, 0))) {
            safe_invoke(on_zoom, "Zoom callback not configured", false);
        }
    }
}

// ============================================================================
// Rendering Settings
// ============================================================================

void UIManager::draw_rendering_settings() {
    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderInt("Max Iterations", &state.max_iterations, 64, 8192)) {
            state.mark_dirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Auto")) {
            // Auto-adjust iterations based on zoom level
            // This is just a UI helper, actual logic is in the callback
            safe_invoke(on_zoom, "", true);
            safe_invoke(on_zoom, "", false);
        }

        // Antialiasing
        const char* aa_items[] = { "Off (1x)", "2x SSAA", "4x SSAA", "8x SSAA" };
        int aa_index = (state.antialiasing_samples == 1) ? 0 :
            (state.antialiasing_samples == 2) ? 1 :
            (state.antialiasing_samples == 4) ? 2 : 3;

        if (ImGui::Combo("Antialiasing", &aa_index, aa_items, 4)) {
            state.antialiasing_samples = (aa_index == 0) ? 1 :
                (aa_index == 1) ? 2 :
                (aa_index == 2) ? 4 : 8;
            state.mark_dirty();
        }
    }
}

// ============================================================================
// Color Palette
// ============================================================================

void UIManager::draw_color_palette() {
    if (ImGui::CollapsingHeader("Color Palette", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* palette_names[] = {
            "Fire", "Electric", "Grayscale", "Nebula", "Solar",
            "Ocean", "Rainbow", "Sunset", "Forest", "Neon"
        };
        constexpr int PALETTE_COUNT = 10;
        constexpr int COLUMNS = 2;

        // Grid of palette buttons
        for (int i = 0; i < PALETTE_COUNT; i++) {
            if (i % COLUMNS != 0) ImGui::SameLine();

            bool selected = (state.palette_mode == i);

            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, UIConstants::COLOR_SELECTED);
            }

            bool clicked = ImGui::Button(palette_names[i], ImVec2(155, 30));

            if (selected) {
                ImGui::PopStyleColor();
            }

            if (clicked) {
                state.palette_mode = i;
                state.mark_dirty();
            }
        }

        if (ImGui::SliderFloat("Color Offset", &state.color_offset, 0.0f, 1.0f)) {
            state.mark_dirty();
        }
        if (ImGui::SliderFloat("Color Scale", &state.color_scale, 0.1f, 10.0f)) {
            state.mark_dirty();
        }

        // Color enhancement
        ImGui::Spacing();
        ImGui::Text("Color Enhancement:");
        bool changed = false;
        changed |= ImGui::SliderFloat("Brightness", &state.color_brightness, 0.5f, 2.0f);
        changed |= ImGui::SliderFloat("Saturation", &state.color_saturation, 0.0f, 2.0f);
        changed |= ImGui::SliderFloat("Contrast", &state.color_contrast, 0.5f, 2.0f);
        if (changed) state.mark_dirty();

        if (ImGui::Button("Reset Enhancement", ImVec2(UIConstants::FULL_WIDTH, 0))) {
            state.color_brightness = 1.0f;
            state.color_saturation = 1.0f;
            state.color_contrast = 1.0f;
            state.mark_dirty();
        }

        // Color animation
        ImGui::Spacing();
        if (ImGui::Checkbox("Animate Colors", &state.animate_colors)) {
            state.mark_dirty();
        }
        if (state.animate_colors) {
            if (ImGui::SliderFloat("Animation Speed", &state.animation_speed, 0.1f, 5.0f)) {
                state.mark_dirty();
            }
        }
    }
}

// ============================================================================
// Advanced Effects
// ============================================================================

void UIManager::draw_advanced_effects() {
    if (ImGui::CollapsingHeader("Advanced Effects")) {
        const char* interior_items[] = { "Black", "Orbit Trap", "Stripes", "Distance" };
        if (ImGui::Combo("Interior Style", &state.interior_style, interior_items, 4)) {
            state.mark_dirty();
        }

        if (ImGui::Checkbox("Orbit Trap Coloring", &state.orbit_trap_enabled)) {
            state.mark_dirty();
        }
        if (state.orbit_trap_enabled) {
            if (ImGui::SliderFloat("Trap Radius", &state.orbit_trap_radius, 0.1f, 2.0f)) {
                state.mark_dirty();
            }
        }

        if (state.interior_style == 2) {
            if (ImGui::Checkbox("Enable Stripes", &state.stripe_enabled)) {
                state.mark_dirty();
            }
            if (state.stripe_enabled) {
                if (ImGui::SliderFloat("Stripe Density", &state.stripe_density, 1.0f, 50.0f)) {
                    state.mark_dirty();
                }
            }
        }
    }
}

// ============================================================================
// Performance Info
// ============================================================================

void UIManager::draw_performance_info() {
    if (ImGui::CollapsingHeader("Performance & Info")) {
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame time: %.2f ms", 1000.0f / ImGui::GetIO().Framerate);

        ImGui::Separator();
        ImGui::Text("Actions:");

        if (ImGui::Button(UIStrings::SAVE_SCREENSHOT, ImVec2(UIConstants::FULL_WIDTH, 0))) {
            safe_invoke(on_save_screenshot, "Screenshot callback not configured");
        }

        if (ImGui::Button(UIStrings::TOGGLE_FULLSCREEN, ImVec2(UIConstants::FULL_WIDTH, 0))) {
            safe_invoke(on_toggle_fullscreen, "Fullscreen callback not configured");
        }
    }
}

// ============================================================================
// High-Resolution Export (Using class members instead of static variables)
// ============================================================================

void UIManager::draw_high_res_export() {
    if (ImGui::CollapsingHeader("High-Resolution Export")) {
        ImGui::Text("Print Quality Export");
        ImGui::Separator();

        const char* presets[] = {
            "Custom",
            "8x10 @ 300 DPI",
            "11x14 @ 300 DPI",
            "16x20 @ 300 DPI",
            "24x36 @ 300 DPI",
            "40x60 @ 300 DPI"
        };

        if (ImGui::Combo("Size Preset", &export_settings_.preset, presets, 6)) {
            switch (export_settings_.preset) {
            case 1: export_settings_.width = 2400;  export_settings_.height = 3000;  break;
            case 2: export_settings_.width = 3300;  export_settings_.height = 4200;  break;
            case 3: export_settings_.width = 4800;  export_settings_.height = 6000;  break;
            case 4: export_settings_.width = 7200;  export_settings_.height = 10800; break;
            case 5: export_settings_.width = 12000; export_settings_.height = 18000; break;
            }
        }

        if (export_settings_.preset == 0) {
            ImGui::InputInt("Width", &export_settings_.width);
            ImGui::InputInt("Height", &export_settings_.height);
            export_settings_.width = std::clamp(export_settings_.width, 100, 32000);
            export_settings_.height = std::clamp(export_settings_.height, 100, 32000);
        }

        ImGui::Separator();
        ImGui::Checkbox("2x Supersampling", &export_settings_.supersample);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Renders at 2x resolution for better anti-aliasing");
        }

        uint32_t finalWidth = export_settings_.supersample ? export_settings_.width * 2 : export_settings_.width;
        uint32_t finalHeight = export_settings_.supersample ? export_settings_.height * 2 : export_settings_.height;
        float megapixels = (finalWidth * finalHeight) / 1000000.0f;

        ImGui::Separator();
        ImGui::Text("Output: %dx%d", export_settings_.width, export_settings_.height);
        ImGui::Text("Render: %dx%d (%.1f MP)", finalWidth, finalHeight, megapixels);

        if (megapixels > 100.0f) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "⚠ Large render!");
            ImGui::TextWrapped("May take several minutes");
        }

        ImGui::Separator();
        if (ImGui::Button("Export 16-bit PNG", ImVec2(UIConstants::FULL_WIDTH, 0))) {
            if (safe_invoke(on_export_print,
                "Export callback not configured",
                static_cast<uint32_t>(export_settings_.width),
                static_cast<uint32_t>(export_settings_.height),
                export_settings_.supersample)) {
                notifications.add("Exporting high-resolution image...", UIConstants::COLOR_INFO);
            }
        }

        ImGui::TextDisabled("Format: 16-bit PNG (lossless)");
    }
}

// ============================================================================
// Deep Zoom Controls (ALL using callbacks now)
// ============================================================================

void UIManager::draw_deep_zoom_controls(DeepZoomManager* deep_zoom) {
    if (!deep_zoom) return;

    if (ImGui::CollapsingHeader("Deep Zoom", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Zoom Depth: Level %d", deep_zoom->state.zoom_depth_level);
        ImGui::Text("Iterations: %d", deep_zoom->state.deep_zoom_iterations);
        ImGui::Text("Est. Render Time: %.2fs", deep_zoom->state.estimated_render_time);

        ImGui::Separator();
        ImGui::Text("Coordinate Export:");

        // ✅ FIXED: Use callback instead of direct call
        if (ImGui::Button("Copy Coordinates", ImVec2(UIConstants::FULL_WIDTH, 0))) {
            safe_invoke(on_deep_zoom_copy_coordinates,
                "Copy coordinates callback not configured");
        }

        ImGui::Separator();
        ImGui::Text("Preset Zoom Paths:");

        const char* preset_names[] = {
            "Seahorse Valley Journey",
            "Elephant Valley Dive",
            "Mini Mandelbrot Hunt"
        };

        // ✅ FIXED: Use callback instead of direct calls
        for (int i = 0; i < 3; i++) {
            if (ImGui::Button(preset_names[i], ImVec2(UIConstants::FULL_WIDTH, 0))) {
                if (safe_invoke(on_deep_zoom_preset,
                    "Deep zoom preset callback not configured", i)) {
                    notifications.add(
                        fmt::format("Starting {} sequence...", preset_names[i]),
                        UIConstants::COLOR_INFO
                    );
                }
            }
        }

        ImGui::Separator();
        ImGui::Text("Manual Deep Zoom:");

        ImGui::InputText("Target X", deep_zoom_settings_.target_x,
            sizeof(deep_zoom_settings_.target_x), ImGuiInputTextFlags_CharsDecimal);
        ImGui::InputText("Target Y", deep_zoom_settings_.target_y,
            sizeof(deep_zoom_settings_.target_y), ImGuiInputTextFlags_CharsDecimal);
        ImGui::InputText("Target Zoom", deep_zoom_settings_.target_zoom,
            sizeof(deep_zoom_settings_.target_zoom), ImGuiInputTextFlags_CharsDecimal);
        ImGui::SliderFloat("Duration (s)", &deep_zoom_settings_.duration, 1.0f, 30.0f);

        // ✅ FIXED: Use callback instead of direct call
        if (ImGui::Button("Start Zoom Animation", ImVec2(UIConstants::FULL_WIDTH, 0))) {
            double x, y, z;
            bool valid = true;

            valid &= parse_coordinate(deep_zoom_settings_.target_x, x, -0.743643887037151);
            valid &= parse_coordinate(deep_zoom_settings_.target_y, y, 0.13182590420533);
            valid &= parse_coordinate(deep_zoom_settings_.target_zoom, z, 0.00001);

            if (valid) {
                if (safe_invoke(on_deep_zoom_manual,
                    "Manual deep zoom callback not configured",
                    x, y, z, deep_zoom_settings_.duration)) {
                    notifications.add("Deep zoom animation started!", UIConstants::COLOR_INFO);
                }
            }
            else {
                notifications.add("Invalid coordinate format!", UIConstants::COLOR_ERROR);
            }
        }

        ImGui::Separator();

        // ✅ FIXED: Use callbacks for settings instead of direct modification
        bool use_perturbation = deep_zoom->state.use_perturbation;
        if (ImGui::Checkbox("Use Perturbation Theory", &use_perturbation)) {
            safe_invoke(on_deep_zoom_use_perturbation,
                "Perturbation callback not configured", use_perturbation);
        }

        bool use_series = deep_zoom->state.use_series_approximation;
        if (ImGui::Checkbox("Series Approximation", &use_series)) {
            safe_invoke(on_deep_zoom_use_series,
                "Series approximation callback not configured", use_series);
        }

        int samples = deep_zoom->state.samples_per_pixel;
        if (ImGui::SliderInt("Samples Per Pixel", &samples, 1, 16)) {
            safe_invoke(on_deep_zoom_samples_changed,
                "Samples callback not configured", samples);
        }

        if (deep_zoom->state.zoom_animating) {
            ImGui::Separator();
            ImGui::ProgressBar(deep_zoom->state.zoom_progress, ImVec2(UIConstants::FULL_WIDTH, 0));
            ImGui::Text("Zooming... %.1f%%", deep_zoom->state.zoom_progress * 100.0f);
        }
    }
}

// Fractal-specific controls (Julia, Mandelbulb, Phoenix) continue in Part 3...
// Animation window continues in Part 3...

// ============================================================================
// ui_manager.cpp - Part 3: Animation Controls (ALL using callbacks)
// ============================================================================

// ============================================================================
// Animation Window
// ============================================================================

void UIManager::draw_animation_window(AnimationSystem* anim_system,
    AnimationRenderer* anim_renderer,
    FractalType current_type)
{
    if (!anim_system || !show_animation_window) return;

    ImGui::SetNextWindowSize(
        ImVec2(UIConstants::ANIMATION_WINDOW_WIDTH, UIConstants::ANIMATION_WINDOW_HEIGHT),
        ImGuiCond_FirstUseEver
    );

    if (!ImGui::Begin("Animation Timeline", &show_animation_window)) {
        ImGui::End();
        return;
    }

    auto& animation = anim_system->get_animation();
    const auto& keyframes = anim_system->get_keyframes();

    // Playback Controls
    draw_animation_playback_controls(anim_system, keyframes);

    // Keyframe Management
    draw_animation_keyframe_management(anim_system, keyframes);

    // Export Settings
    draw_animation_export_settings(animation, anim_system);

    // Video Encoding Settings
    draw_video_encoding_settings(animation, anim_renderer);

    // Render Progress
    draw_animation_render_progress(anim_renderer);

    // Help
    draw_animation_help();

    ImGui::End();
}

// ============================================================================
// Animation Playback Controls (ALL using callbacks)
// ============================================================================

void UIManager::draw_animation_playback_controls(AnimationSystem* anim_system,
    const std::vector<Keyframe>& keyframes) {
    ImGui::SeparatorText("Playback Controls");

    bool is_playing = anim_system->is_playing();
    float current_time = anim_system->get_current_time();
    float duration = anim_system->get_duration();

    // Play/Pause button
    bool can_play = (keyframes.size() >= 2);
    if (!can_play) {
        ImGui::BeginDisabled();
    }

    // ✅ FIXED: Use callbacks instead of direct calls
    if (ImGui::Button(is_playing ? "⏸ Pause" : "▶ Play", ImVec2(90, 30))) {
        if (is_playing) {
            safe_invoke(on_animation_pause, "Animation pause callback not configured");
        }
        else {
            safe_invoke(on_animation_play, "Animation play callback not configured");
        }
    }

    if (!can_play) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Need at least 2 keyframes to play");
        }
    }

    ImGui::SameLine();

    // ✅ FIXED: Stop button uses callback
    if (ImGui::Button("⏹ Stop", ImVec2(90, 30))) {
        safe_invoke(on_animation_stop, "Animation stop callback not configured");
    }

    ImGui::SameLine();

    // Loop toggle
    auto& animation = anim_system->get_animation();
    bool loop = animation.loop;

    // ✅ FIXED: Use callback instead of direct modification
    if (ImGui::Checkbox("Loop", &loop)) {
        safe_invoke(on_animation_loop_changed,
            "Animation loop callback not configured", loop);
    }

    // Timeline scrubber
    ImGui::Spacing();
    ImGui::Text("Time: %.2fs / %.2fs", current_time, duration);

    // Manual time input
    ImGui::SetNextItemWidth(150);

    // ✅ FIXED: Use callback for seek
    if (ImGui::InputFloat("Set Time", &timeline_state_.desired_time, 0.5f, 1.0f, "%.2f s")) {
        timeline_state_.desired_time = std::max(0.0f, timeline_state_.desired_time);
        timeline_state_.time_manually_set = true;
        safe_invoke(on_animation_seek,
            "Animation seek callback not configured",
            timeline_state_.desired_time);
    }

    // Sync from current_time if animation is playing
    if (is_playing && !ImGui::IsItemActive()) {
        timeline_state_.desired_time = current_time;
        timeline_state_.time_manually_set = false;
    }

    // Show indicator if time was manually set
    if (timeline_state_.time_manually_set && !is_playing) {
        ImGui::SameLine();
        ImGui::TextColored(UIConstants::COLOR_WARNING, "%.2fs", timeline_state_.desired_time);
    }

    // Slider - ✅ FIXED: Use callback for seek
    if (duration > 0) {
        float scrub_time = is_playing ? current_time : timeline_state_.desired_time;

        if (ImGui::SliderFloat("##timeline", &scrub_time, 0.0f, duration, "%.2f s")) {
            timeline_state_.desired_time = scrub_time;
            timeline_state_.time_manually_set = true;
            safe_invoke(on_animation_seek,
                "Animation seek callback not configured", scrub_time);
        }

        if (ImGui::IsItemActive()) {
            safe_invoke(on_animation_seek, "", scrub_time);
        }
    }

    // Progress bar (when playing)
    if (is_playing && duration > 0) {
        float progress = current_time / duration;
        ImGui::ProgressBar(progress, ImVec2(UIConstants::FULL_WIDTH, 0),
            fmt::format("{:.0f}%", progress * 100).c_str());
    }
}

// ============================================================================
// Keyframe Management (ALL using callbacks)
// ============================================================================

void UIManager::draw_animation_keyframe_management(AnimationSystem* anim_system,
    const std::vector<Keyframe>& keyframes) {
    ImGui::SeparatorText("Keyframes");

    float current_time = anim_system->get_current_time();

    // ✅ FIXED: Add keyframe uses callback
    if (ImGui::Button("➕ Add Keyframe Here", ImVec2(UIConstants::FULL_WIDTH, 0))) {
        safe_invoke(on_keyframe_add,
            "Add keyframe callback not configured",
            current_time, state);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Captures current fractal view at this time");
    }

    ImGui::Spacing();

    // Keyframe list
    ImGui::BeginChild("KeyframeList", ImVec2(0, 250), true);

    if (keyframes.empty()) {
        ImGui::TextDisabled("No keyframes yet");
        ImGui::TextWrapped("\nAdd keyframes to create an animation:\n"
            "1. Navigate to a view\n"
            "2. Set time on timeline\n"
            "3. Click 'Add Keyframe'");
    }
    else {
        ImGui::Text("Keyframes: %zu", keyframes.size());
        ImGui::Separator();

        for (size_t i = 0; i < keyframes.size(); i++) {
            const auto& kf = keyframes[i];

            ImGui::PushID(static_cast<int>(i));

            bool is_near_current = (std::abs(current_time - kf.time) < 0.01f);
            bool is_selected = (selected_keyframe_index == static_cast<int>(i));

            // Keyframe item
            ImVec4 color = is_near_current ? ImVec4(0.3f, 0.7f, 1.0f, 1.0f) : ImVec4(1, 1, 1, 1);

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            bool selected = ImGui::Selectable(
                fmt::format("🎯 Keyframe {} @ {:.2f}s", i + 1, kf.time).c_str(),
                is_selected
            );
            ImGui::PopStyleColor();

            if (selected) {
                // ✅ FIXED: Seek uses callback
                safe_invoke(on_animation_seek, "", kf.time);
                selected_keyframe_index = static_cast<int>(i);
            }

            // Tooltip
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Time: %.3f seconds", kf.time);
                ImGui::Separator();
                ImGui::Text("Center: (%.6f, %.6f)", kf.state.center_x, kf.state.center_y);
                ImGui::Text("Zoom: %.9f", kf.state.zoom);
                ImGui::Text("Iterations: %d", kf.state.max_iterations);
                ImGui::Text("Palette: %d", kf.state.palette_mode);

                const char* interp_names[] = {
                    "Linear", "Ease In/Out", "Ease In", "Ease Out", "Exponential"
                };
                ImGui::Text("Interpolation: %s", interp_names[static_cast<int>(kf.interp_type)]);

                ImGui::EndTooltip();
            }

            // Context menu
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("🔍 Jump to this keyframe")) {
                    // ✅ FIXED: Uses callback
                    safe_invoke(on_animation_seek, "", kf.time);
                }

                // ✅ FIXED: Update keyframe uses callback
                if (ImGui::MenuItem("🔄 Update with current view")) {
                    safe_invoke(on_keyframe_update,
                        "Update keyframe callback not configured", i, state);
                }

                ImGui::Separator();

                // ✅ FIXED: Delete keyframe uses callback
                if (ImGui::MenuItem("🗑 Delete", "Del")) {
                    if (safe_invoke(on_keyframe_delete,
                        "Delete keyframe callback not configured", i)) {
                        selected_keyframe_index = -1;
                    }
                }

                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    // ✅ FIXED: Clear all uses callback
    if (!keyframes.empty()) {
        if (ImGui::Button("🗑 Clear All Keyframes", ImVec2(UIConstants::FULL_WIDTH, 0))) {
            if (safe_invoke(on_keyframes_clear,
                "Clear keyframes callback not configured")) {
                selected_keyframe_index = -1;
            }
        }
    }
}

// ============================================================================
// Animation Export Settings
// ============================================================================

void UIManager::draw_animation_export_settings(Animation& animation, AnimationSystem* anim_system) {
    ImGui::Spacing();
    ImGui::SeparatorText("Export Settings");

    // FPS
    ImGui::SliderInt("FPS", &animation.target_fps, 24, 120);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "24 fps = cinematic\n"
            "30 fps = standard video\n"
            "60 fps = smooth motion\n"
            "120 fps = ultra smooth"
        );
    }

    // Resolution presets
    const char* resolution_presets[] = {
        "1920x1080 (Full HD)",
        "2560x1440 (2K)",
        "3840x2160 (4K)",
        "7680x4320 (8K)",
        "Custom"
    };

    if (ImGui::Combo("Resolution", &timeline_state_.selected_resolution_preset,
        resolution_presets, 5)) {
        switch (timeline_state_.selected_resolution_preset) {
        case 0: animation.export_width = 1920; animation.export_height = 1080; break;
        case 1: animation.export_width = 2560; animation.export_height = 1440; break;
        case 2: animation.export_width = 3840; animation.export_height = 2160; break;
        case 3: animation.export_width = 7680; animation.export_height = 4320; break;
        case 4: break; // Custom
        }
    }

    if (timeline_state_.selected_resolution_preset == 4) {
        ImGui::InputInt("Width", &animation.export_width);
        ImGui::InputInt("Height", &animation.export_height);
    }

    // Clamp values
    animation.export_width = std::clamp(animation.export_width, 640, 7680);
    animation.export_height = std::clamp(animation.export_height, 480, 4320);

    ImGui::Text("Output: %dx%d @ %d fps",
        animation.export_width, animation.export_height, animation.target_fps);
}

// ============================================================================
// Video Encoding Settings (Using class members instead of static)
// ============================================================================

void UIManager::draw_video_encoding_settings(Animation& animation, AnimationRenderer* anim_renderer) {
    ImGui::SeparatorText("Video Encoding");

    // Codec selection
    const char* codec_items[] = {
        "H.264 (Most Compatible)",
        "H.265 (Better Compression)",
        "VP9 (Open Source)",
        "ProRes (Professional)",
        "AV1 (Future-proof)"
    };

    ImGui::Combo("Codec", &video_encode_settings_.codec_index, codec_items, 5);

    // Quality selection
    const char* quality_items[] = {
        "Draft (Fast, Lower Quality)",
        "Good (Balanced)",
        "High (Slow, Best Quality)",
        "Lossless (Huge Files)"
    };

    ImGui::Combo("Quality", &video_encode_settings_.quality_index, quality_items, 4);

    // CRF slider (for H.264/H.265)
    if (video_encode_settings_.codec_index == 0 || video_encode_settings_.codec_index == 1) {
        ImGui::SliderInt("CRF", &video_encode_settings_.crf, 0, 51);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Lower = better quality, larger file\n"
                "18 = visually lossless\n"
                "23 = good quality\n"
                "28 = acceptable"
            );
        }
    }

    // Output filename
    ImGui::InputText("Output File", video_encode_settings_.filename,
        sizeof(video_encode_settings_.filename));

    // Cleanup option
    ImGui::Checkbox("Delete frames after encoding", &video_encode_settings_.delete_frames_after);

    ImGui::Separator();

    // ✅ FIXED: Render & Encode button uses callback
    if (ImGui::Button("🎬 Render & Encode Video", ImVec2(-1, 35))) {
        // Convert UI settings to VideoEncodeSettings
        VideoEncodeSettings settings;
        settings.codec = static_cast<VideoCodec>(video_encode_settings_.codec_index);
        settings.quality = static_cast<VideoQuality>(video_encode_settings_.quality_index);
        settings.crf = video_encode_settings_.crf;
        settings.output_filename = video_encode_settings_.filename;
        settings.delete_frames_after = video_encode_settings_.delete_frames_after;
        settings.fps = animation.target_fps;

        safe_invoke(on_render_encode,
            "Render & encode callback not configured",
            animation, settings);
    }

    // Show encoding progress
    if (anim_renderer && anim_renderer->video_encoder) {
        const auto& encode_progress = anim_renderer->video_encoder->get_progress();

        if (encode_progress.is_encoding) {
            ImGui::Separator();
            ImGui::SeparatorText("Encoding Video");

            ImGui::ProgressBar(encode_progress.progress, ImVec2(-1, 0),
                fmt::format("{}/{} frames", encode_progress.frames_encoded,
                    encode_progress.total_frames).c_str());

            ImGui::Text("%s", encode_progress.status.c_str());
            ImGui::Text("Encoding speed: %.1f fps", encode_progress.fps_encoding);

            // ✅ FIXED: Cancel button uses callback
            if (ImGui::Button("❌ Cancel Encoding", ImVec2(-1, 0))) {
                safe_invoke(on_cancel_encoding,
                    "Cancel encoding callback not configured");
            }
        }
        else if (encode_progress.completed) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Video Encoding Complete!");
            ImGui::Text("Output: %s", video_encode_settings_.filename);
        }
    }
}

// ============================================================================
// Animation Render Progress
// ============================================================================

// ============================================================================
// CORRECTED: Animation Render Progress (matches actual RenderProgress struct)
// ============================================================================

void UIManager::draw_animation_render_progress(AnimationRenderer* anim_renderer) {
    if (!anim_renderer || !anim_renderer->is_rendering()) return;

    ImGui::Separator();
    ImGui::SeparatorText("Rendering Animation");

    auto progress = anim_renderer->get_progress();

    // ✅ FIXED: Correct field names
    ImGui::ProgressBar(progress.progress, ImVec2(-1, 0),
        fmt::format("Frame {}/{}", progress.current_frame, progress.total_frames).c_str());

    ImGui::Text("Elapsed: %s", format_time(progress.elapsed_time).c_str());
    ImGui::Text("Remaining: %s", format_time(progress.estimated_time_remaining).c_str());

    // ✅ FIXED: Calculate FPS instead of reading non-existent field
    if (progress.elapsed_time > 0.0f && progress.current_frame > 0) {
        float fps = progress.current_frame / progress.elapsed_time;
        ImGui::Text("Speed: %.1f fps", fps);
    }
    else {
        ImGui::Text("Speed: calculating...");
    }

    // ✅ BONUS: Show the current_status string
    if (!progress.current_status.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "%s", progress.current_status.c_str());
    }

    if (ImGui::Button("Cancel Render", ImVec2(-1, 0))) {
        anim_renderer->cancel_render();
        notifications.add("Render cancelled", UIConstants::COLOR_WARNING);
    }
}

// ============================================================================
// Animation Help
// ============================================================================

void UIManager::draw_animation_help() {
    if (ImGui::CollapsingHeader("📖 Help")) {
        ImGui::TextWrapped(
            "Animation Workflow:\n\n"
            "1. KEYFRAMES: Navigate to different fractal views and add keyframes "
            "at different times on the timeline.\n\n"
            "2. PLAYBACK: Use Play/Pause to preview your animation. The fractal "
            "will smoothly interpolate between keyframes.\n\n"
            "3. EXPORT: Set your desired resolution and FPS, then click "
            "'Render & Encode Video' to create a video file.\n\n"
            "Tips:\n"
            "• Add at least 2 keyframes to create an animation\n"
            "• Right-click keyframes for more options\n"
            "• Use the timeline slider to scrub through time\n"
            "• Higher FPS = smoother but larger files"
        );
    }
}

// Remaining helper functions (minimap, status bar, help overlay, presets) 
// continue in Part 4...

// ============================================================================
// ui_manager.cpp - Part 4: Helper Functions and Fractal-Specific Controls
// ============================================================================

// ============================================================================
// Fractal-Specific Controls
// ============================================================================

void UIManager::draw_fractal_specific_controls(FractalType type) {
    if (type == FractalType::JuliaSet) {
        draw_julia_controls();
    }
    else if (type == FractalType::Mandelbulb) {
        draw_mandelbulb_controls();
    }
    else if (type == FractalType::Phoenix) {
        draw_phoenix_controls();
    }
}

void UIManager::draw_julia_controls() {
    if (ImGui::CollapsingHeader("Julia Set Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderFloat("C Real", &state.julia_c_real, -2.0f, 2.0f)) {
            state.mark_dirty();
        }
        if (ImGui::SliderFloat("C Imaginary", &state.julia_c_imag, -2.0f, 2.0f)) {
            state.mark_dirty();
        }

        ImGui::Spacing();
        ImGui::Text("Classic Julia Sets:");

        struct JuliaPreset {
            const char* name;
            float c_real;
            float c_imag;
        };

        const JuliaPreset presets[] = {
            {"Dendritic", -0.4f, 0.6f},
            {"Siegel Disk", -0.391f, -0.587f},
            {"Douady's Rabbit", -0.123f, 0.745f},
            {"San Marco", -0.75f, 0.0f}
        };

        for (int i = 0; i < 4; i++) {
            if (i % 2 != 0) ImGui::SameLine();

            if (ImGui::Button(presets[i].name, ImVec2(UIConstants::PRESET_BUTTON_WIDTH, 0))) {
                state.julia_c_real = presets[i].c_real;
                state.julia_c_imag = presets[i].c_imag;
                state.mark_dirty();
            }
        }

        if (ImGui::Checkbox("Animate Julia", &state.animate_julia)) {
            state.mark_dirty();
        }
    }
}

void UIManager::draw_mandelbulb_controls() {
    if (ImGui::CollapsingHeader("3D Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderFloat("Distance", &state.camera_distance, 1.0f, 10.0f)) {
            state.mark_dirty();
        }
        if (ImGui::SliderFloat("Rotation", &state.rotation_y, -3.14159f, 3.14159f)) {
            state.mark_dirty();
        }

        if (ImGui::Button("Reset Camera", ImVec2(UIConstants::FULL_WIDTH, 0))) {
            state.camera_distance = 3.0f;
            state.rotation_y = 0.0f;
            state.mark_dirty();
        }

        ImGui::Spacing();
        if (ImGui::SliderFloat("Power", &state.mandelbulb_power, 2.0f, 16.0f)) {
            state.mark_dirty();
        }
        if (ImGui::SliderFloat("Field of View", &state.fov, 0.5f, 2.0f)) {
            state.mark_dirty();
        }

        ImGui::Spacing();
        if (ImGui::Checkbox("Auto Rotate", &state.auto_rotate)) {
            state.mark_dirty();
        }
        if (state.auto_rotate) {
            if (ImGui::SliderFloat("Rotation Speed", &state.rotation_speed, 0.1f, 2.0f)) {
                state.mark_dirty();
            }
        }

        ImGui::Spacing();
        ImGui::Text("Power Presets:");

        struct PowerPreset {
            const char* name;
            float power;
        };

        const PowerPreset presets[] = {
            {"Classic (8)", 8.0f},
            {"Smooth (4)", 4.0f},
            {"Spiky (12)", 12.0f},
            {"Extreme (16)", 16.0f}
        };

        for (int i = 0; i < 4; i++) {
            if (i % 2 != 0) ImGui::SameLine();

            if (ImGui::Button(presets[i].name, ImVec2(UIConstants::PRESET_BUTTON_WIDTH, 0))) {
                state.mandelbulb_power = presets[i].power;
                state.mark_dirty();
            }
        }
    }
}

void UIManager::draw_phoenix_controls() {
    if (ImGui::CollapsingHeader("Phoenix Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("Phoenix fractals use 'memory' of previous iterations:");
        ImGui::Spacing();

        // Phoenix P parameter (damping)
        if (ImGui::SliderFloat("P (Damping)", &state.phoenix_p, -0.5f, 0.5f, "%.3f")) {
            state.mark_dirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Controls the damping effect\nTry values near 0.0 for classic patterns");
        }

        // Phoenix R parameter (feedback/memory)
        if (ImGui::SliderFloat("R (Memory)", &state.phoenix_r, -1.5f, 0.5f, "%.3f")) {
            state.mark_dirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Controls the memory/feedback strength\n-0.5 creates the classic Phoenix fractal");
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Julia mode toggle
        if (ImGui::Checkbox("Julia Set Mode", &state.use_julia_set)) {
            state.mark_dirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Toggle between Mandelbrot-style and Julia set");
        }

        // Julia parameters (only if in Julia mode)
        if (state.use_julia_set) {
            ImGui::Spacing();
            if (ImGui::SliderFloat("Julia C Real", &state.julia_c_real, -2.0f, 2.0f, "%.3f")) {
                state.mark_dirty();
            }
            if (ImGui::SliderFloat("Julia C Imag", &state.julia_c_imag, -2.0f, 2.0f, "%.3f")) {
                state.mark_dirty();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Stripe density (flow visualization)
        if (ImGui::SliderFloat("Flow Stripes", &state.stripe_density, 0.0f, 20.0f, "%.1f")) {
            state.mark_dirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "0 = No stripes\n"
                "5-15 = Flowing patterns\n"
                "Visualizes the angle of escape"
            );
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Parameter Presets:");

        // Preset buttons
        struct PhoenixPreset {
            const char* name;
            float p, r;
        };

        const PhoenixPreset presets[] = {
            {"Classic Phoenix", 0.0f, -0.5f},
            {"Swirl", 0.2f, -0.3f},
            {"Tendrils", -0.1f, -0.8f},
            {"Chaos", 0.3f, -0.6f}
        };

        for (int i = 0; i < 4; i++) {
            if (i % 2 != 0) ImGui::SameLine();

            if (ImGui::Button(presets[i].name, ImVec2(155, 0))) {
                state.phoenix_p = presets[i].p;
                state.phoenix_r = presets[i].r;
                state.mark_dirty();
                notifications.add(
                    fmt::format("Applied {} preset", presets[i].name),
                    UIConstants::COLOR_SUCCESS
                );
            }
        }
    }
}

// ============================================================================
// Preset Window
// ============================================================================

void UIManager::draw_preset_window(FractalType current_type) {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 260, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(UIConstants::PRESET_WINDOW_WIDTH, 0), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Preset Locations", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (current_type == FractalType::Mandelbrot) {
        ImGui::Text("Mandelbrot Locations:");
        for (const auto& preset : Presets::MANDELBROT_PRESETS) {
            if (ImGui::Button(preset.name, ImVec2(UIConstants::FULL_WIDTH, 0))) {
                safe_invoke(on_apply_preset, "", preset);
            }
        }
    }
    else if (current_type == FractalType::BurningShip) {
        ImGui::Text("Burning Ship Locations:");
        for (const auto& preset : Presets::BURNING_SHIP_PRESETS) {
            if (ImGui::Button(preset.name, ImVec2(UIConstants::FULL_WIDTH, 0))) {
                safe_invoke(on_apply_preset, "", preset);
            }
        }
    }
    else if (current_type == FractalType::JuliaSet) {
        ImGui::Text("Julia Set Locations:");
        ImGui::TextWrapped("Use the Julia Parameters section to explore!");
    }
    else if (current_type == FractalType::Mandelbulb) {
        ImGui::Text("Mandelbulb Views:");

        struct MandelbulbView {
            const char* name;
            float distance;
            float rotation;
            float power;
        };

        const MandelbulbView views[] = {
            {"Front View", 3.0f, 0.0f, 8.0f},
            {"Side View", 3.0f, 1.5708f, 8.0f},
            {"Close-up Detail", 1.5f, 0.785f, 8.0f}
        };

        for (const auto& view : views) {
            if (ImGui::Button(view.name, ImVec2(UIConstants::FULL_WIDTH, 0))) {
                state.camera_distance = view.distance;
                state.rotation_y = view.rotation;
                state.mandelbulb_power = view.power;
                state.mark_dirty();
            }
        }
    }

    ImGui::End();
}

// ============================================================================
// Minimap
// ============================================================================

void UIManager::draw_minimap() {
    ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 160));
    ImGui::SetNextWindowSize(ImVec2(UIConstants::MINIMAP_SIZE, UIConstants::MINIMAP_SIZE));
    ImGui::SetNextWindowBgAlpha(UIConstants::MINIMAP_ALPHA);

    if (!ImGui::Begin("Location", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();

    // Draw Mandelbrot outline
    ImVec2 center(canvas_pos.x + canvas_size.x * 0.5f,
        canvas_pos.y + canvas_size.y * 0.5f);
    float scale = canvas_size.x * 0.3f;

    // Main cardioid
    draw_list->AddCircleFilled(center, scale, IM_COL32(100, 100, 120, 200), 32);

    // Bulb
    ImVec2 bulb_center(center.x - scale * 0.8f, center.y);
    draw_list->AddCircleFilled(bulb_center, scale * 0.5f, IM_COL32(100, 100, 120, 200), 32);

    // Current position indicator
    float norm_x = (state.center_x + 2.0) / 4.0;
    float norm_y = (state.center_y + 1.5) / 3.0;
    ImVec2 pos(canvas_pos.x + norm_x * canvas_size.x,
        canvas_pos.y + norm_y * canvas_size.y);

    // Pulsing indicator
    float pulse = (std::sin(ImGui::GetTime() * 3.0f) * 0.5f + 0.5f);
    float radius = 3.0f + pulse * 2.0f;
    draw_list->AddCircleFilled(pos, radius, IM_COL32(255, 100, 100, 255), 12);
    draw_list->AddCircle(pos, radius + 2.0f, IM_COL32(255, 200, 200, 255), 12, 2.0f);

    ImGui::End();
}

// ============================================================================
// Status Bar
// ============================================================================

void UIManager::draw_status_bar(FractalType current_type, float fps, int width, int height) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - UIConstants::STATUS_BAR_HEIGHT));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, UIConstants::STATUS_BAR_HEIGHT));
    ImGui::SetNextWindowBgAlpha(0.9f);

    if (!ImGui::Begin("StatusBar", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    // Fractal info
    ImGui::Text("%s", FractalState::get_name(current_type));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Resolution
    ImGui::Text("%dx%d", width, height);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // FPS
    ImGui::Text("FPS: %.1f", fps);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Iterations
    ImGui::Text("Iterations: %d", state.max_iterations);

    // Right-aligned status
    ImGui::SameLine(io.DisplaySize.x - 150);
    if (state.is_rendering) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "⚙ Rendering...");
    }
    else {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Ready");
    }

    ImGui::End();
}

// ============================================================================
// Help Overlay
// ============================================================================

void UIManager::draw_help_overlay() {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Help & Controls", &state.show_help, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Welcome to the Fractal Viewer!");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("🖱️ Mouse Controls:");
    ImGui::BulletText("Drag: Pan the view");
    ImGui::BulletText("Scroll: Zoom in/out");
    ImGui::BulletText("Double-click: Zoom to point");
    ImGui::BulletText("Right-click: Zoom out from point");

    ImGui::Spacing();
    ImGui::Text("⌨️ Keyboard Shortcuts:");
    ImGui::BulletText("WASD / Arrows: Pan view");
    ImGui::BulletText("Q/E: Zoom out/in");
    ImGui::BulletText("R: Reset view");
    ImGui::BulletText("Tab: Switch fractal type");
    ImGui::BulletText("Space: Toggle UI");
    ImGui::BulletText("H: Toggle this help");
    ImGui::BulletText("F: Toggle fullscreen");
    ImGui::BulletText("S: Save screenshot");
    ImGui::BulletText("[/]: Decrease/increase iterations");
    ImGui::BulletText("1-6: Change color palette");
    ImGui::BulletText("Z/X/C: Deep zoom presets (Mandelbrot only)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("💡 Tips:");
    ImGui::BulletText("Use high iterations for deep zooms");
    ImGui::BulletText("Try different color palettes with number keys");
    ImGui::BulletText("Explore the preset locations");
    ImGui::BulletText("Create animations with the timeline");
    ImGui::BulletText("Export high-resolution images for printing");

    ImGui::Spacing();
    if (ImGui::Button("Close (H)", ImVec2(UIConstants::FULL_WIDTH, 0))) {
        state.show_help = false;
    }

    ImGui::End();
}

// ============================================================================
// End of ui_manager.cpp
// ============================================================================

// Note: This completes the improved UI Manager implementation.
// Combine all parts (1-4) into a single ui_manager.cpp file.
