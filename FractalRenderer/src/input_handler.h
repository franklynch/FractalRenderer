#pragma once
#include <SDL.h>
#include <functional>
#include <chrono>
#include <imgui.h>
#include "fractal_state.h"

class InputHandler {
public:
    // Callbacks for various actions
    std::function<void(bool zoom_in)> on_zoom;
    std::function<void(int x, int y, bool zoom_in)> on_zoom_to_point;
    std::function<void()> on_reset_view;
    std::function<void()> on_save_screenshot;
    std::function<void()> on_toggle_fullscreen;
    std::function<void()> on_toggle_ui;
    std::function<void()> on_toggle_help;
    std::function<void(FractalType& current)> on_switch_fractal;
    std::function<void(int delta)> on_adjust_iterations;
    std::function<void(int palette)> on_set_palette;
    std::function<void()> on_screenshot;
    std::function<void()> on_fullscreen_toggle;
    // std::function<void(int preset_index)> on_deep_zoom_preset; 
    std::function<void(int)> on_deep_zoom_preset;// ← ADD THIS

    InputHandler(FractalState& state, int window_width, int window_height)
        : state(state), window_width(window_width), window_height(window_height) {
    }

    void update_window_size(int w, int h) {
        window_width = w;
        window_height = h;
    }

    // Process SDL events
    bool process_event(const SDL_Event& e, FractalType current_type);

    // Process continuous input (WASD, etc.)
    void process_continuous_input(const uint8_t* key_state, float delta_time);

private:
    FractalState& state;
    int window_width;
    int window_height;

    // Double-click detection
    std::chrono::steady_clock::time_point last_click_time;
    int last_click_x = 0;
    int last_click_y = 0;

    void handle_mouse_wheel(const SDL_MouseWheelEvent& wheel, FractalType current_type);
    void handle_mouse_button_down(const SDL_MouseButtonEvent& button);
  //  void handle_mouse_button_up(const SDL_MouseButtonEvent& button);
    void handle_mouse_motion(const SDL_MouseMotionEvent& motion, FractalType current_type);
    void handle_keypress(SDL_Keycode key, FractalType& current_type);
};

// Implementation
inline bool InputHandler::process_event(const SDL_Event& e, FractalType current_type) {
    // Returns true if should quit

    switch (e.type) {
    case SDL_QUIT:
        return true;

    case SDL_MOUSEWHEEL:
        if (!ImGui::GetIO().WantCaptureMouse) {
            handle_mouse_wheel(e.wheel, current_type);
        }
        break;

    case SDL_MOUSEBUTTONDOWN:
        if (!ImGui::GetIO().WantCaptureMouse) {
            handle_mouse_button_down(e.button);
        }
        break;

    case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_LEFT) {
            state.is_dragging = false;
        }
        break;

    case SDL_MOUSEMOTION:
        if (state.is_dragging && !ImGui::GetIO().WantCaptureMouse) {
            handle_mouse_motion(e.motion, current_type);
        }
        break;

    case SDL_KEYDOWN:
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            FractalType temp = current_type;
            handle_keypress(e.key.keysym.sym, temp);
        }
        break;
    }

    return false;
}

inline void InputHandler::handle_mouse_wheel(const SDL_MouseWheelEvent& wheel, FractalType current_type) {
    if (current_type == FractalType::Mandelbulb) {
        // Zoom camera for 3D
        float zoom_factor = (wheel.y > 0) ? 0.9f : 1.1f;
        state.camera_distance *= zoom_factor;
        state.camera_distance = glm::clamp(state.camera_distance, 1.0f, 10.0f);
    }
    else {
        // 2D zoom
        if (on_zoom) on_zoom(wheel.y > 0);
    }
    state.mark_dirty();
}

inline void InputHandler::handle_mouse_button_down(const SDL_MouseButtonEvent& button) {
    if (button.button == SDL_BUTTON_LEFT) {
        auto now = std::chrono::steady_clock::now();
        auto time_since = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_click_time);

        // Double-click detection (300ms window, 5px tolerance)
        if (time_since.count() < 300 &&
            abs(button.x - last_click_x) < 5 &&
            abs(button.y - last_click_y) < 5) {
            if (on_zoom_to_point) on_zoom_to_point(button.x, button.y, true);
        }
        else {
            // Start dragging
            state.is_dragging = true;
            state.last_mouse_x = button.x;
            state.last_mouse_y = button.y;
        }

        last_click_time = now;
        last_click_x = button.x;
        last_click_y = button.y;
    }
    else if (button.button == SDL_BUTTON_RIGHT) {
        if (on_zoom_to_point) on_zoom_to_point(button.x, button.y, false);
    }
}

inline void InputHandler::handle_mouse_motion(const SDL_MouseMotionEvent& motion, FractalType current_type) {
    int dx = motion.x - state.last_mouse_x;
    int dy = motion.y - state.last_mouse_y;

    if (current_type == FractalType::Mandelbulb) {
        // Rotate for 3D
        state.rotation_y += dx * 0.01f;
    }
    else {
        // Pan for 2D
        if (window_width > 0 && window_height > 0) {
            float aspect = static_cast<float>(window_width) / static_cast<float>(window_height);
            state.center_x -= (dx / static_cast<float>(window_width)) * state.zoom * aspect;
            state.center_y -= (dy / static_cast<float>(window_height)) * state.zoom;
        }
    }

    state.last_mouse_x = motion.x;
    state.last_mouse_y = motion.y;
    state.mark_dirty();
}

inline void InputHandler::handle_keypress(SDL_Keycode key, FractalType& current_type) {
    switch (key) {
    case SDLK_r:
        if (on_reset_view) on_reset_view();
        break;
    case SDLK_TAB:
        if (on_switch_fractal) on_switch_fractal(current_type);
        break;
    case SDLK_ESCAPE:
        // Handle in main loop
        break;
    case SDLK_s:
        if (on_screenshot) on_screenshot();
        break;
    case SDLK_SPACE:
        if (on_toggle_ui) on_toggle_ui();
        break;
    case SDLK_h:
        if (on_toggle_help) on_toggle_help();
        break;
    case SDLK_f:
        if (on_fullscreen_toggle) on_fullscreen_toggle();
        break;
    case SDLK_LEFTBRACKET:
        if (on_adjust_iterations) on_adjust_iterations(-64);
        break;
    case SDLK_RIGHTBRACKET:
        if (on_adjust_iterations) on_adjust_iterations(64);
        break;
    case SDLK_1: case SDLK_2: case SDLK_3:
    case SDLK_4: case SDLK_5: case SDLK_6:
        if (on_set_palette) on_set_palette(key - SDLK_1);
        break;  // ← FIXED: Added missing break
    case SDLK_z:
        if (on_deep_zoom_preset) on_deep_zoom_preset(0); // Seahorse
        break;
    case SDLK_x:
        if (on_deep_zoom_preset) on_deep_zoom_preset(1); // Elephant
        break;
    case SDLK_c:
        if (on_deep_zoom_preset) on_deep_zoom_preset(2); // Mini Mandelbrot
        break;

    }
}

inline void InputHandler::process_continuous_input(const uint8_t* key_state, float delta_time) {
    if (window_width == 0 || window_height == 0) return;

    float pan_speed = static_cast<float>(state.zoom * delta_time * 2.0);
    float aspect = static_cast<float>(window_width) / static_cast<float>(window_height);

    bool moved = false;

    // WASD / Arrow keys
    if (key_state[SDL_SCANCODE_W] || key_state[SDL_SCANCODE_UP]) {
        state.center_y -= pan_speed;
        moved = true;
    }
    if (key_state[SDL_SCANCODE_S] || key_state[SDL_SCANCODE_DOWN]) {
        state.center_y += pan_speed;
        moved = true;
    }
    if (key_state[SDL_SCANCODE_A] || key_state[SDL_SCANCODE_LEFT]) {
        state.center_x -= pan_speed * aspect;
        moved = true;
    }
    if (key_state[SDL_SCANCODE_D] || key_state[SDL_SCANCODE_RIGHT]) {
        state.center_x += pan_speed * aspect;
        moved = true;
    }

    // Q/E zoom
    if (key_state[SDL_SCANCODE_Q]) {
        if (on_zoom) on_zoom(false);
        moved = true;
    }
    if (key_state[SDL_SCANCODE_E]) {
        if (on_zoom) on_zoom(true);
        moved = true;
    }

    if (moved) state.mark_dirty();
}