#include "animation_renderer.h"
#include <vk_initializers.h>
#include <vk_images.h>
#include <fmt/core.h>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

// #define STB_IMAGE_WRITE_IMPLEMENTATION
#define _CRT_SECURE_NO_WARNINGS
#include "stb_image_write.h"    

AnimationRenderer::AnimationRenderer(
    VkDevice device, VmaAllocator allocator, VkDescriptorSetLayout descriptorLayout) : device(device), allocator(allocator), descriptorLayout(descriptorLayout) 
{

    video_encoder = std::make_unique<VideoEncoder>();
}

AnimationRenderer::~AnimationRenderer() {
    cancel_render();
}

void AnimationRenderer::start_render(
    const Animation& animation,
    AnimationSystem* anim_system,
    const std::string& output_folder
) {
    if (is_rendering()) {
        fmt::print("Already rendering!\n");
        return;
    }

    // Validate animation
    if (animation.keyframes.size() < 2) {
        fmt::print("Need at least 2 keyframes to render!\n");
        if (on_render_error) {
            on_render_error("Need at least 2 keyframes to render");
        }
        return;
    }

    // Reset progress
    progress = RenderProgress();
    progress.status = RenderStatus::Rendering;
    progress.total_frames = static_cast<int>(animation.duration * animation.target_fps);
    progress.current_status = "Initializing...";
    cancel_requested = false;

    fmt::print("\n=== ANIMATION RENDER STARTED ===\n");
    fmt::print("Animation: {}\n", animation.name);
    fmt::print("Duration: {:.2f}s @ {} FPS = {} frames\n",
        animation.duration, animation.target_fps, progress.total_frames);
    fmt::print("Resolution: {}x{}\n", animation.export_width, animation.export_height);
    fmt::print("Output folder: {}\n\n", output_folder);

    // Create output directory
    try {
        std::filesystem::create_directories(output_folder);
    }
    catch (const std::exception& e) {
        fmt::print("Failed to create output directory: {}\n", e.what());
        progress.status = RenderStatus::Error;
        if (on_render_error) {
            on_render_error("Failed to create output directory");
        }
        return;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Render each frame
    for (int frame = 0; frame < progress.total_frames && !cancel_requested; frame++) {
        progress.current_frame = frame;
        progress.progress = static_cast<float>(frame) / progress.total_frames;

        // Calculate time for this frame
        float time = (frame / static_cast<float>(animation.target_fps));

        // Get interpolated state
        FractalState frame_state = anim_system->interpolate(time);

        // Generate filename
        std::ostringstream filename;
        filename << output_folder << "/frame_"
            << std::setfill('0') << std::setw(6) << frame << ".png";

        progress.current_status = fmt::format("Rendering frame {}/{}", frame + 1, progress.total_frames);

        fmt::print("\rRendering frame {}/{} ({:.1f}%) - Time: {:.2f}s",
            frame + 1, progress.total_frames, progress.progress * 100.0f, time);
        std::cout.flush();

        // Render the frame
        bool success = render_frame(
            frame,
            time,
            frame_state,
            animation.export_width,
            animation.export_height,
            filename.str()
        );

        if (!success) {
            fmt::print("\nFailed to render frame {}\n", frame);
            progress.status = RenderStatus::Error;
            if (on_render_error) {
                on_render_error(fmt::format("Failed to render frame {}", frame));
            }
            return;
        }

        // Calculate time estimates
        auto current_time = std::chrono::high_resolution_clock::now();
        progress.elapsed_time = std::chrono::duration<float>(current_time - start_time).count();

        float avg_time_per_frame = progress.elapsed_time / (frame + 1);
        int frames_remaining = progress.total_frames - (frame + 1);
        progress.estimated_time_remaining = avg_time_per_frame * frames_remaining;

        // Callback
        if (on_frame_complete) {
            on_frame_complete(frame, progress.total_frames);
        }
    }

    fmt::print("\n");

    // Finalize
    if (cancel_requested) {
        fmt::print("Render cancelled by user\n");
        progress.status = RenderStatus::Cancelled;
        progress.current_status = "Cancelled";
    }
    else {
        fmt::print("=== RENDER COMPLETE ===\n");
        fmt::print("Total time: {:.2f} seconds\n", progress.elapsed_time);
        fmt::print("Average: {:.2f} ms/frame\n", (progress.elapsed_time * 1000.0f) / progress.total_frames);
        fmt::print("Output: {}\n\n", output_folder);

        progress.status = RenderStatus::Completed;
        progress.current_status = "Complete!";
        progress.progress = 1.0f;

        if (on_render_complete) {
            on_render_complete();
        }
    }
    
}


void AnimationRenderer::render_and_encode(
    const Animation& animation,
    AnimationSystem* anim_system,
    const VideoEncodeSettings& video_settings,
    const std::string& output_folder
) {
    // Step 1: Render all frames
    start_render(animation, anim_system, output_folder);

    // Check if render completed successfully
    if (progress.status != RenderStatus::Completed) {
        return;  // Render failed or was cancelled
    }

    // Step 2: Encode video
    fmt::print("\n=== Starting video encoding ===\n");

    video_encoder->on_progress = [this](int frames_done, int total) {
        // Update UI if needed
        };

    video_encoder->on_complete = [this](const std::string& filename) {
        if (on_render_complete) {
            on_render_complete();
        }
        };

    video_encoder->on_error = [this](const std::string& error) {
        if (on_render_error) {
            on_render_error(error);
        }
        };

    video_encoder->encode(output_folder, video_settings);
}

void AnimationRenderer::cancel_render() {
    if (is_rendering()) {
        cancel_requested = true;
        fmt::print("Cancelling render...\n");
    }
}

void AnimationRenderer::update(float delta_time) {
    // Rendering is synchronous, but we can use this for UI updates
}

bool AnimationRenderer::render_frame(
    int frame_number,
    float time,
    const FractalState& state,
    uint32_t width,
    uint32_t height,
    const std::string& output_path
)
{
    if (!render_frame_callback) {
        fmt::print("Error: No render callback set!\n");
        return false;
    }

    return render_frame_callback(state, width, height, output_path);
}

AllocatedImage AnimationRenderer::create_render_target(uint32_t width, uint32_t height) {
    AllocatedImage image;
    image.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    image.imageExtent = { width, height, 1 };

    VkImageUsageFlags usages = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo img_info = vkinit::image_create_info(
        image.imageFormat, usages, image.imageExtent
    );

    VmaAllocationCreateInfo img_allocinfo = {};
    img_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    img_allocinfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    vmaCreateImage(allocator, &img_info, &img_allocinfo,
        &image.image, &image.allocation, nullptr);

    VkImageViewCreateInfo view_info = vkinit::imageview_create_info(
        image.imageFormat, image.image, VK_IMAGE_ASPECT_COLOR_BIT
    );
    vkCreateImageView(device, &view_info, nullptr, &image.imageView);

    return image;
}

void AnimationRenderer::destroy_render_target(const AllocatedImage& image) {
    vkDestroyImageView(device, image.imageView, nullptr);
    vmaDestroyImage(allocator, image.image, image.allocation);
}

bool AnimationRenderer::save_frame_to_png(
    const AllocatedImage& image,
    uint32_t width,
    uint32_t height,
    const std::string& filename
) {
    // This will be implemented with the actual frame data
    return false;
}