#pragma once

#include <vk_types.h>
#include <vector>
#include <vk_descriptors.h>
#include "fractal_state.h"
#include "compute_effect_manager.h"
#include "ui_manager.h"
#include "input_handler.h"
#include "deep_zoom_system.h" 
#include "animation_renderer.h"

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush() {
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)(); //call functors
		}

		deletors.clear();
	}
};
//> framedata
struct FrameData {
	VkSemaphore _swapchainSemaphore;
	VkFence _renderFence;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	DeletionQueue _deletionQueue;
	DescriptorAllocatorGrowable _frameDescriptors;

	VkDescriptorSet _deepZoomDescriptorSet = VK_NULL_HANDLE;
};

constexpr unsigned int FRAME_OVERLAP = 2;
//< framedata

class VulkanEngine {
private:
	// NEW: Add these member variables
	FractalState fractal_state;
	FractalType current_fractal_type = FractalType::Mandelbrot;

	std::unique_ptr<ComputeEffectManager> compute_manager;
	std::unique_ptr<UIManager> ui_manager;
	std::unique_ptr<InputHandler> input_handler;

	// NEW: Dirty flag for optimization
	bool state_dirty = true;

	std::unique_ptr<AnimationSystem> animation_system;
	std::unique_ptr<AnimationRenderer> animation_renderer;

	ReferenceOrbitBuffer reference_orbit;  // ← ADD THIS
	VkDescriptorSetLayout _deepZoomDescriptorLayout;  // ← ADD THIS
	VkDescriptorSet _deepZoomDescriptorSet;  // ← ADD THIS



public:

	bool _isInitialized{ false };
	int _frameNumber{ 0 };

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	std::vector<VkSemaphore> _renderSemaphores;

	//> inst_init
	VkInstance _instance;// Vulkan library handle
	VkDebugUtilsMessengerEXT _debug_messenger;// Vulkan debug output handle
	VkPhysicalDevice _chosenGPU;// GPU chosen as the default device
	VkDevice _device; // Vulkan device for commands
	VkSurfaceKHR _surface;// Vulkan window surface
	//< inst_init

	//> queues
	FrameData _frames[FRAME_OVERLAP];

	FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; };

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;
	//< queues

	//> swap_init
	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;

	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;
	//< swap_init


	std::unique_ptr<DeepZoomManager> deep_zoom_manager;

	
	
	VmaAllocator _allocator;

	AllocatedImage _drawImage;
	AllocatedImage _depthImage;

	VkExtent2D _drawExtent;

	// DescriptorAllocator globalDescriptorAllocator;

	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;



	// immediate submit structures
	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;

	
	int currentBackgroundEffect{ 0 };

;	bool resize_requested{ false };

	bool _drawImageNeedsTransition = true;  // Add this
	VkImageLayout _drawImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;


    
	// Async compute queue
	VkQueue _computeQueue;
	uint32_t _computeQueueFamily;
	VkCommandPool _computeCommandPool;
	VkCommandBuffer _computeCommandBuffers[3]; // One per resolution
	VkFence _computeFences[3];
	VkSemaphore _computeSemaphores[3];

	
	// Resize management
	
	std::chrono::steady_clock::time_point last_resize_time;
	static constexpr auto RESIZE_DEBOUNCE_MS = std::chrono::milliseconds(100);



	;

	//< init_data

	VulkanEngine();

	~VulkanEngine();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	
	void draw_minimap();
	void draw_status_bar();
	void draw_loading_screen(const char* message, float progress);

	
	

	void apply_ui_theme();

	void setup_callbacks();
	void verify_push_constant_layout();

	
	void handle_zoom(bool zoom_in);
	void zoom_to_point(int mouse_x, int mouse_y, bool zoom_in);
	
	void handle_continuous_input(const uint8_t* keyState, float deltaTime);
	
	void save_screenshot();
	void export_print_quality(uint32_t width, uint32_t height, bool supersample = false);  // NEW



	void toggle_fullscreen();

	void debug_print_fractal_state()
	{
		fmt::print("\n===== FRACTAL STATE DEBUG =====\n");
		fmt::print("center_x: {}\n", fractal_state.center_x);
		fmt::print("center_y: {}\n", fractal_state.center_y);
		fmt::print("zoom: {}\n", fractal_state.zoom);
		fmt::print("max_iterations: {}\n", fractal_state.max_iterations);
		fmt::print("color_offset: {}\n", fractal_state.color_offset);
		fmt::print("color_scale: {}\n", fractal_state.color_scale);
		fmt::print("bailout: {}\n", fractal_state.bailout);
		fmt::print("palette_mode: {}\n", fractal_state.palette_mode);
		fmt::print("aa_samples: {}\n", fractal_state.antialiasing_samples);
		fmt::print("interior_style: {}\n", fractal_state.interior_style);
		fmt::print("orbit_trap_enabled: {}\n", fractal_state.orbit_trap_enabled);
		fmt::print("orbit_trap_radius: {}\n", fractal_state.orbit_trap_radius);
		fmt::print("==============================\n\n");
	};

	//draw loop
	void draw();

	void draw_background(VkCommandBuffer cmd);
	

	void init_descriptors();

	void init_pipelines();
	
	
	float renderScale = 1.f;

	DescriptorAllocatorGrowable globalDescriptorAllocator;

	DeletionQueue _mainDeletionQueue;

	bool render_animation_frame(
		const FractalState& state,
		uint32_t width,
		uint32_t height,
		const std::string& output_path
	);
	

	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	
	


	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	void destroy_image(const AllocatedImage& img);


	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void destroy_buffer(const AllocatedBuffer& buffer);

	//run main loop
	void run();

	bool stop_rendering{ false };
private:

	void init_vulkan();

	void init_swapchain();

	void create_swapchain(uint32_t width, uint32_t height);
	void resize_swapchain();
	void destroy_swapchain();

	void update_deep_zoom_descriptors();

	void prepare_deep_zoom_rendering();

	void init_commands();

	void init_sync_structures();

	void init_imgui();

	
};
