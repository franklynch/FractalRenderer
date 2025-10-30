// deep_zoom_system.h
// Deep zoom system with high-precision support for extreme zoom levels

#pragma once
#include <complex>
#include <vector>
#include <string>
#include <vulkan/vulkan.h>
#include "vk/vk_mem_alloc.h"
#include "vk/vk_types.h"
#include "high_precision_math.h"

struct FractalState;

// ============================================================================
// Precision Mode Enum
// ============================================================================
enum class PrecisionMode {
    Double,        // Standard double precision (zoom > 1e-14)
    Quad,          // MPFR 128-bit precision (zoom 1e-30 to 1e-14)
    Arbitrary      // MPFR dynamic precision (zoom < 1e-30)
};

// ============================================================================
// Arbitrary Precision Float
// ============================================================================
class ArbitraryFloat {
public:
    double mantissa;
    int exponent;

    ArbitraryFloat(double value = 0.0);
    ArbitraryFloat(const HighPrecisionFloat& hp);

    double to_double() const;
    HighPrecisionFloat to_high_precision(int bits = 128) const;

    void normalize();

    ArbitraryFloat operator+(const ArbitraryFloat& other) const;
    ArbitraryFloat operator-(const ArbitraryFloat& other) const;
    ArbitraryFloat operator*(const ArbitraryFloat& other) const;
    ArbitraryFloat operator/(const ArbitraryFloat& other) const;

    std::pair<float, float> to_double_double() const {
        double val = to_double();
        float hi = static_cast<float>(val);
        float lo = static_cast<float>(val - static_cast<double>(hi));
        return { hi, lo };
    }

    

};

// ============================================================================
// Reference Orbit Buffer
// ============================================================================
struct ReferenceOrbitPoint {
    std::complex<double> value;
    int iteration;
};



class ReferenceOrbitBuffer {
public:
    std::vector<ReferenceOrbitPoint> cpu_data;
    AllocatedBuffer gpu_buffer;
    bool is_dirty = true;

    void resize(size_t new_size) {
        cpu_data.resize(new_size);
        is_dirty = true;
    }

    void upload_to_gpu(VkDevice device, VmaAllocator allocator);
    void destroy(VkDevice device, VmaAllocator allocator);
};

// ============================================================================
// Zoom Animation
// ============================================================================
struct ZoomKeyframe {
    ArbitraryFloat center_x{ 0.0 };
    ArbitraryFloat center_y{ 0.0 };
    ArbitraryFloat zoom{ 1.0 };
    float duration = 0.0f;
};

// ============================================================================
// Deep Zoom State
// ============================================================================
struct DeepZoomState {
    ArbitraryFloat center_x;
    ArbitraryFloat center_y;
    ArbitraryFloat zoom;

    int max_iterations = 1000;
    float bailout = 256.0f;

    bool use_perturbation = true;
    int reference_iterations = 0;
    int deep_zoom_iterations = 0;

    bool use_series_approximation = true;
    int series_order = 10;

    int samples_per_pixel = 1;

    float color_offset = 0.0f;
    float color_scale = 1.0f;
    int palette_mode = 0;

    bool zoom_animating = false;
    float zoom_progress = 0.0f;

    int zoom_depth_level = 0;
    float estimated_render_time = 0.0f;

    // High-precision support
    PrecisionMode precision_mode = PrecisionMode::Double;
    int precision_bits = 64;
    bool high_precision_enabled = false;
};

// ============================================================================
// Deep Zoom Manager
// ============================================================================
class DeepZoomManager {
public:
    DeepZoomManager(VkDevice device, VmaAllocator allocator);
    ~DeepZoomManager();

    void initialize();
    void update(float delta_time);

    void playZoomPath(const std::vector<ZoomKeyframe>& path);
    void zoomTo(const ArbitraryFloat& target_x,
        const ArbitraryFloat& target_y,
        const ArbitraryFloat& target_zoom,
        float duration = 5.0f);

    std::string exportCoordinates() const;

    // Reference orbit computation
    void compute_reference_orbit();
    void compute_reference_orbit_high_precision();

    void set_fractal_state(FractalState* state_ptr) {
        _fractal_state = state_ptr;
    }

    // Precision mode management
    void update_precision_mode();
    PrecisionMode get_precision_mode() const { return state.precision_mode; }

    DeepZoomState state;
    ReferenceOrbitBuffer reference_orbit;

private:
    VkDevice _device;
    VmaAllocator _allocator;

    std::vector<ZoomKeyframe> _zoom_path;
    size_t _current_keyframe = 0;
    float _animation_time = 0.0f;

    void update_animation(float delta_time);
    void interpolate_to_keyframe(int index, float t);

    int calculate_required_precision_bits() const;

    FractalState* _fractal_state = nullptr;

};

// ============================================================================
// Preset Deep Zoom Locations
// ============================================================================
namespace DeepZoomPresets {
    ZoomKeyframe createSeahorseZoom();
    ZoomKeyframe createElephantZoom();
    ZoomKeyframe createMiniMandelbrotZoom();
}