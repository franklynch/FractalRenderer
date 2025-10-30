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
        // ═══════════════════════════════════════════════════════════════
        // Double-Double Splitting Functions
        // ═══════════════════════════════════════════════════════════════

        // Split high-precision float into double-double format
        auto split_highprecision = [](const HighPrecisionFloat& hp_value) -> std::pair<float, float> {
            double value = hp_value.to_double();
            float hi = static_cast<float>(value);
            double hi_as_double = static_cast<double>(hi);
            float lo = static_cast<float>(value - hi_as_double);
            return { hi, lo };
            };

        // Fallback for regular doubles (when HP not available)
        auto split_double = [](double value) -> std::pair<float, float> {
            float hi = static_cast<float>(value);
            double hi_as_double = static_cast<double>(hi);
            float lo = static_cast<float>(value - hi_as_double);
            return { hi, lo };
            };

        // ═══════════════════════════════════════════════════════════════
        // Coordinate Conversion (HP or Double)
        // ═══════════════════════════════════════════════════════════════

        if (state.hp_coords.is_valid) {
            // Use high-precision path
            auto [cx_hi, cx_lo] = split_highprecision(state.hp_coords.center_x);
            auto [cy_hi, cy_lo] = split_highprecision(state.hp_coords.center_y);
            auto [z_hi, z_lo] = split_highprecision(state.hp_coords.zoom);

            // data1: center_x_hi, center_x_lo, center_y_hi, center_y_lo
            new_constants.data1 = glm::vec4(cx_hi, cx_lo, cy_hi, cy_lo);

            // data2: zoom_hi, zoom_lo, max_iterations, use_perturbation
            new_constants.data2 = glm::vec4(
                z_hi,
                z_lo,
                static_cast<float>(state.max_iterations),
                state.use_perturbation ? 1.0f : 0.0f
            );
        }
        else {
            // Fallback to regular doubles if HP not available
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
        }

        // ═══════════════════════════════════════════════════════════════
        // Remaining Push Constants
        // ═══════════════════════════════════════════════════════════════

        // data3: color_offset, color_scale, bailout, palette_mode
        new_constants.data3 = glm::vec4(
            state.color_offset,
            state.color_scale,
            static_cast<float>(state.bailout),
            static_cast<float>(state.palette_mode)
        );

        // data4: samples_per_pixel, reference_iterations, use_series_approx, series_order
        // ✅ FIX: Use actual orbit buffer size, not state.reference_iterations
        new_constants.data4 = glm::vec4(
            static_cast<float>(state.antialiasing_samples),
            static_cast<float>(state.reference_iterations), 
            state.use_series_approximation ? 1.0f : 0.0f,
            static_cast<float>(state.series_order)
        );

        // data5: (if needed for your shader)
        new_constants.data5 = glm::vec4(0.0f);

        // ═══════════════════════════════════════════════════════════════
        // Safety Checks & Corrections
        // ═══════════════════════════════════════════════════════════════

        // ✅ FIX: Ensure ref_iter is valid
        // Note: ref_iter validation done in VulkanEngine before dispatch
        // No access to reference_orbit here

        // ✅ FIX: Ensure zoom is valid
        if (new_constants.data2.x == 0.0f || std::isnan(new_constants.data2.x) || std::isinf(new_constants.data2.x)) {
            fmt::print("⚠️  Invalid zoom ({:.6e}), resetting to 3.0\n", new_constants.data2.x);
            new_constants.data2.x = 3.0f;
            new_constants.data2.y = 0.0f;
        }

        // ✅ FIX: Ensure bailout is valid
        if (new_constants.data3.z < 1.0f) {
            fmt::print("⚠️  Invalid bailout ({:.1f}), resetting to 256.0\n", new_constants.data3.z);
            new_constants.data3.z = 256.0f;
        }

        // ═══════════════════════════════════════════════════════════════
        // Diagnostic Output (periodic)
        // ═══════════════════════════════════════════════════════════════

        static uint64_t call_counter = 0;
        static uint64_t last_print = 0;
        call_counter++;

        if (call_counter - last_print > 60) {

            fmt::print("\n╔════════════════════════════════════════╗\n");
            fmt::print("  Deep Zoom Push Constants (Frame {})\n", call_counter);
            fmt::print("╠════════════════════════════════════════╣\n");
            fmt::print("  data1 (center): ({:.6f}, {:.6f}, {:.6f}, {:.6f})\n",
                new_constants.data1.x, new_constants.data1.y,
                new_constants.data1.z, new_constants.data1.w);
            fmt::print("  data2 (zoom+iter): ({:.6e}, {:.6e}, {:.0f}, {:.0f})\n",
                new_constants.data2.x, new_constants.data2.y,
                new_constants.data2.z, new_constants.data2.w);
            fmt::print("  data3 (color): ({:.2f}, {:.2f}, {:.0f}, {:.0f})\n",
                new_constants.data3.x, new_constants.data3.y,
                new_constants.data3.z, new_constants.data3.w);
            fmt::print("  data4 (samples/orbit): ({:.0f}, {:.0f}, {:.0f}, {:.0f})\n",
                new_constants.data4.x, new_constants.data4.y,
                new_constants.data4.z, new_constants.data4.w);

            // Validation warnings
            if (new_constants.data2.x <= 0.0f) {
                fmt::print("  ⚠️  WARNING: Zoom is zero or negative!\n");
            }
            if (new_constants.data4.y == 0.0f && new_constants.data2.w > 0.5f) {
                fmt::print("  ⚠️  WARNING: Perturbation enabled but orbit_count is 0!\n");
            }
            else {
                fmt::print("  ✅ Orbit count: {:.0f} iterations\n", new_constants.data4.y);
            }

            fmt::print("╚════════════════════════════════════════╝\n\n");
            last_print = call_counter;
            

            // Validation warnings
            bool has_warnings = false;
            if (new_constants.data2.x <= 0.0f) {
                fmt::print("\n  ⚠️  WARNING: Zoom is zero or negative!\n");
                has_warnings = true;
            }
            if (new_constants.data4.y == 0.0f && new_constants.data2.w > 0.5f) {
                fmt::print("\n  ⚠️  WARNING: Perturbation enabled but ref_iter is 0!\n");
                has_warnings = true;
            }
            if (new_constants.data3.z < 1.0f) {
                fmt::print("\n  ⚠️  WARNING: Bailout is too low!\n");
                has_warnings = true;
            }
            if (!has_warnings) {
                fmt::print("\n  ✅ All values valid!\n");
            }

            fmt::print("╚════════════════════════════════════════╝\n\n");

            last_print = call_counter;
        }
    }
    
    break;

    // ============================================================================
    // USAGE NOTES:
    // ============================================================================
    // 1. Make sure to include <fmt/core.h> at the top of compute_effect_manager.h
    // 2. The fmt::print statements are for debugging - remove them in production
    // 3. This assumes you've already updated fractal_state.h with the hp_coords struct
    // ============================================================================
       
    }

    /// NEW: Always update, and update the cache
    //push_constants = new_constants;
    // cached_constants = new_constants;
    //is_dirty = true;
    
    if (memcmp(&push_constants, &new_constants, sizeof(ComputePushConstants)) != 0) {
        push_constants = new_constants;
        is_dirty = true;
    }
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