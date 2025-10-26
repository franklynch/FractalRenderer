#pragma once
#include <glm/glm.hpp>

// Centralized fractal state management
enum class FractalType {
    Mandelbrot = 0,
    JuliaSet,  
    BurningShip, 
    Mandelbulb, 
    Phoenix, 
    Deep_Zoom, 
    Count
};

struct FractalState {
    // View parameters
    double center_x = -0.5;
    double center_y = 0.0;
    double zoom = 3.0;
    int max_iterations = 256;

    // Camera (3D)
    float camera_distance = 3.0f;
    float rotation_y = 0.0f;
    float fov = 1.0f;

    // Julia parameters
    float julia_c_real = -0.7f;
    float julia_c_imag = 0.27015f;

    // Mandelbulb parameters
    float mandelbulb_power = 8.0f;

    // Rendering
    float bailout = 4.0f;
    int antialiasing_samples = 1;

    // Coloring
    int palette_mode = 0;
    float color_offset = 0.0f;
    float color_scale = 1.0f;
    bool animate_colors = false;
    float animation_speed = 1.0f;

    // Advanced effects
    int interior_style = 0;
    bool orbit_trap_enabled = false;
    float orbit_trap_radius = 0.5f;
    bool stripe_enabled = false;
    float stripe_density = 10.0f;
    bool lighting_enabled = false;

    // UI state
    bool show_ui = true;
    bool show_help = false;
    bool show_minimap = true;
    bool show_status_bar = true;

    // Interaction
    bool is_dragging = false;
    int last_mouse_x = 0;
    int last_mouse_y = 0;
    bool is_rendering = false;

    // Animation
    bool auto_rotate = false;
    float rotation_speed = 0.5f;
    bool animate_julia = false;

    // Dirty flag for optimization
    bool needs_update = true;
    void mark_dirty() { needs_update = true; }
    void clear_dirty() { needs_update = false; }

    // Color enhancement
    float color_brightness = 1.0f;
    float color_saturation = 1.0f;
    float color_contrast = 1.0f;

    // Phoenix-specific parameters
    float phoenix_p = 0.0f;          // Damping parameter
    float phoenix_r = -0.5f;         // Feedback/memory parameter
    bool use_julia_set = false;      // Toggle Julia mode
    // float stripe_density = 0.0f;     // Flow stripe visualization (0 = off)

    // Deep Zoom parameters
    bool use_perturbation = false;         // Enable perturbation theory
    int reference_iterations = 0;          // Reference orbit length
    bool use_series_approximation = false; // Series optimization
    int series_order = 3;                  // Series approximation order
    int samples_per_pixel = 1;             // Supersampling (1, 2, or 4)

    

    // Reset to default view
    void reset() {
        // Reset to default Mandelbrot view
        center_x = -0.5;
        center_y = 0.0;
        zoom = 1.5;
        max_iterations = 256;
        camera_distance = 3.0f;
        rotation_y = 0.0f;

        // Reset color enhancement
        color_brightness = 1.0f;
        color_saturation = 1.0f;
        color_contrast = 1.0f;

        mark_dirty();
    }

    // Helper: Get fractal type name
    static const char* get_name(FractalType type) {
        static const char* names[] = {
			"Mandelbrot", "Julia Set", "Burning Ship", "Mandelbulb", "Phoenix", "Deep_Zoom"
        };
        return names[static_cast<int>(type)];
    }
};

// Preset locations for easy navigation
struct Preset {
    const char* name;
    FractalType type;
    double center_x, center_y, zoom;
    int iterations;
};
namespace Presets {
    inline const Preset MANDELBROT_PRESETS[] = {
        // Wide overview first, then zoom into classic locations
        {"Overview", FractalType::Mandelbrot, -0.5, 0.0, 2.5, 256},
        {"Seahorse Valley", FractalType::Mandelbrot, -0.743643887037151, 0.13182590420533, 0.008, 1024},
        {"Elephant Valley", FractalType::Mandelbrot, 0.257, 0.0, 0.015, 768},
        {"Triple Spiral", FractalType::Mandelbrot, -0.088, 0.654, 0.02, 512},
        {"Mini Mandelbrot", FractalType::Mandelbrot, -1.7497, 0.00001, 0.0005, 1024},
        {"Spiral Galaxy", FractalType::Mandelbrot, -0.7453, 0.1127, 0.01, 768}
    };

    inline const Preset BURNING_SHIP_PRESETS[] = {
        {"The Main Ship", FractalType::BurningShip, -0.5, -0.6, 2.0, 256},        // Wide overview
        {"The Bow", FractalType::BurningShip, -1.755, -0.03, 0.02, 768},          // Detailed bow structure
        {"Ship Antenna", FractalType::BurningShip, -1.7497, -0.0375, 0.005, 1024}, // Deep antenna detail
        {"Crystal Cavern", FractalType::BurningShip, -1.7540, -0.0280, 0.015, 768}, // Crystal formations
        {"Deep Tendrils", FractalType::BurningShip, -1.749, 0.0, 0.001, 1536}     // Ultra-deep zoom
    };
}