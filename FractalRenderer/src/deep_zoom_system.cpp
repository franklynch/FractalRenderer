// deep_zoom_system.cpp
// Deep zoom system implementation with high-precision support

#include "deep_zoom_system.h"
#include "fractal_state.h"
#include "high_precision_math.h"

#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <fmt/core.h>

// ============================================================================
// ArbitraryFloat Implementation
// ============================================================================

ArbitraryFloat::ArbitraryFloat(double value) {
    if (value == 0.0) {
        mantissa = 0.0;
        exponent = 0;
        return;
    }

    exponent = static_cast<int>(std::floor(std::log10(std::abs(value))));
    mantissa = value / std::pow(10.0, exponent);
    normalize();
}

ArbitraryFloat::ArbitraryFloat(const HighPrecisionFloat& hp) {
    double d = hp.to_double();

    if (d == 0.0) {
        mantissa = 0.0;
        exponent = 0;
        return;
    }

    exponent = static_cast<int>(std::floor(std::log10(std::abs(d))));
    mantissa = d / std::pow(10.0, exponent);
    normalize();
}

double ArbitraryFloat::to_double() const {
    return mantissa * std::pow(10.0, exponent);
}

HighPrecisionFloat ArbitraryFloat::to_high_precision(int bits) const {
    return HighPrecisionFloat(to_double(), static_cast<mpfr_prec_t>(bits));
}

void ArbitraryFloat::normalize() {
    if (mantissa == 0.0) {
        exponent = 0;
        return;
    }

    while (std::abs(mantissa) >= 10.0) {
        mantissa /= 10.0;
        exponent++;
    }

    while (std::abs(mantissa) < 1.0 && mantissa != 0.0) {
        mantissa *= 10.0;
        exponent--;
    }
}

ArbitraryFloat ArbitraryFloat::operator+(const ArbitraryFloat& other) const {
    return ArbitraryFloat(to_double() + other.to_double());
}

ArbitraryFloat ArbitraryFloat::operator-(const ArbitraryFloat& other) const {
    return ArbitraryFloat(to_double() - other.to_double());
}

ArbitraryFloat ArbitraryFloat::operator*(const ArbitraryFloat& other) const {
    ArbitraryFloat result;
    result.mantissa = mantissa * other.mantissa;
    result.exponent = exponent + other.exponent;
    result.normalize();
    return result;
}

ArbitraryFloat ArbitraryFloat::operator/(const ArbitraryFloat& other) const {
    ArbitraryFloat result;
    result.mantissa = mantissa / other.mantissa;
    result.exponent = exponent - other.exponent;
    result.normalize();
    return result;
}

// ============================================================================
// ReferenceOrbitBuffer Implementation
// ============================================================================

void ReferenceOrbitBuffer::upload_to_gpu(VkDevice device, VmaAllocator allocator) {
    if (!is_dirty || cpu_data.empty()) return;

    // ✅ Convert complex<double> to vec2 (float) for GPU
    std::vector<glm::vec2> gpu_data(cpu_data.size());
    for (size_t i = 0; i < cpu_data.size(); i++) {
        gpu_data[i] = glm::vec2(
            static_cast<float>(cpu_data[i].value.real()),
            static_cast<float>(cpu_data[i].value.imag())
        );
    }

    size_t buffer_size = gpu_data.size() * sizeof(glm::vec2);  // ← 8 bytes per element

    // vkDeviceWaitIdle(device);

    // Destroy old buffer if it exists
    if (gpu_buffer.buffer != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        vmaDestroyBuffer(allocator, gpu_buffer.buffer, gpu_buffer.allocation);
        gpu_buffer.buffer = VK_NULL_HANDLE;
    }

    // Create buffer
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VkResult result = vmaCreateBuffer(allocator, &buffer_info, &alloc_info,
        &gpu_buffer.buffer, &gpu_buffer.allocation, &gpu_buffer.info);

    if (result != VK_SUCCESS) {
        fmt::print("❌ Failed to create orbit buffer! VkResult: {}\n", (int)result);
        return;
    }

    // Map and copy data
    void* data;
    vmaMapMemory(allocator, gpu_buffer.allocation, &data);
    memcpy(data, gpu_data.data(), buffer_size);
    vmaUnmapMemory(allocator, gpu_buffer.allocation);

    is_dirty = false;

    fmt::print("✅ Uploaded orbit buffer: {} points, {} bytes\n",
        cpu_data.size(), buffer_size);
}

void ReferenceOrbitBuffer::destroy(VkDevice device, VmaAllocator allocator) {
    if (gpu_buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, gpu_buffer.buffer, gpu_buffer.allocation);
        gpu_buffer.buffer = VK_NULL_HANDLE;
    }
}

// ============================================================================
// DeepZoomManager Implementation
// ============================================================================

DeepZoomManager::DeepZoomManager(VkDevice device, VmaAllocator allocator)
    : _device(device), _allocator(allocator) {
    // Initialize with default state
    state.center_x = ArbitraryFloat(-0.5);
    state.center_y = ArbitraryFloat(0.0);
    state.zoom = ArbitraryFloat(2.0);
}

DeepZoomManager::~DeepZoomManager() {
    reference_orbit.destroy(_device, _allocator);
}

void DeepZoomManager::initialize() {
    // Compute initial reference orbit
    compute_reference_orbit();
}

void DeepZoomManager::update(float delta_time) {
    // Update animation if playing
    if (state.zoom_animating) {
        update_animation(delta_time);
    }

    // Update zoom depth level based on current zoom
    double zoom_val = state.zoom.to_double();
    if (zoom_val > 1e-6) {
        state.zoom_depth_level = 0;  // Shallow
    }
    else if (zoom_val > 1e-10) {
        state.zoom_depth_level = 1;  // Moderate
    }
    else if (zoom_val > 1e-14) {
        state.zoom_depth_level = 2;  // Deep
    }
    else {
        state.zoom_depth_level = 3;  // Extreme
    }

    // Estimate render time (rough heuristic)
    state.estimated_render_time = state.max_iterations * 0.001f *
        state.samples_per_pixel *
        (1.0f + state.zoom_depth_level * 0.5f);
}

// ============================================================================
// High-Precision Support
// ============================================================================

int DeepZoomManager::calculate_required_precision_bits() const {
    double zoom_val = std::abs(state.zoom.to_double());

    if (zoom_val >= 1e-14) {
        return 64;  // Standard double is sufficient
    }

    // Calculate bits needed: ~3.32 bits per decimal digit
    double digits_needed = -std::log10(zoom_val);
    int bits = 64 + static_cast<int>(digits_needed * 3.32) + 64; // +64 safety margin

    // Clamp to reasonable range
    bits = std::max(128, std::min(bits, 4096));

    return bits;
}

void DeepZoomManager::update_precision_mode() {
    double zoom_val = std::abs(state.zoom.to_double());

    PrecisionMode old_mode = state.precision_mode;

    if (zoom_val > 1e-14) {
        state.precision_mode = PrecisionMode::Double;
        state.precision_bits = 64;
        state.high_precision_enabled = false;
    }
    else if (zoom_val > 1e-30) {
        state.precision_mode = PrecisionMode::Quad;
        state.precision_bits = calculate_required_precision_bits();
        state.high_precision_enabled = true;
    }
    else {
        state.precision_mode = PrecisionMode::Arbitrary;
        state.precision_bits = calculate_required_precision_bits();
        state.high_precision_enabled = true;

        if (_fractal_state) {
            _fractal_state->update_hp_from_double(state.precision_bits);
        }
    }

    // Log precision mode changes
    if (old_mode != state.precision_mode) {
        const char* mode_names[] = { "Double", "Quad", "Arbitrary" };
        fmt::print("\n========================================\n");
        fmt::print("Precision Mode Changed: {}\n", mode_names[(int)state.precision_mode]);
        fmt::print("Zoom Level: {:.2e}\n", zoom_val);
        fmt::print("Precision Bits: {}\n", state.precision_bits);
        fmt::print("========================================\n\n");
    }
}

void DeepZoomManager::compute_reference_orbit_high_precision() {
    int precision_bits = state.precision_bits;

    fmt::print("\n==================================================\n");
    fmt::print("  HIGH-PRECISION REFERENCE ORBIT COMPUTATION  \n");
    fmt::print("==================================================\n");
    fmt::print("  Precision: {} bits ({} decimal digits)\n",
        precision_bits, (int)(precision_bits / 3.32));
    fmt::print("  Max Iterations: {}\n", state.max_iterations);
    fmt::print("  Center: ({:.20e}, {:.20e})\n",
        state.center_x.to_double(), state.center_y.to_double());
    fmt::print("  Zoom: {:.2e}\n", state.zoom.to_double());
    fmt::print("\n");

    auto start_time = std::chrono::high_resolution_clock::now();

    // Create high-precision center point
    HighPrecisionComplex c(precision_bits);
    c.real = state.center_x.to_high_precision(precision_bits);
    c.imag = state.center_y.to_high_precision(precision_bits);

    // Initialize z = 0
    HighPrecisionComplex z(precision_bits);
    z.real = HighPrecisionFloat(0.0, precision_bits);
    z.imag = HighPrecisionFloat(0.0, precision_bits);

    // Prepare storage
    reference_orbit.resize(state.max_iterations);

    int escape_iter = state.max_iterations;
    int progress_interval = std::max(1, state.max_iterations / 20);

    for (int i = 0; i < state.max_iterations; i++) {
        // Store current z value (converted to double for GPU)
        auto z_double = z.to_complex_double();
        reference_orbit.cpu_data[i].value = z_double;
        reference_orbit.cpu_data[i].iteration = i;

        // Check escape condition
        HighPrecisionFloat mag_sq = z.magnitude_squared();

        if (mag_sq > 4.0) {
            escape_iter = i;
            fmt::print("  -> Escaped at iteration {}\n", i);
            break;
        }

        // Iterate: z = z^2 + c
        z = z.square() + c;

        // Progress indicator
        if (i % progress_interval == 0 && i > 0) {
            double progress = (double)i / state.max_iterations * 100.0;
            fmt::print("  Progress: {:.1f}% (iter {}/{}, |z| = {:.2e})\n",
                progress, i, state.max_iterations,
                std::sqrt(mag_sq.to_double()));
        }
    }

    // Trim to actual escape iteration
    if (escape_iter < state.max_iterations) {
        reference_orbit.resize(escape_iter + 1);
    }

    state.reference_iterations = static_cast<int>(reference_orbit.cpu_data.size());
    state.deep_zoom_iterations = state.reference_iterations;

    // Upload to GPU
    reference_orbit.upload_to_gpu(_device, _allocator);

    // Store HP coordinates in FractalState
    // *** ADD THIS: Store coords in FractalState even in double mode ***
    if (_fractal_state != nullptr) {
        _fractal_state->hp_coords.center_x = HighPrecisionFloat(state.center_x.to_double(),
            static_cast<mpfr_prec_t>(64));
        _fractal_state->hp_coords.center_y = HighPrecisionFloat(state.center_y.to_double(),
            static_cast<mpfr_prec_t>(64));
        _fractal_state->hp_coords.zoom = HighPrecisionFloat(state.zoom.to_double(),
            static_cast<mpfr_prec_t>(64));
        _fractal_state->hp_coords.precision_bits = 64;
        _fractal_state->hp_coords.is_valid = true;
    }

    fmt::print("Reference orbit computed: {} iterations (double precision)\n",
        state.reference_iterations);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);


    fmt::print("\n");
    fmt::print("==================================================\n");
    fmt::print("  COMPUTATION COMPLETE                        \n");
    fmt::print("==================================================\n");
    fmt::print("  Total Iterations: {}\n", state.reference_iterations);
    fmt::print("  Computation Time: {} ms\n", duration.count());
    fmt::print("  Iterations/Second: {:.0f}\n",
        state.reference_iterations / (duration.count() / 1000.0));
    fmt::print("\n");
}

void DeepZoomManager::compute_reference_orbit() {
    if (!state.use_perturbation) return;

    // Update precision mode and route to high-precision if needed
    update_precision_mode();

    if (state.high_precision_enabled) {
        compute_reference_orbit_high_precision();
        return;
    }

    // Standard double-precision computation
    fmt::print("Using double-precision reference orbit computation...\n");

    // Get center point in double precision
    std::complex<double> c(state.center_x.to_double(),
        state.center_y.to_double());

    // Compute reference orbit: iterate z = z^2 + c
    std::complex<double> z(0.0, 0.0);

    reference_orbit.resize(state.max_iterations);

    // *** FIX: Define progress_interval ***
    int progress_interval = std::max(1, state.max_iterations / 20);

    int escape_iter = state.max_iterations;

    for (int i = 0; i < state.max_iterations; i++) {
        reference_orbit.cpu_data[i].value = z;
        reference_orbit.cpu_data[i].iteration = i;

        // Check for escape
        double mag = std::abs(z);
        if (mag > 2.0) {
            escape_iter = i;
            fmt::print("  -> Escaped at iteration {} (|z| = {})\n", i, mag);
            break;
        }

        // Safety check for very large values
        if (mag > 1e10 || std::isnan(mag) || std::isinf(mag)) {
            escape_iter = i;
            fmt::print("  -> Diverged at iteration {} (|z| = {})\n", i, mag);
            break;
        }

        // Iterate: z = z^2 + c
        z = z * z + c;

        // Progress indicator
        if (i % progress_interval == 0 && i > 0) {
            double progress = (double)i / state.max_iterations * 100.0;
            fmt::print("  Progress: {:.1f}% (iter {}/{}, |z| = {:.2e})\n",
                progress, i, state.max_iterations, mag);
        }
    }

    // Trim to actual escape iteration
    if (escape_iter < state.max_iterations) {
        reference_orbit.resize(escape_iter + 1);
    }

    state.reference_iterations = static_cast<int>(reference_orbit.cpu_data.size());
    state.deep_zoom_iterations = state.reference_iterations;

    // *** ADD: Store coords in FractalState even in double mode ***
    if (_fractal_state != nullptr) {
        _fractal_state->hp_coords.center_x = HighPrecisionFloat(state.center_x.to_double(),
            static_cast<mpfr_prec_t>(64));
        _fractal_state->hp_coords.center_y = HighPrecisionFloat(state.center_y.to_double(),
            static_cast<mpfr_prec_t>(64));
        _fractal_state->hp_coords.zoom = HighPrecisionFloat(state.zoom.to_double(),
            static_cast<mpfr_prec_t>(64));
        _fractal_state->hp_coords.precision_bits = 64;
        _fractal_state->hp_coords.is_valid = true;
    }

    // Upload to GPU
    reference_orbit.upload_to_gpu(_device, _allocator);

    

    fmt::print("Reference orbit computed: {} iterations (double precision)\n",
        state.reference_iterations);
}

// ============================================================================
// Animation System
// ============================================================================

void DeepZoomManager::playZoomPath(const std::vector<ZoomKeyframe>& path) {
    _zoom_path = path;
    _current_keyframe = 0;
    _animation_time = 0.0f;
    state.zoom_animating = !path.empty();
    state.zoom_progress = 0.0f;
}

void DeepZoomManager::zoomTo(const ArbitraryFloat& target_x,
    const ArbitraryFloat& target_y,
    const ArbitraryFloat& target_zoom,
    float duration) {
    std::vector<ZoomKeyframe> path;

    // Start keyframe (current position)
    ZoomKeyframe start;
    start.center_x = state.center_x;
    start.center_y = state.center_y;
    start.zoom = state.zoom;
    start.duration = 0.0f;
    path.push_back(start);

    // End keyframe (target position)
    ZoomKeyframe end;
    end.center_x = target_x;
    end.center_y = target_y;
    end.zoom = target_zoom;
    end.duration = duration;
    path.push_back(end);

    playZoomPath(path);
}

void DeepZoomManager::update_animation(float delta_time) {
    if (_zoom_path.empty() || _current_keyframe >= _zoom_path.size()) {
        state.zoom_animating = false;
        return;
    }

    _animation_time += delta_time;

    ZoomKeyframe& current_kf = _zoom_path[_current_keyframe];

    // Check if we've reached the current keyframe
    if (_animation_time >= current_kf.duration) {
        // Move to this keyframe exactly
        state.center_x = current_kf.center_x;
        state.center_y = current_kf.center_y;
        state.zoom = current_kf.zoom;

        // Move to next keyframe
        _current_keyframe++;
        _animation_time = 0.0f;

        // Recompute reference orbit at new location
        compute_reference_orbit();

        // Check if animation is complete
        if (_current_keyframe >= _zoom_path.size()) {
            state.zoom_animating = false;
            state.zoom_progress = 1.0f;
        }
    }
    else {
        // Interpolate between previous and current keyframe
        float t = _animation_time / current_kf.duration;
        interpolate_to_keyframe(_current_keyframe, t);

        // Update progress
        float total_duration = 0.0f;
        float elapsed_duration = 0.0f;
        for (size_t i = 0; i < _zoom_path.size(); i++) {
            total_duration += _zoom_path[i].duration;
            if (i < _current_keyframe) {
                elapsed_duration += _zoom_path[i].duration;
            }
        }
        elapsed_duration += _animation_time;
        state.zoom_progress = (total_duration > 0.0f) ? (elapsed_duration / total_duration) : 1.0f;
    }
}

void DeepZoomManager::interpolate_to_keyframe(int index, float t) {
    if (index <= 0 || index >= _zoom_path.size()) return;

    ZoomKeyframe& prev = _zoom_path[index - 1];
    ZoomKeyframe& current = _zoom_path[index];

    // Linear interpolation (could use smoothstep or ease functions)
    double prev_cx = prev.center_x.to_double();
    double prev_cy = prev.center_y.to_double();
    double prev_z = prev.zoom.to_double();

    double curr_cx = current.center_x.to_double();
    double curr_cy = current.center_y.to_double();
    double curr_z = current.zoom.to_double();

    // Interpolate in log space for zoom
    double log_prev_z = std::log(prev_z);
    double log_curr_z = std::log(curr_z);
    double log_interp_z = log_prev_z + t * (log_curr_z - log_prev_z);

    state.center_x = ArbitraryFloat(prev_cx + t * (curr_cx - prev_cx));
    state.center_y = ArbitraryFloat(prev_cy + t * (curr_cy - prev_cy));
    state.zoom = ArbitraryFloat(std::exp(log_interp_z));
}

std::string DeepZoomManager::exportCoordinates() const {
    std::ostringstream oss;
    oss << std::scientific << std::setprecision(17);
    oss << "Center X: " << state.center_x.to_double() << "\n";
    oss << "Center Y: " << state.center_y.to_double() << "\n";
    oss << "Zoom: " << state.zoom.to_double() << "\n";
    oss << "Iterations: " << state.max_iterations << "\n";
    return oss.str();
}

// ============================================================================
// DeepZoomPresets Implementation
// ============================================================================

namespace DeepZoomPresets {
    ZoomKeyframe createSeahorseZoom() {
        ZoomKeyframe kf;
        kf.center_x = ArbitraryFloat(-0.743643887037151);
        kf.center_y = ArbitraryFloat(0.13182590420533);
        kf.zoom = ArbitraryFloat(1e-6);
        kf.duration = 5.0f;
        return kf;
    }

    ZoomKeyframe createElephantZoom() {
        ZoomKeyframe kf;
        kf.center_x = ArbitraryFloat(-0.7453526);
        kf.center_y = ArbitraryFloat(0.1133189);
        kf.zoom = ArbitraryFloat(1e-8);
        kf.duration = 7.0f;
        return kf;
    }

    ZoomKeyframe createMiniMandelbrotZoom() {
        ZoomKeyframe kf;
        kf.center_x = ArbitraryFloat(-0.74364990);
        kf.center_y = ArbitraryFloat(0.13188204);
        kf.zoom = ArbitraryFloat(1e-10);
        kf.duration = 10.0f;
        return kf;
    }
}   