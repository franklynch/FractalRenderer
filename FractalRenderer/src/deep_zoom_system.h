#pragma once

#include "vk_types.h"
#include <vector>
#include <complex>
#include <string>

// ArbitraryFloat - high-precision floating point (mantissa + exponent)
struct ArbitraryFloat {
    double mantissa;  // Mantissa (significand)
    int exponent;     // Power of 10 exponent

    ArbitraryFloat() : mantissa(1.0), exponent(0) {}
    ArbitraryFloat(double value);
    ArbitraryFloat(double m, int e) : mantissa(m), exponent(e) {}

    // ✅ FIXED: Use to_double() not toDouble()
    double to_double() const;
    void normalize();

    ArbitraryFloat operator+(const ArbitraryFloat& other) const;
    ArbitraryFloat operator-(const ArbitraryFloat& other) const;
    ArbitraryFloat operator*(const ArbitraryFloat& other) const;
    ArbitraryFloat operator/(const ArbitraryFloat& other) const;
};

// Double-Double precision (split into hi/lo parts)
struct DoubleDouble {
    double hi;  // High part
    double lo;  // Low part (error/remainder)

    DoubleDouble() : hi(0.0), lo(0.0) {}
    DoubleDouble(double h, double l) : hi(h), lo(l) {}

    static DoubleDouble from_double(double d) {
        return DoubleDouble(d, 0.0);
    }
};

// Reference orbit point for perturbation theory
struct ReferenceOrbitPoint {
    std::complex<double> value;  // Complex value at this iteration
    int iteration;               // Iteration number
};

// GPU buffer for reference orbit
struct ReferenceOrbitBuffer {
    // ✅ FIXED: Use cpu_data not orbit
    std::vector<ReferenceOrbitPoint> cpu_data;

    AllocatedBuffer gpu_buffer;
    bool is_dirty = true;

    void resize(size_t size) {
        cpu_data.resize(size);
        is_dirty = true;
    }

    void upload_to_gpu(VkDevice device, VmaAllocator allocator);
    void destroy(VkDevice device, VmaAllocator allocator);
};

// Deep zoom state
struct DeepZoomState {
    // High-precision center coordinates
    ArbitraryFloat center_x;
    ArbitraryFloat center_y;
    ArbitraryFloat zoom;

    // Rendering parameters
    int max_iterations = 1024;
    int samples_per_pixel = 1;

    // Perturbation theory settings
    bool use_perturbation = true;
    int reference_iterations = 0;

    // Series approximation settings
    bool use_series_approximation = false;
    int series_order = 4;

    // ✅ NEW: Animation state (expected by ui_manager.cpp)
    bool zoom_animating = false;
    float zoom_progress = 0.0f;

    // ✅ NEW: Status info (expected by ui_manager.cpp)
    int zoom_depth_level = 0;           // 0=shallow, 1=moderate, 2=deep, 3=extreme
    int deep_zoom_iterations = 0;       // Actual iterations used
    float estimated_render_time = 0.0f; // Estimated render time in ms

    DeepZoomState()
        : center_x(0.0), center_y(0.0), zoom(1.0) {
    }
};

// Zoom path keyframe
struct ZoomKeyframe {
    ArbitraryFloat center_x;
    ArbitraryFloat center_y;
    ArbitraryFloat zoom;
    float duration;  // Duration to reach this keyframe
};

// Deep zoom manager
class DeepZoomManager {
public:
    DeepZoomManager(VkDevice device, VmaAllocator allocator);
    ~DeepZoomManager();

    // ✅ NEW: Initialize (expected by vk_engine.cpp)
    void initialize();

    // Update and render
    void update(float delta_time);
    void compute_reference_orbit();

    // ✅ NEW: Animation methods (expected by vk_engine.cpp)
    void playZoomPath(const std::vector<ZoomKeyframe>& path);
    void zoomTo(const ArbitraryFloat& target_x, const ArbitraryFloat& target_y,
        const ArbitraryFloat& target_zoom, float duration);

    // ✅ NEW: Export coordinates (expected by vk_engine.cpp)
    std::string exportCoordinates() const;

    // State management
    DeepZoomState state;
    ReferenceOrbitBuffer reference_orbit;

    // Mark dirty when state changes
    void mark_dirty() { reference_orbit.is_dirty = true; }

private:
    VkDevice _device;
    VmaAllocator _allocator;

    // Animation state
    std::vector<ZoomKeyframe> _zoom_path;
    int _current_keyframe = 0;
    float _animation_time = 0.0f;

    void update_animation(float delta_time);
    void interpolate_to_keyframe(int index, float t);
};

// ✅ NEW: Deep zoom presets (expected by vk_engine.cpp)
namespace DeepZoomPresets {
    ZoomKeyframe createSeahorseZoom();
    ZoomKeyframe createElephantZoom();
    ZoomKeyframe createMiniMandelbrotZoom();
}
