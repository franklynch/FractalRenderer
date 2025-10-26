#pragma once
#include "fractal_state.h"

#include <imgui.h>
#include <vector>
#include <string>
#include <functional>
#include <fmt/core.h>

// Forward declarations
struct Animation;
class DeepZoomManager;
class AnimationSystem;
class AnimationRenderer;
struct Keyframe;
struct VideoEncodeSettings;

// ============================================================================
// Notification System
// ============================================================================

class NotificationSystem {
public:
    void add(const std::string& message, ImVec4 color = ImVec4(1, 1, 1, 1));
    void update(float deltaTime);
    void draw();

private:
    struct Notification {
        std::string message;
        float time_remaining;
        ImVec4 color;
    };

    static constexpr float NOTIFICATION_DURATION = 3.0f;
    static constexpr size_t MAX_NOTIFICATIONS = 10;
    std::vector<Notification> notifications;
};

// ============================================================================
// Export Settings (moved from static variables)
// ============================================================================

struct ExportSettings {
    int preset = 0;
    int width = 7200;
    int height = 10800;
    bool supersample = true;
};

struct VideoEncodeSettingsUI {
    int codec_index = 0;
    int quality_index = 1;
    int crf = 23;
    char filename[256] = "fractal_animation.mp4";
    bool delete_frames_after = false;
};

struct DeepZoomManualSettings {
    char target_x[64] = "-0.743643887037151";
    char target_y[64] = "0.13182590420533";
    char target_zoom[64] = "0.00001";
    float duration = 5.0f;
};

struct AnimationTimelineState {
    float desired_time = 0.0f;
    bool time_manually_set = false;
    int selected_resolution_preset = 0;
};

// ============================================================================
// UI Manager - Handles all ImGui rendering
// ============================================================================

class UIManager {
public:
    explicit UIManager(FractalState& state);
    ~UIManager() = default;

    // Delete copy/move to prevent accidental copies
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    // Theme setup
    void apply_theme();

    // Main draw function
    void draw_all(FractalType current_type, float fps,
        DeepZoomManager* deep_zoom,
        AnimationSystem* anim_system,
        AnimationRenderer* anim_renderer);

    // State accessor
    const FractalState& get_state() const { return state; }

    // Notification system (public for callbacks to use)
    NotificationSystem notifications;

    // ========================================================================
    // Callbacks - All UI actions go through these (24 total)
    // ========================================================================

    // Basic view controls
    std::function<void(const Preset&)> on_apply_preset;
    std::function<void()> on_reset_view;
    std::function<void(bool zoom_in)> on_zoom;
    std::function<void()> on_save_screenshot;
    std::function<void()> on_toggle_fullscreen;
    std::function<void(uint32_t width, uint32_t height, bool supersample)> on_export_print;
    std::function<void(FractalType)> on_fractal_type_changed;

    // Deep zoom callbacks
    std::function<void(int preset_index)> on_deep_zoom_preset;
    std::function<void(double x, double y, double zoom, float duration)> on_deep_zoom_manual;
    std::function<void()> on_deep_zoom_copy_coordinates;
    std::function<void(bool enabled)> on_deep_zoom_use_perturbation;
    std::function<void(bool enabled)> on_deep_zoom_use_series;
    std::function<void(int samples)> on_deep_zoom_samples_changed;

    // Animation playback callbacks
    std::function<void()> on_animation_play;
    std::function<void()> on_animation_pause;
    std::function<void()> on_animation_stop;
    std::function<void(float time)> on_animation_seek;
    std::function<void(bool loop)> on_animation_loop_changed;

    // Keyframe management callbacks
    std::function<void(float time, const FractalState&)> on_keyframe_add;
    std::function<void(size_t index, const FractalState&)> on_keyframe_update;
    std::function<void(size_t index)> on_keyframe_delete;
    std::function<void()> on_keyframes_clear;

    // Animation export callbacks
    std::function<void(const Animation&)> on_export_animation;
    std::function<void(const Animation&, const VideoEncodeSettings&)> on_render_encode;
    std::function<void()> on_cancel_render;
    std::function<void()> on_cancel_encoding;

private:
    // Reference to fractal state
    FractalState& state;

    // UI state
    bool show_animation_window = false;
    int selected_keyframe_index = -1;

    // Persistent settings (moved from static variables)
    ExportSettings export_settings_;
    VideoEncodeSettingsUI video_encode_settings_;
    DeepZoomManualSettings deep_zoom_settings_;
    AnimationTimelineState timeline_state_;

    // ========================================================================
    // Main window sections
    // ========================================================================
    void draw_main_controls(FractalType& current_type, DeepZoomManager* deep_zoom);
    void draw_fractal_type_selector(FractalType& current_type);
    void draw_view_controls();
    void draw_rendering_settings();
    void draw_color_palette();
    void draw_advanced_effects();
    void draw_performance_info();
    void draw_high_res_export();

    // ========================================================================
    // Fractal-specific controls
    // ========================================================================
    void draw_fractal_specific_controls(FractalType type);
    void draw_julia_controls();
    void draw_mandelbulb_controls();
    void draw_phoenix_controls();

    // ========================================================================
    // Other windows
    // ========================================================================
    void draw_preset_window(FractalType current_type);
    void draw_minimap();
    void draw_status_bar(FractalType current_type, float fps, int width, int height);
    void draw_help_overlay();

    // ========================================================================
    // Deep zoom
    // ========================================================================
    void draw_deep_zoom_controls(DeepZoomManager* deep_zoom);

    // ========================================================================
    // Animation window sections
    // ========================================================================
    void draw_animation_window(AnimationSystem* anim_system,
        AnimationRenderer* anim_renderer,
        FractalType current_type);

    void draw_animation_playback_controls(AnimationSystem* anim_system,
        const std::vector<Keyframe>& keyframes);

    void draw_animation_keyframe_management(AnimationSystem* anim_system,
        const std::vector<Keyframe>& keyframes);

    void draw_animation_export_settings(Animation& animation, AnimationSystem* anim_system);

    void draw_animation_render_progress(AnimationRenderer* anim_renderer);

    void draw_animation_help();

    void draw_video_encoding_settings(Animation& animation, AnimationRenderer* anim_renderer);

    // ========================================================================
    // Helper methods
    // ========================================================================

    /**
     * @brief Safely invoke a callback with error handling
     * @tparam Func Callback type
     * @tparam Args Argument types
     * @param callback The callback to invoke
     * @param error_msg Error message to show if callback is null
     * @param args Arguments to pass to callback
     * @return true if callback was invoked successfully, false otherwise
     */
    template<typename Func, typename... Args>
    bool safe_invoke(Func& callback, const char* error_msg, Args&&... args) {
        if (!callback) {
            if (error_msg && error_msg[0] != '\0') {
                notifications.add(error_msg, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            }
            return false;
        }

        try {
            callback(std::forward<Args>(args)...);
            return true;
        }
        catch (const std::exception& e) {
            notifications.add(
                fmt::format("Error: {}", e.what()),
                ImVec4(1.0f, 0.0f, 0.0f, 1.0f)
            );
            return false;
        }
    }
};
