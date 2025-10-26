#include "animation_system.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp> 

AnimationSystem::AnimationSystem(FractalState& state)
    : fractal_state(state) {
    animation.duration = 10.0f;  // Default 10 seconds
}

void AnimationSystem::add_keyframe(float time, const FractalState& state) {
    animation.keyframes.emplace_back(time, state);

    // Keep sorted by time
    std::sort(animation.keyframes.begin(), animation.keyframes.end(),
        [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });

    // Update duration
    if (time > animation.duration) {
        animation.duration = time + 1.0f;
    }
}

void AnimationSystem::remove_keyframe(size_t index) {
    if (index < animation.keyframes.size()) {
        animation.keyframes.erase(animation.keyframes.begin() + index);
    }
}

void AnimationSystem::update_keyframe(size_t index, const FractalState& state) {
    if (index < animation.keyframes.size()) {
        animation.keyframes[index].state = state;
        // Keep sorted by time (in case time changed)
        std::sort(animation.keyframes.begin(), animation.keyframes.end(),
            [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
    }
}

void AnimationSystem::play() {
    if (animation.keyframes.size() < 2) return;
    playing = true;
}

void AnimationSystem::pause() {
    playing = false;
}

void AnimationSystem::stop() {
    playing = false;
    current_time = 0.0f;
}

void AnimationSystem::seek(float time) {
    current_time = std::clamp(time, 0.0f, animation.duration);
    if (!playing && animation.keyframes.size() >= 2) {
        fractal_state = interpolate(current_time);
        fractal_state.mark_dirty();
    }
}

void AnimationSystem::update(float delta_time) {
    if (!playing || animation.keyframes.size() < 2) return;

    current_time += delta_time;

    if (current_time >= animation.duration) {
        if (animation.loop) {
            current_time = fmod(current_time, animation.duration);
        }
        else {
            current_time = animation.duration;
            playing = false;
        }
    }

    // Update fractal state with interpolated values
    fractal_state = interpolate(current_time);
    fractal_state.mark_dirty();
}

FractalState AnimationSystem::interpolate(float time) const {
    if (animation.keyframes.empty()) return fractal_state;
    if (animation.keyframes.size() == 1) return animation.keyframes[0].state;

    // Clamp time
    time = std::clamp(time, 0.0f, animation.duration);

    // Find surrounding keyframes
    size_t k1 = 0, k2 = 1;
    find_keyframe_pair(time, k1, k2);

    const Keyframe& key1 = animation.keyframes[k1];
    const Keyframe& key2 = animation.keyframes[k2];

    // ← ADD THIS SAFETY CHECK
    float time_diff = key2.time - key1.time;
    if (time_diff < 0.001f) {
        // Keyframes too close together, just return first one
        return key1.state;
    }

    // Calculate interpolation factor (0 to 1)
    float t = (time - key1.time) / time_diff;  // Now safe from divide by zero

    // Apply easing
    switch (key2.interp_type) {
    case InterpolationType::EaseInOut:
        t = ease_in_out(t);
        break;
    case InterpolationType::EaseIn:
        t = ease_in(t);
        break;
    case InterpolationType::EaseOut:
        t = ease_out(t);
        break;
    case InterpolationType::Exponential:
        t = t * t;
        break;
    default:
        break;
    }

    // Interpolate all state values
    FractalState result;

    // Positions (linear interpolation)
    result.center_x = key1.state.center_x + t * (key2.state.center_x - key1.state.center_x);
    result.center_y = key1.state.center_y + t * (key2.state.center_y - key1.state.center_y);

    // ← FIX THE ZOOM INTERPOLATION
    // Zoom (exponential interpolation for smooth feel)
    // Add safety checks for log()
    if (key1.state.zoom > 0.0 && key2.state.zoom > 0.0) {
        double log_zoom1 = std::log(key1.state.zoom);
        double log_zoom2 = std::log(key2.state.zoom);
        result.zoom = std::exp(log_zoom1 + t * (log_zoom2 - log_zoom1));
    }
    else {
        // Fallback to linear interpolation if zoom values are invalid
        result.zoom = key1.state.zoom + t * (key2.state.zoom - key1.state.zoom);
    }

    // Ensure zoom is always positive and reasonable
    result.zoom = std::max(0.000001, result.zoom);

    float iter_t = t;
    if (t < 0.33f) {
        iter_t = 0.0f;  // Use first keyframe's iterations
    }
    else if (t < 0.67f) {
        iter_t = 0.5f;  // Use middle value
    }
    else {
        iter_t = 1.0f;  // Use second keyframe's iterations
    }

    // Rest of interpolation...
    result.max_iterations = static_cast<int>(
        key1.state.max_iterations + iter_t * (key2.state.max_iterations - key1.state.max_iterations)
        );

    result.color_offset = key1.state.color_offset + t * (key2.state.color_offset - key1.state.color_offset);
    result.color_scale = key1.state.color_scale + t * (key2.state.color_scale - key1.state.color_scale);
    result.color_brightness = key1.state.color_brightness + t * (key2.state.color_brightness - key1.state.color_brightness);
    result.color_saturation = key1.state.color_saturation + t * (key2.state.color_saturation - key1.state.color_saturation);
    result.color_contrast = key1.state.color_contrast + t * (key2.state.color_contrast - key1.state.color_contrast);

    result.palette_mode = (t < 0.5f) ? key1.state.palette_mode : key2.state.palette_mode;

    result.rotation_y = key1.state.rotation_y + t * (key2.state.rotation_y - key1.state.rotation_y);
    result.camera_distance = key1.state.camera_distance + t * (key2.state.camera_distance - key1.state.camera_distance);
    result.mandelbulb_power = key1.state.mandelbulb_power + t * (key2.state.mandelbulb_power - key1.state.mandelbulb_power);

    result.bailout = key1.state.bailout;
    result.antialiasing_samples = key1.state.antialiasing_samples;
    result.orbit_trap_enabled = key1.state.orbit_trap_enabled;
    result.orbit_trap_radius = key1.state.orbit_trap_radius;

    return result;
}

size_t AnimationSystem::find_keyframe_pair(float time, size_t& k1, size_t& k2) const {
    // Find the two keyframes surrounding the given time
    for (size_t i = 0; i < animation.keyframes.size() - 1; i++) {
        if (time >= animation.keyframes[i].time && time <= animation.keyframes[i + 1].time) {
            k1 = i;
            k2 = i + 1;
            return k1;
        }
    }

    // Default to last pair
    k1 = animation.keyframes.size() - 2;
    k2 = animation.keyframes.size() - 1;
    return k1;
}

// Easing functions (smooth interpolation)
float AnimationSystem::ease_in_out(float t) const {
    return t < 0.5f
        ? 2.0f * t * t
        : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

float AnimationSystem::ease_in(float t) const {
    return t * t;
}

float AnimationSystem::ease_out(float t) const {
    return 1.0f - (1.0f - t) * (1.0f - t);
}

void AnimationSystem::clear_keyframes() {
    animation.keyframes.clear();
    animation.duration = 0.0f;
    stop();
}

// Fix the save function to include all fields
bool AnimationSystem::save_to_file(const std::string& filename) const {
    try {
        nlohmann::json j;
        j["name"] = animation.name;
        j["description"] = animation.description;
        j["duration"] = animation.duration;
        j["loop"] = animation.loop;
        j["target_fps"] = animation.target_fps;
        j["export_width"] = animation.export_width;
        j["export_height"] = animation.export_height;

        j["keyframes"] = nlohmann::json::array();
        for (const auto& kf : animation.keyframes) {
            nlohmann::json kf_json;
            kf_json["time"] = kf.time;
            kf_json["interp_type"] = (int)kf.interp_type;

            // Complete state serialization
            kf_json["center_x"] = kf.state.center_x;
            kf_json["center_y"] = kf.state.center_y;
            kf_json["zoom"] = kf.state.zoom;
            kf_json["max_iterations"] = kf.state.max_iterations;
            kf_json["palette_mode"] = kf.state.palette_mode;
            kf_json["color_offset"] = kf.state.color_offset;
            kf_json["color_scale"] = kf.state.color_scale;
            kf_json["color_brightness"] = kf.state.color_brightness;
            kf_json["color_saturation"] = kf.state.color_saturation;
            kf_json["color_contrast"] = kf.state.color_contrast;
            kf_json["rotation_y"] = kf.state.rotation_y;
            kf_json["camera_distance"] = kf.state.camera_distance;
            kf_json["mandelbulb_power"] = kf.state.mandelbulb_power;
            kf_json["bailout"] = kf.state.bailout;
            kf_json["antialiasing_samples"] = kf.state.antialiasing_samples;
            kf_json["orbit_trap_enabled"] = kf.state.orbit_trap_enabled;
            kf_json["orbit_trap_radius"] = kf.state.orbit_trap_radius;

            j["keyframes"].push_back(kf_json);
        }

        std::ofstream file(filename);
        if (!file.is_open()) {
            fmt::print("Failed to open file for writing: {}\n", filename);
            return false;
        }
        file << j.dump(4);
        fmt::print("Animation saved to: {}\n", filename);
        return true;
    }
    catch (const std::exception& e) {
        fmt::print("Failed to save animation: {}\n", e.what());
        return false;
    }
}

bool AnimationSystem::load_from_file(const std::string& filename) {
    try {
        std::ifstream file(filename);
        nlohmann::json j = nlohmann::json::parse(file);

        animation.name = j["name"];
        animation.description = j["description"];
        animation.duration = j["duration"];
        animation.loop = j["loop"];
        animation.target_fps = j["target_fps"];
        animation.export_width = j["export_width"];
        animation.export_height = j["export_height"];

        animation.keyframes.clear();
        for (const auto& kf_json : j["keyframes"]) {
            FractalState state;
            state.center_x = kf_json["center_x"];
            state.center_y = kf_json["center_y"];
            state.zoom = kf_json["zoom"];
            state.max_iterations = kf_json["max_iterations"];
            state.palette_mode = kf_json["palette_mode"];
            state.color_offset = kf_json["color_offset"];
            state.color_scale = kf_json["color_scale"];
            // ... load other state fields ...

            Keyframe kf(kf_json["time"], state);
            kf.interp_type = (InterpolationType)kf_json["interp_type"];

            animation.keyframes.push_back(kf);
        }

        fmt::print("Animation loaded from: {}\n", filename);
        return true;
    }
    catch (const std::exception& e) {
        fmt::print("Failed to load animation: {}\n", e.what());
        return false;
    }
}