#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "fractal_state.h"
#include <array>
#include <cstring>  // for memcmp
#include <fmt/core.h>


// Aligned push constants structure (80 bytes total)
struct alignas(16) ComputePushConstants {
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
    glm::vec4 data5;  // 80 bytes total
};

// Single compute effect with cached state
class ComputeEffect {
public:
    FractalType type = FractalType::Mandelbrot;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    ComputePushConstants push_constants = {};

    bool uses_deep_zoom_layout = false;

    // Cache for dirty checking
    ComputePushConstants cached_constants = {};

    bool is_dirty = true;

    bool needs_update() const { return is_dirty; }
    void mark_clean() { cached_constants = push_constants; is_dirty = false; }

    void update_from_state(const FractalState& state, float time);
};

// Manages all compute effects
class ComputeEffectManager {
public:
    ComputeEffectManager(VkDevice device,
        VkDescriptorSetLayout regular_layout,
        VkDescriptorSetLayout deep_zoom_layout);

    ~ComputeEffectManager();

    void init_pipelines();
    void cleanup();

    ComputeEffect* get_effect(FractalType type);

    // Optimized dispatch with dirty checking
    void dispatch(VkCommandBuffer cmd, FractalType type,
        const FractalState& state, float time,
        VkDescriptorSet desc_set, VkExtent2D extent);

private:
    VkDevice device;
    // Regular descriptor layout (image only)
    VkDescriptorSetLayout regular_descriptor_layout;
    VkPipelineLayout regular_pipeline_layout = VK_NULL_HANDLE;

    // Deep zoom descriptor layout (image + buffer)
    VkDescriptorSetLayout deep_zoom_descriptor_layout;
    VkPipelineLayout deep_zoom_pipeline_layout = VK_NULL_HANDLE;

   // std::array<ComputeEffect, 4> effects;  // 4 fractal types

    std::array<ComputeEffect, static_cast<size_t>(FractalType::Count)> effects{};

    // Pipeline creation helpers
    void create_pipeline_layouts();
    VkPipeline create_compute_pipeline(const char* shader_path, VkPipelineLayout layout);
    void init_effect(FractalType type, const char* shader_path, bool use_deep_zoom_layout = false);
};

// Implementation
inline void ComputeEffect::update_from_state(const FractalState& state, float time) {
    ComputePushConstants new_constants = {};  // Zero initialize

    switch (type) {
    case FractalType::Mandelbrot:
        new_constants.data1 = glm::vec4(
            static_cast<float>(state.center_x),
            static_cast<float>(state.center_y),
            static_cast<float>(state.zoom),
            static_cast<float>(state.max_iterations)
        );
        new_constants.data2 = glm::vec4(
            state.color_offset,
            state.color_scale,
            static_cast<float>(state.bailout),
            static_cast<float>(state.palette_mode)
        );
        new_constants.data3 = glm::vec4(
            static_cast<float>(state.antialiasing_samples),
            static_cast<float>(state.interior_style),
            state.orbit_trap_enabled ? 1.0f : 0.0f,
            state.orbit_trap_radius
        );
        new_constants.data4 = glm::vec4(
            state.stripe_density,
            state.stripe_enabled ? 1.0f : 0.0f,
            state.color_brightness,      // ← ADDED
            state.color_saturation       // ← ADDED
        );
        new_constants.data5 = glm::vec4(
            state.color_contrast,        // ← ADDED
            0.0f, 0.0f, 0.0f
        );
        break;

    case FractalType::JuliaSet:
        new_constants.data1 = glm::vec4(
            static_cast<float>(state.center_x),
            static_cast<float>(state.center_y),
            static_cast<float>(state.zoom),
            static_cast<float>(state.max_iterations)
        );
        new_constants.data2 = glm::vec4(
            static_cast<float>(state.julia_c_real),
            static_cast<float>(state.julia_c_imag),
            static_cast<float>(state.bailout),
            static_cast<float>(state.color_offset)
        );
        new_constants.data3 = glm::vec4(
            static_cast<float>(state.antialiasing_samples),
            static_cast<float>(state.color_scale),
            state.color_brightness,      // ← ADDED
            state.color_saturation       // ← ADDED
        );
        new_constants.data4 = glm::vec4(
            state.color_contrast,        // ← ADDED
            static_cast<float>(state.palette_mode),  // ← ADDED
            0.0f, 0.0f
        );
        new_constants.data5 = glm::vec4(0.0f);
        break;

    case FractalType::BurningShip:
        new_constants.data1 = glm::vec4(
            static_cast<float>(state.center_x),
            static_cast<float>(state.center_y),
            static_cast<float>(state.zoom),
            static_cast<float>(state.max_iterations)
        );
        new_constants.data2 = glm::vec4(
            state.color_offset,
            state.color_scale,
            static_cast<float>(state.bailout),
            static_cast<float>(state.palette_mode)
        );
        new_constants.data3 = glm::vec4(
            static_cast<float>(state.antialiasing_samples),
            static_cast<float>(state.interior_style),
            state.orbit_trap_enabled ? 1.0f : 0.0f,
            state.orbit_trap_radius
        );
        new_constants.data4 = glm::vec4(
            state.stripe_density,
            state.stripe_enabled ? 1.0f : 0.0f,
            state.color_brightness,      // ← ADDED
            state.color_saturation       // ← ADDED
        );
        new_constants.data5 = glm::vec4(
            state.color_contrast,        // ← ADDED
            0.0f, 0.0f, 0.0f
        );
        break;

    case FractalType::Mandelbulb:
        new_constants.data1 = glm::vec4(
            state.camera_distance,
            state.rotation_y,
            state.mandelbulb_power,
            static_cast<float>(state.max_iterations)
        );
        new_constants.data2 = glm::vec4(
            state.color_offset,
            state.color_scale,
            0.0f,
            static_cast<float>(state.palette_mode)
        );
        new_constants.data3 = glm::vec4(
            time,
            state.fov,
            static_cast<float>(state.antialiasing_samples),
            state.color_brightness       // ← CHANGED from hardcoded 0.8f
        );
        new_constants.data4 = glm::vec4(
            state.rotation_speed,
            state.color_saturation,      // ← ADDED
            state.color_contrast,        // ← ADDED
            0.0f
        );
        new_constants.data5 = glm::vec4(0.0f);
        break;

    case FractalType::Phoenix:
        // center + zoom
        new_constants.data1 = glm::vec4(
            static_cast<float>(state.center_x),
            static_cast<float>(state.center_y),
            static_cast<float>(state.zoom),
            static_cast<float>(state.max_iterations)
        );

        // Julia parameters + Phoenix p and r
        new_constants.data2 = glm::vec4(
            static_cast<float>(state.julia_c_real),
            static_cast<float>(state.julia_c_imag),
            static_cast<float>(state.phoenix_p),      // ← FIX: Phoenix p parameter
            static_cast<float>(state.phoenix_r)       // ← FIX: Phoenix r parameter
        );

        // Anti-aliasing + color controls
        new_constants.data3 = glm::vec4(
            static_cast<float>(state.antialiasing_samples),
            static_cast<float>(state.color_scale),
            state.color_brightness,
            state.color_saturation
        );

        // Color enhancement + palette + stripe effects
        new_constants.data4 = glm::vec4(
            state.color_contrast,
            static_cast<float>(state.palette_mode),
            static_cast<float>(state.stripe_density),  // ← ADD: Stripe density
            state.use_julia_set ? 1.0f : 0.0f         // ← ADD: Julia mode flag
        );

        new_constants.data5 = glm::vec4(0.0f);
        break;



    case FractalType::Deep_Zoom:
    {
        // Split doubles into high/low pairs for double-double precision
        auto split_double = [](double value) -> std::pair<float, float> {
            float hi = static_cast<float>(value);
            float lo = static_cast<float>(value - static_cast<double>(hi));
            return { hi, lo };
            };

        auto [cx_hi, cx_lo] = split_double(state.center_x);
        auto [cy_hi, cy_lo] = split_double(state.center_y);
        auto [z_hi, z_lo] = split_double(state.zoom);

        // data1: center_x_hi, center_x_lo, center_y_hi, center_y_lo
        new_constants.data1 = glm::vec4(cx_hi, cx_lo, cy_hi, cy_lo);

        // data2: zoom_hi, zoom_lo, max_iterations, use_perturbation
        new_constants.data2 = glm::vec4(
            z_hi,
            z_lo,
            static_cast<float>(state.max_iterations),
            state.use_perturbation ? 1.0f : 0.0f
        );

        // data3: color_offset, color_scale, bailout, palette_mode
        new_constants.data3 = glm::vec4(
            state.color_offset,
            state.color_scale,
            static_cast<float>(state.bailout),
            static_cast<float>(state.palette_mode)
        );

        // data4: samples_per_pixel, reference_iterations, use_series_approx, series_order
        new_constants.data4 = glm::vec4(
            static_cast<float>(state.antialiasing_samples),
            static_cast<float>(state.reference_iterations),
            state.use_series_approximation ? 1.0f : 0.0f,
            static_cast<float>(state.series_order)
        );

        new_constants.data5 = glm::vec4(0.0f);

        // DEBUG: Print what we're sending to shader
        fmt::print("=== DEEP ZOOM PUSH CONSTANTS ===\n");
        fmt::print("Center: hi=({}, {}), lo=({}, {})\n",
            new_constants.data1.x, new_constants.data1.z,
            new_constants.data1.y, new_constants.data1.w);
        fmt::print("Zoom: hi={}, lo={}\n",
            new_constants.data2.x, new_constants.data2.y);
        fmt::print("Max iter: {}, Use pert: {}\n",
            new_constants.data2.z, new_constants.data2.w);
        fmt::print("Ref iter: {}\n", new_constants.data4.y);
        fmt::print("================================\n");


    }
    break;
       
    }

    /// NEW: Always update, and update the cache
    push_constants = new_constants;
    cached_constants = new_constants;
    is_dirty = true;
    
}

inline void ComputeEffectManager::dispatch(VkCommandBuffer cmd, FractalType type,
    const FractalState& state, float time,
    VkDescriptorSet desc_set, VkExtent2D extent) {

    ComputeEffect* effect = get_effect(type);
    if (!effect || effect->pipeline == VK_NULL_HANDLE) {
        return;  // Safety check
    }

    // Update push constants from current state
    effect->update_from_state(state, time);

    // Bind the pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect->pipeline);

    // ✅ Use the effect's specific layout (critical for Step 8!)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        effect->layout,  // Each effect has its own layout
        0, 1, &desc_set, 0, nullptr);

    // ✅ Push constants must also use the correct layout
    vkCmdPushConstants(cmd, effect->layout, VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(ComputePushConstants), &effect->push_constants);

    effect->mark_clean();

    // Calculate workgroups
    constexpr uint32_t WORKGROUP_SIZE = 16;
    uint32_t workgroups_x = (extent.width + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    uint32_t workgroups_y = (extent.height + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

    // Dispatch compute shader
    vkCmdDispatch(cmd, workgroups_x, workgroups_y, 1);
}

inline ComputeEffect* ComputeEffectManager::get_effect(FractalType type) {
    size_t index = static_cast<size_t>(type);
    if (index >= effects.size()) return nullptr;
    return &effects[index];
}