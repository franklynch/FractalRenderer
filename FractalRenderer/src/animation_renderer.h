#pragma once

#include "animation_system.h"
#include "video_encoder.h"
#include "fractal_state.h"
#include <vk_types.h>
#include <functional>
#include <string>
#include <atomic>
#include <thread>

enum class RenderStatus {
    Idle,
    Rendering,
    Completed,
    Cancelled,
    Error
};

struct RenderProgress {
    int current_frame = 0;
    int total_frames = 0;
    float progress = 0.0f;  // 0.0 to 1.0
    std::string current_status;
    RenderStatus status = RenderStatus::Idle;

    float elapsed_time = 0.0f;
    float estimated_time_remaining = 0.0f;
};

class AnimationRenderer {
public:
    AnimationRenderer(
        VkDevice device,
        VmaAllocator allocator,
        VkDescriptorSetLayout descriptorLayout
    );

    ~AnimationRenderer();

    using RenderFrameCallback = std::function<bool(
        const FractalState&,
        uint32_t width,
        uint32_t height,
        const std::string& path
        )>;

    RenderFrameCallback render_frame_callback;

    std::unique_ptr<VideoEncoder> video_encoder;
    VideoEncodeSettings encode_settings;

    // Start rendering animation
    void start_render(
        const Animation& animation,
        AnimationSystem* anim_system,
        const std::string& output_folder = "animation_frames"
    );

    void render_and_encode(
        const Animation& animation,
        AnimationSystem* anim_system,
        const VideoEncodeSettings& video_settings,
        const std::string& output_folder = "animation_frames"
    );

    // Cancel ongoing render
    void cancel_render();

    // Update (call every frame to check progress)
    void update(float delta_time);

    // Get current progress
    const RenderProgress& get_progress() const { return progress; }

    // Check if currently rendering
    bool is_rendering() const { return progress.status == RenderStatus::Rendering; }

    // Callbacks
    std::function<void()> on_render_complete;
    std::function<void(const std::string&)> on_render_error;
    std::function<void(int, int)> on_frame_complete;  // frame_number, total_frames

private:
    VkDevice device;
    VmaAllocator allocator;
    VkDescriptorSetLayout descriptorLayout;

    RenderProgress progress;
    std::atomic<bool> cancel_requested{ false };

    // Render a single frame
    bool render_frame(
        int frame_number,
        float time,
        const FractalState& state,
        uint32_t width,
        uint32_t height,
        const std::string& output_path
    );

    // Helper to create offscreen render target
    AllocatedImage create_render_target(uint32_t width, uint32_t height);
    void destroy_render_target(const AllocatedImage& image);

    // Helper to save frame to disk
    bool save_frame_to_png(
        const AllocatedImage& image,
        uint32_t width,
        uint32_t height,
        const std::string& filename
    );
};
