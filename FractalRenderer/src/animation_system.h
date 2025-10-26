#pragma once
#include "fractal_state.h"
#include <vector>
#include <string>
#include <memory>
#include <fmt/core.h>

enum class InterpolationType {
    Linear,
    EaseInOut,      // Smooth start and end
    EaseIn,         // Slow start
    EaseOut,        // Slow end
    Exponential     // For zoom (feels more natural)
};

struct Keyframe {
    float time;              // Time in seconds
    FractalState state;      // Complete fractal state at this time
    InterpolationType interp_type = InterpolationType::EaseInOut;

    Keyframe(float t, const FractalState& s) : time(t), state(s) {}
};

struct Animation {
    std::string name;
    std::vector<Keyframe> keyframes;
    float duration;          // Total duration in seconds
    bool loop = false;

    // Metadata
    std::string description;
    int target_fps = 60;
    int export_width = 1920;
    int export_height = 1080;
};

class AnimationSystem {
public:
    AnimationSystem(FractalState& state);

    // Keyframe management
    void add_keyframe(float time, const FractalState& state);
    void remove_keyframe(size_t index);
    void update_keyframe(size_t index, const FractalState& state);
    void clear_keyframes();

    // Playback
    void play();
    void pause();
    void stop();
    void seek(float time);

    // Update (call every frame)
    void update(float delta_time);

    // Interpolation
    FractalState interpolate(float time) const;

    // Save/Load
    bool save_to_file(const std::string& filename) const;
    bool load_from_file(const std::string& filename);

    // Getters
    bool is_playing() const { return playing; }
    float get_current_time() const { return current_time; }
    float get_duration() const { return animation.duration; }
    const std::vector<Keyframe>& get_keyframes() const { return animation.keyframes; }

    Animation& get_animation() { return animation; }

private:
    FractalState& fractal_state;  // Reference to main state
    Animation animation;

    bool playing = false;
    float current_time = 0.0f;

    // Helper functions
    size_t find_keyframe_pair(float time, size_t& k1, size_t& k2) const;
    float ease_in_out(float t) const;
    float ease_in(float t) const;
    float ease_out(float t) const;
};
