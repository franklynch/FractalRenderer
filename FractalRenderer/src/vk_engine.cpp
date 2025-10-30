
#define _CRT_SECURE_NO_WARNINGS

#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <string>

#include <vk_types.h>
#include <vk_initializers.h>
#include <vk_images.h>

#include "VkBootstrap.h"
#include <array>
#include <thread>
#include <chrono>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "vk_pipelines.h"
#include "vk_descriptors.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <png.h>

#include "animation_system.h"

#include "video_encoder.h"


constexpr bool bUseValidationLayers = true;

VulkanEngine::VulkanEngine() {
	// Can be empty - default initialization is fine
}

VulkanEngine::~VulkanEngine() {
	// Cleanup is handled by unique_ptr automatically
}

void VulkanEngine::init()
{
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	_window = SDL_CreateWindow(
		"Fractal Viewer",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);

	init_vulkan();
	init_swapchain();
	init_commands();
	init_sync_structures();
	init_descriptors();
	update_deep_zoom_descriptors();

	fmt::print("Deep zoom descriptor layout created: {}\n",
		_deepZoomDescriptorLayout != VK_NULL_HANDLE);
	fmt::print("Deep zoom descriptor set allocated: {}\n",
		_deepZoomDescriptorSet != VK_NULL_HANDLE);
	fmt::print("Reference orbit buffer initialized: {}\n",
		reference_orbit.gpu_buffer.buffer != VK_NULL_HANDLE);

	init_pipelines();
	init_imgui();

	// ✨ NEW: Initialize modular subsystems
	compute_manager = std::make_unique<ComputeEffectManager>(
		_device,
		_drawImageDescriptorLayout,      // Regular layout
		_deepZoomDescriptorLayout        // Deep zoom layout
	);
	compute_manager->init_pipelines();

	ui_manager = std::make_unique<UIManager>(fractal_state);
	ui_manager->apply_theme();

	input_handler = std::make_unique<InputHandler>(fractal_state, _windowExtent.width, _windowExtent.height);

	deep_zoom_manager = std::make_unique<DeepZoomManager>(_device, _allocator);
	deep_zoom_manager->initialize();  // CORRECT - using arrow operator

	deep_zoom_manager->set_fractal_state(&fractal_state);

	animation_system = std::make_unique<AnimationSystem>(fractal_state);

	animation_renderer = std::make_unique<AnimationRenderer>(
		_device, _allocator, _drawImageDescriptorLayout
	);

	// Set up render callback
	animation_renderer->render_frame_callback = [this](
		const FractalState& state,
		uint32_t width,
		uint32_t height,
		const std::string& path
		) {
			return this->render_animation_frame(state, width, height, path);
		};

	setup_callbacks();

	if (!VideoEncoder::is_ffmpeg_available()) {
		fmt::print("\n WARNING: FFmpeg not found!\n");
		fmt::print("Video encoding will not be available.\n");
		fmt::print("Please install FFmpeg from https://ffmpeg.org/\n\n");
	}
	else {
		fmt::print("✓ FFmpeg found: {}\n", VideoEncoder::get_ffmpeg_version());
	}



	_isInitialized = true;
}

void VulkanEngine::destroy_swapchain()
{
	vkDestroySwapchainKHR(_device, _swapchain, nullptr);

	// destroy swapchain resources
	for (int i = 0; i < _swapchainImageViews.size(); i++) {

		vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
	}

	// Clean up render semaphores
	for (VkSemaphore renderSem : _renderSemaphores) {
		vkDestroySemaphore(_device, renderSem, nullptr);
	}

	_renderSemaphores.clear();
}

void VulkanEngine::initialize_deep_zoom()
{
	if (!deep_zoom_manager) {
		return;
	}

	// Wait for GPU
	vkDeviceWaitIdle(_device);

	// Set initial state
	deep_zoom_manager->state.center_x = ArbitraryFloat(fractal_state.center_x);
	deep_zoom_manager->state.center_y = ArbitraryFloat(fractal_state.center_y);
	deep_zoom_manager->state.zoom = ArbitraryFloat(fractal_state.zoom);
	deep_zoom_manager->state.max_iterations = fractal_state.max_iterations;

	// Compute orbit
	deep_zoom_manager->compute_reference_orbit();

	// Copy to rendering buffer
	reference_orbit.cpu_data = deep_zoom_manager->reference_orbit.cpu_data;

	// ✅ NUCLEAR OPTION: Destroy old buffer and force recreation
	if (reference_orbit.gpu_buffer.buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(_allocator, reference_orbit.gpu_buffer.buffer,
			reference_orbit.gpu_buffer.allocation);
		reference_orbit.gpu_buffer.buffer = VK_NULL_HANDLE;
	}

	// Create new buffer with CORRECT size
	size_t buffer_size = reference_orbit.cpu_data.size() * sizeof(glm::vec2);

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = buffer_size;  // ✅ ACTUAL SIZE!
	bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	vmaCreateBuffer(_allocator, &bufferInfo, &allocInfo,
		&reference_orbit.gpu_buffer.buffer,
		&reference_orbit.gpu_buffer.allocation,
		&reference_orbit.gpu_buffer.info);

	// Upload data
	void* data;
	vmaMapMemory(_allocator, reference_orbit.gpu_buffer.allocation, &data);
	memcpy(data, reference_orbit.cpu_data.data(), buffer_size);
	vmaUnmapMemory(_allocator, reference_orbit.gpu_buffer.allocation);

	// Update descriptors
	update_deep_zoom_descriptors();

	// Update state
	fractal_state.reference_iterations = static_cast<int>(reference_orbit.cpu_data.size());

	fmt::print("✅ Deep Zoom initialized: {} orbit points, {} bytes\n",
		reference_orbit.cpu_data.size(), buffer_size);
}



void VulkanEngine::prepare_deep_zoom_rendering()
{
	if (current_fractal_type != FractalType::Deep_Zoom) {
		return;
	}

	bool needs_recompute = false;
	deep_zoom_manager->state.max_iterations = fractal_state.max_iterations;
	bool is_animating = (deep_zoom_manager && deep_zoom_manager->state.zoom_animating);

	if (fractal_state.use_perturbation && !is_animating) {
		if (fractal_state.needs_update || reference_orbit.cpu_data.empty()) {
			needs_recompute = true;
		}
	}

	if (needs_recompute) {
		// Wait for GPU before buffer operations
		vkDeviceWaitIdle(_device);

		// Compute new orbit
		deep_zoom_manager->state.max_iterations = fractal_state.max_iterations;
		deep_zoom_manager->compute_reference_orbit();

		// Copy and upload
		reference_orbit.cpu_data = deep_zoom_manager->reference_orbit.cpu_data;
		reference_orbit.is_dirty = true;  // Mark for upload
		reference_orbit.upload_to_gpu(_device, _allocator);

		// Update descriptors (waits for fences inside)
		update_deep_zoom_descriptors();

		// Update state
		fractal_state.reference_iterations = static_cast<int>(reference_orbit.cpu_data.size());
		fractal_state.clear_dirty();
	}
}

void VulkanEngine::update_deep_zoom_descriptors()
{
	fmt::print("\n🔍 update_deep_zoom_descriptors() called\n");
	fmt::print("  Orbit CPU size: {} elements\n", reference_orbit.cpu_data.size());

	// ✅ Just verify buffer exists
	if (reference_orbit.gpu_buffer.buffer == VK_NULL_HANDLE) {
		fmt::print("  ❌ ERROR: No GPU buffer exists!\n");
		return;
	}

	// Wait for GPU
//	for (int i = 0; i < FRAME_OVERLAP; i++) {
//		vkWaitForFences(_device, 1, &_frames[i]._renderFence, VK_TRUE, UINT64_MAX);
//	}

	fmt::print("  📝 Updating descriptors\n");

	// ✅ Update descriptor sets with existing buffer
	for (int i = 0; i < FRAME_OVERLAP; i++) {
		DescriptorWriter writer;

		writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE,
			VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

		// Use VK_WHOLE_SIZE - let Vulkan use the entire buffer
		writer.write_buffer(1, reference_orbit.gpu_buffer.buffer,
			VK_WHOLE_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

		writer.update_set(_device, _frames[i]._deepZoomDescriptorSet);
	}

	fmt::print("  ✅ Descriptors updated for {} frames\n", FRAME_OVERLAP);
	fmt::print("╚════════════════════════════════════════╝\n\n");
}





void VulkanEngine::cleanup()
{
	if (_isInitialized) {
		vkDeviceWaitIdle(_device);

		// NEW: Clean up subsystems first
		if (compute_manager) {
			compute_manager->cleanup();
			compute_manager.reset();
		}

		deep_zoom_manager.reset();

		ui_manager.reset();
		input_handler.reset();

		// Existing cleanup code...
		for (int i = 0; i < FRAME_OVERLAP; i++) {
			_frames[i]._deletionQueue.flush();
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
			vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
			vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);
		}

		reference_orbit.destroy(_device, _allocator);

		_mainDeletionQueue.flush();
		destroy_swapchain();

		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkDestroyDevice(_device, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::draw_background(VkCommandBuffer cmd)
{
	
	

		float time = static_cast<float>(ImGui::GetTime());

	
	FrameData& currentFrame = get_current_frame();

	int current_frame_idx = _frameNumber % FRAME_OVERLAP;

	VkDescriptorSet descriptor_set = (current_fractal_type == FractalType::Deep_Zoom)
		? currentFrame._deepZoomDescriptorSet
		: _drawImageDescriptors;

	// ═══════════════════════════════════════════════════════════════
	// 🔍 DIAGNOSTIC: Log rendering details
	// ═══════════════════════════════════════════════════════════════
	static uint64_t last_draw_log = 0;
	// bool should_log = (_frameNumber - last_draw_log) > 60 &&
	//	current_fractal_type == FractalType::Deep_Zoom;

	bool should_log = false;

	if (should_log) {
		fmt::print("\n╔════════════════════════════════════════════╗\n");
		fmt::print("║ Frame {:6} - draw_background()        ║\n", _frameNumber);
		fmt::print("╚════════════════════════════════════════════╝\n");
		fmt::print("  Frame index: {}/{}\n", current_frame_idx, FRAME_OVERLAP);
		fmt::print("  Descriptor set: {:p}\n", (void*)descriptor_set);
		fmt::print("  Draw image: {:p}\n", (void*)_drawImage.imageView);
		fmt::print("  Draw extent: {}x{}\n", _drawExtent.width, _drawExtent.height);
		fmt::print("  Image layout: {}\n", (int)_drawImageLayout);

		// Check if descriptor is valid
		if (descriptor_set == VK_NULL_HANDLE) {
			fmt::print("  ⚠️⚠️⚠️ DESCRIPTOR SET IS NULL! ⚠️⚠️⚠️\n");
		}

		// Check if image is valid
		if (_drawImage.imageView == VK_NULL_HANDLE) {
			fmt::print("  ⚠️⚠️⚠️ IMAGE VIEW IS NULL! ⚠️⚠️⚠️\n");
		}

		// Verify image layout is correct (should be GENERAL for compute)
		if (_drawImageLayout != VK_IMAGE_LAYOUT_GENERAL) {
			fmt::print("  ⚠️ WARNING: Image layout is {} (expected {}=GENERAL)\n",
				(int)_drawImageLayout, (int)VK_IMAGE_LAYOUT_GENERAL);
		}

		fmt::print("╚════════════════════════════════════════════╝\n\n");
		last_draw_log = _frameNumber;
	}


	if (current_fractal_type == FractalType::Deep_Zoom) {
		
		fractal_state.reference_iterations = static_cast<int>(reference_orbit.cpu_data.size());
	}

	if (current_fractal_type == FractalType::Deep_Zoom) {
		fmt::print("  🔧 Dispatching deep zoom compute shader...\n");
		fmt::print("     - Orbit points: {}\n", reference_orbit.cpu_data.size());
		fmt::print("     - Center: ({}, {})\n",
			deep_zoom_manager->state.center_x.to_double(),
			deep_zoom_manager->state.center_y.to_double());
		fmt::print("     - Zoom: {}\n", deep_zoom_manager->state.zoom.to_double());

		// Dispatch compute shader here...

		fmt::print("  ✅ Deep zoom compute dispatched\n");
	}

	compute_manager->dispatch(
		cmd,
		current_fractal_type,
		fractal_state,
		time,
		descriptor_set,
		_drawExtent
	);

	// Clear dirty flag after rendering
	fractal_state.clear_dirty();

	
}

void VulkanEngine::verify_push_constant_layout()
{
	fmt::print("\n===== PUSH CONSTANT LAYOUT =====\n");
	fmt::print("sizeof(ComputePushConstants): {} bytes\n", sizeof(ComputePushConstants));
	fmt::print("sizeof(glm::vec4): {} bytes\n", sizeof(glm::vec4));
	fmt::print("Expected total: {} bytes (4 vec4s)\n", sizeof(glm::vec4) * 4);

	ComputePushConstants test;
	test.data1 = glm::vec4(1.0f, 2.0f, 3.0f, 4.0f);
	test.data2 = glm::vec4(5.0f, 6.0f, 7.0f, 8.0f);
	test.data3 = glm::vec4(9.0f, 10.0f, 11.0f, 12.0f);
	test.data4 = glm::vec4(13.0f, 14.0f, 15.0f, 16.0f);

	fmt::print("\nTest values:\n");
	fmt::print("data1: ({}, {}, {}, {})\n", test.data1.x, test.data1.y, test.data1.z, test.data1.w);
	fmt::print("data2: ({}, {}, {}, {})\n", test.data2.x, test.data2.y, test.data2.z, test.data2.w);
	fmt::print("data3: ({}, {}, {}, {})\n", test.data3.x, test.data3.y, test.data3.z, test.data3.w);
	fmt::print("data4: ({}, {}, {}, {})\n", test.data4.x, test.data4.y, test.data4.z, test.data4.w);

	// Check memory layout
	float* ptr = (float*)&test;
	fmt::print("\nMemory layout (as floats):\n");
	for (int i = 0; i < 16; i++) {
		fmt::print("[{}] = {}\n", i, ptr[i]);
	}
	fmt::print("================================\n\n");
}

void VulkanEngine::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
	
	
	
	// Define color attachment with LOAD_OP_LOAD so we draw over the fractal
	VkRenderingAttachmentInfo colorAttachment =
		vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;   // ✅ preserve fractal image
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

	VkRenderingInfo renderInfo = vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);
	renderInfo.flags = 0;

	// ✅ Begin dynamic rendering
	vkCmdBeginRendering(cmd, &renderInfo);

	// ✅ Make sure ImGui uses correct viewport/scissor
	VkViewport viewport = { 0.0f, 0.0f, (float)_swapchainExtent.width, (float)_swapchainExtent.height, 0.0f, 1.0f };
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = { {0, 0}, _swapchainExtent };
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// ✅ Render ImGui UI overlay
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

void VulkanEngine::draw()
{
	
	VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, UINT64_MAX));
	
	

	uint32_t swapchainImageIndex;
	
	VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000,
		get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex);
	
	if (e == VK_ERROR_OUT_OF_DATE_KHR) {
		resize_requested = true;
		return;
	}


	// VK_SUBOPTIMAL_KHR means swapchain still works but isn't optimal
   // Continue rendering but mark for resize after this frame
	if (e == VK_SUBOPTIMAL_KHR) {
		resize_requested = true;
		// Don't return - continue rendering this frame
	}

	if (e != VK_SUCCESS) {
		fmt::print("Failed to acquire swapchain image: {}\n", static_cast<int>(e));
		return;
	}

	// Validate swapchain image index
	if (swapchainImageIndex >= _swapchainImages.size()) {
		fmt::print("Error: Invalid swapchain image index\n");
		return;
	}



	VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));

	get_current_frame()._deletionQueue.flush();
	get_current_frame()._frameDescriptors.clear_pools(_device);

	VK_CHECK(vkResetCommandBuffer(get_current_frame()._mainCommandBuffer, 0));
	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	// CRITICAL: Set _drawExtent BEFORE calling draw_background()
		_drawExtent.width = _drawImage.imageExtent.width;
		_drawExtent.height = _drawImage.imageExtent.height;

	// Transition and render
	
		vkutil::transition_image(cmd, _drawImage.image,
			_drawImageLayout, VK_IMAGE_LAYOUT_GENERAL);
		_drawImageLayout = VK_IMAGE_LAYOUT_GENERAL;

		prepare_deep_zoom_rendering();

		static uint64_t last_layout_log = 0;
		// bool should_log_layout = (_frameNumber - last_layout_log) > 60 &&
		//	current_fractal_type == FractalType::Deep_Zoom;

		bool should_log_layout = false;

		if (should_log_layout) {
			fmt::print("[Frame {}] Before draw_background:\n", _frameNumber);
			fmt::print("  Current layout: {}\n", (int)_drawImageLayout);
			fmt::print("  Image: {:p}\n", (void*)_drawImage.imageView);
			last_layout_log = _frameNumber;
		}


		_drawExtent.width = _drawImage.imageExtent.width;
		_drawExtent.height = _drawImage.imageExtent.height;

		

	draw_background(cmd);  // Now _drawExtent is valid

	VkImage sourceImage = _drawImage.image;
	VkExtent2D sourceExtent = { _drawExtent.width, _drawExtent.height };
	
	

	/// Transition to TRANSFER_SRC for blit
	vkutil::transition_image(cmd, sourceImage, VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	_drawImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;  // 

	// Transition swapchain to TRANSFER_DST
	vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex],
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// Blit to swapchain
	VkImageBlit blitRegion{};
	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.mipLevel = 0;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcOffsets[0] = { 0, 0, 0 };
	blitRegion.srcOffsets[1] = { (int32_t)sourceExtent.width, (int32_t)sourceExtent.height, 1 };
	blitRegion.dstSubresource = blitRegion.srcSubresource;
	blitRegion.dstOffsets[0] = { 0, 0, 0 };
	blitRegion.dstOffsets[1] = { (int32_t)_swapchainExtent.width, (int32_t)_swapchainExtent.height, 1 };

	vkCmdBlitImage(cmd, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		_swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blitRegion, VK_FILTER_LINEAR);

	// Rest of rendering (ImGui, present)
	vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex],
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	draw_imgui(cmd, _swapchainImageViews[swapchainImageIndex]);
	vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex],
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame()._swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(
		VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, _renderSemaphores[swapchainImageIndex]);

	VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);
	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));

	VkPresentInfoKHR presentInfo = vkinit::present_info();
	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &_renderSemaphores[swapchainImageIndex];
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pImageIndices = &swapchainImageIndex;

	VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
	
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
		resize_requested = true;
	}
	else if (presentResult != VK_SUCCESS) {
		fmt::print("Present failed: {}\n", static_cast<int>(presentResult));
		// Don't return - continue to next frame
	}

	_frameNumber++;
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;
	const uint8_t* keyState = SDL_GetKeyboardState(nullptr);
	auto lastFrameTime = std::chrono::high_resolution_clock::now();

	while (!bQuit) {
		auto currentTime = std::chrono::high_resolution_clock::now();
		float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
		lastFrameTime = currentTime;

		// ✨ Update notifications once per frame
		ui_manager->notifications.update(deltaTime);

		// ✨ Update animation
		if (animation_system) {
			animation_system->update(deltaTime);
		}



		if (deep_zoom_manager && current_fractal_type == FractalType::Deep_Zoom) {
			deep_zoom_manager->update(deltaTime);

			// Sync deep zoom state to fractal state
			if (deep_zoom_manager->state.zoom_animating) {
				fractal_state.center_x = deep_zoom_manager->state.center_x.to_double();
				fractal_state.center_y = deep_zoom_manager->state.center_y.to_double();
				fractal_state.zoom = deep_zoom_manager->state.zoom.to_double();
				fractal_state.max_iterations = deep_zoom_manager->state.reference_iterations;
			//	fractal_state.mark_dirty();
			}
		}

		while (SDL_PollEvent(&e) != 0) {
			if (e.type == SDL_QUIT) {
				bQuit = true;
				continue;
			}

			if (e.type == SDL_WINDOWEVENT) {
				if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
					stop_rendering = true;
				}
				else if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
					stop_rendering = false;
				}
			}

			// ✨ ONE line replaces 100+ lines of input handling!
			if (input_handler->process_event(e, current_fractal_type)) {
				bQuit = true;
				break;
			}

			ImGui_ImplSDL2_ProcessEvent(&e);
		}

		if (bQuit) break;

		if (stop_rendering) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		// ✨ Continuous input - one line
		if (!ImGui::GetIO().WantCaptureKeyboard) {
			input_handler->process_continuous_input(keyState, deltaTime);
		}

		// Handle resize
		if (resize_requested) {
			auto now = std::chrono::steady_clock::now();
			if (now - last_resize_time > RESIZE_DEBOUNCE_MS) {
				resize_swapchain();
				last_resize_time = now;
			}
		}



		// ✨ Update animations
		if (fractal_state.auto_rotate) {
			fractal_state.rotation_y += fractal_state.rotation_speed * deltaTime;
			fractal_state.mark_dirty();
		}

		// ImGui frame
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		// ✨ ONE line replaces 300+ line build_ui() function!
		ui_manager->draw_all(
			current_fractal_type,
			ImGui::GetIO().Framerate,
			deep_zoom_manager.get(),
			animation_system.get(),
			animation_renderer.get()  // ← ADD THIS
		);

		ImGui::Render();
		draw();
	}
}

void VulkanEngine::init_vulkan()
{

	vkb::InstanceBuilder builder;

	//make the vulkan instance, with basic debug features
	auto inst_ret = builder.set_app_name("Fractal Viewer")
		.request_validation_layers(bUseValidationLayers)
		.use_default_debug_messenger()
		.require_api_version(1, 3, 0)
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	//grab the instance 
	_instance = vkb_inst.instance;
	_debug_messenger = vkb_inst.debug_messenger;


	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	//vulkan 1.3 features
	VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	features.dynamicRendering = true;
	features.synchronization2 = true;

	//vulkan 1.2 features
	VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.bufferDeviceAddress = true;
	features12.descriptorIndexing = true;


	//use vkbootstrap to select a gpu. 
	//We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 3)
		.set_required_features_13(features)
		.set_required_features_12(features12)
		.set_surface(_surface)
		.select()
		.value();


	//create the final vulkan device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vkb::Device vkbDevice = deviceBuilder.build().value();


	_device = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	auto computeQueue = vkbDevice.get_dedicated_queue(vkb::QueueType::compute);
	if (computeQueue.has_value()) {
		_computeQueue = computeQueue.value();
		_computeQueueFamily = vkbDevice.get_dedicated_queue_index(vkb::QueueType::compute).value();
		fmt::print("Using dedicated compute queue for async rendering\n");
	}
	else {
		// Fall back to graphics queue
		_computeQueue = _graphicsQueue;
		_computeQueueFamily = _graphicsQueueFamily;
		fmt::print("No dedicated compute queue, using graphics queue\n");
	}

	// initialize the memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

	_mainDeletionQueue.push_function([&]() {
		vmaDestroyAllocator(_allocator);
		});

	

}

void VulkanEngine::setup_callbacks()
{
	// ========================================================================
	// Basic View Controls
	// ========================================================================

	auto reset_view_cb = [this]() {
		fractal_state.reset();
		fractal_state.mark_dirty();
		};
	ui_manager->on_reset_view = reset_view_cb;

	auto zoom_cb = [this](bool zoom_in) {
		handle_zoom(zoom_in);
		};
	input_handler->on_zoom = zoom_cb;
	ui_manager->on_zoom = zoom_cb;  // Shared callback

	input_handler->on_zoom_to_point = [this](int x, int y, bool zoom_in) {
		zoom_to_point(x, y, zoom_in);
		};

	// ========================================================================
	// Screenshot & Fullscreen
	// ========================================================================

	auto screenshot_cb = [this]() {
		save_screenshot();
		};
	input_handler->on_screenshot = screenshot_cb;
	ui_manager->on_save_screenshot = screenshot_cb;  // Shared callback

	auto fullscreen_cb = [this]() {
		toggle_fullscreen();
		};
	input_handler->on_fullscreen_toggle = fullscreen_cb;
	ui_manager->on_toggle_fullscreen = fullscreen_cb;  // Shared callback

	// ========================================================================
	// Fractal Type & Presets
	// ========================================================================

	ui_manager->on_fractal_type_changed = [this](FractalType new_type) {
		current_fractal_type = new_type;
		fractal_state.mark_dirty();
		fmt::print("Fractal type changed to: {}\n",
			FractalState::get_name(new_type));

		if (current_fractal_type == FractalType::Deep_Zoom) {
			initialize_deep_zoom();
		}

		// NEW: Initialize deep zoom state when switching to it
		if (new_type == FractalType::Deep_Zoom && deep_zoom_manager) {
			fmt::print("Initializing deep zoom state from fractal state...\n");

			// Transfer current fractal state to deep zoom manager
			deep_zoom_manager->state.center_x = ArbitraryFloat(fractal_state.center_x);
			deep_zoom_manager->state.center_y = ArbitraryFloat(fractal_state.center_y);
			deep_zoom_manager->state.zoom = ArbitraryFloat(fractal_state.zoom);
			deep_zoom_manager->state.max_iterations = fractal_state.max_iterations;

			// Enable perturbation theory for deep zoom
			bool should_use_perturbation = (fractal_state.zoom < 1e-9);
			deep_zoom_manager->state.use_perturbation = should_use_perturbation;
			fractal_state.use_perturbation = should_use_perturbation;

			if (should_use_perturbation) {
				fmt::print("  Perturbation enabled (zoom < 1e-6)\n");
			}
			else {
				fmt::print("  Using high-precision mode (zoom >= 1e-6)\n");
			}

			// ✅ Compute reference orbit ONCE
			deep_zoom_manager->state.max_iterations = fractal_state.max_iterations;
			deep_zoom_manager->compute_reference_orbit();

			// ✅ CRITICAL: Copy CPU data from deep_zoom_manager to reference_orbit
			reference_orbit.cpu_data = deep_zoom_manager->reference_orbit.cpu_data;

			// ✅ Upload to GPU and update descriptors
			reference_orbit.is_dirty = true;
			reference_orbit.upload_to_gpu(_device, _allocator);
			update_deep_zoom_descriptors();

			fmt::print("  Center: ({}, {})\n",
				fractal_state.center_x, fractal_state.center_y);
			fmt::print("  Zoom: {}\n", fractal_state.zoom);
			fmt::print("  Reference orbit: {} points\n",
				reference_orbit.cpu_data.size());
		}

		// NEW: Sync back when leaving Deep_Zoom
		if (current_fractal_type == FractalType::Deep_Zoom &&
			new_type != FractalType::Deep_Zoom &&
			deep_zoom_manager) {
			// Transfer deep zoom state back to fractal state
			fractal_state.center_x = deep_zoom_manager->state.center_x.to_double();
			fractal_state.center_y = deep_zoom_manager->state.center_y.to_double();
			fractal_state.zoom = deep_zoom_manager->state.zoom.to_double();
			fractal_state.use_perturbation = false;  // Disable for regular fractals
			fmt::print("Transferred deep zoom state back to regular fractal\n");
		}

		current_fractal_type = new_type;
		};

	ui_manager->on_apply_preset = [this](const Preset& preset) {
		fractal_state.center_x = preset.center_x;
		fractal_state.center_y = preset.center_y;
		fractal_state.zoom = preset.zoom;
		fractal_state.max_iterations = preset.iterations;
		fractal_state.mark_dirty();
		};

	// ========================================================================
	// High-Resolution Export
	// ========================================================================

	ui_manager->on_export_print = [this](uint32_t width, uint32_t height, bool supersample) {
		export_print_quality(width, height, supersample);
		};

	// ========================================================================
	// Deep Zoom Callbacks
	// ========================================================================

	// Preset zoom callback (for UI buttons)
auto deep_zoom_preset_cb = [this](int preset_index) {
    if (!deep_zoom_manager || current_fractal_type != FractalType::Deep_Zoom) {
        ui_manager->notifications.add(
            "Deep zoom only works with Mandelbrot Deep Zoom fractal!",
            ImVec4(1, 0.5, 0, 1)
        );

        return;
    }
    
	// Select preset
	ZoomKeyframe kf;
	switch (preset_index) {
	case 0: kf = DeepZoomPresets::createSeahorseZoom(); break;
	case 1: kf = DeepZoomPresets::createElephantZoom(); break;
	case 2: kf = DeepZoomPresets::createMiniMandelbrotZoom(); break;
	default: return;
	}

	// Apply preset to deep zoom manager
	deep_zoom_manager->state.center_x = kf.center_x;
	deep_zoom_manager->state.center_y = kf.center_y;
	deep_zoom_manager->state.zoom = kf.zoom;
	deep_zoom_manager->state.max_iterations = fractal_state.max_iterations;
	deep_zoom_manager->state.use_perturbation = true;
	fractal_state.use_perturbation = true;

	// Compute reference orbit
	deep_zoom_manager->compute_reference_orbit();

	// ✅ CRITICAL: Copy CPU data
	reference_orbit.cpu_data = deep_zoom_manager->reference_orbit.cpu_data;

	// ✅ Upload and update descriptors
	reference_orbit.is_dirty = true;
	reference_orbit.upload_to_gpu(_device, _allocator);
	update_deep_zoom_descriptors();

	// ✅ NEW: Update state with actual orbit count for shader!
	fractal_state.reference_iterations = static_cast<int>(reference_orbit.cpu_data.size());

	fmt::print("Reference orbit computed: {} points\n",
		deep_zoom_manager->reference_orbit.cpu_data.size());

	// Sync coordinates to fractal state
	fractal_state.center_x = deep_zoom_manager->state.center_x.to_double();
	fractal_state.center_y = deep_zoom_manager->state.center_y.to_double();
	fractal_state.zoom = deep_zoom_manager->state.zoom.to_double();
	fractal_state.mark_dirty();

	fmt::print("Jumped to preset {} - Center: ({}, {}), Zoom: {:.2e}\n",
		preset_index,
		fractal_state.center_x,
		fractal_state.center_y,
		fractal_state.zoom);
	};



	// Assign to both input handler and UI manager
	input_handler->on_deep_zoom_preset = deep_zoom_preset_cb;
	ui_manager->on_deep_zoom_preset = deep_zoom_preset_cb;

	// Manual deep zoom
	ui_manager->on_deep_zoom_manual = [this](double x, double y, double zoom, float duration) {
		
		if (deep_zoom_manager) {
			// Set the state directly (no animation for now)
			deep_zoom_manager->state.center_x = ArbitraryFloat(x);
			deep_zoom_manager->state.center_y = ArbitraryFloat(y);
			deep_zoom_manager->state.zoom = ArbitraryFloat(zoom);

			// Enable perturbation if needed
			bool needs_perturbation = (zoom < 1e-9);
			deep_zoom_manager->state.use_perturbation = needs_perturbation;
			fractal_state.use_perturbation = needs_perturbation;

			if (needs_perturbation) {
				deep_zoom_manager->state.max_iterations = fractal_state.max_iterations;
				deep_zoom_manager->compute_reference_orbit();
				update_deep_zoom_descriptors();
			}

			// CRITICAL: Sync back to fractal_state
			fractal_state.center_x = x;
			fractal_state.center_y = y;
			fractal_state.zoom = zoom;
			fractal_state.mark_dirty();

			ui_manager->notifications.add(
				"Jumped to coordinates",
				ImVec4(0, 1, 0, 1)
			);
		}

		};

	// Copy coordinates
	ui_manager->on_deep_zoom_copy_coordinates = [this]() {
		if (deep_zoom_manager) {
			std::string coords = deep_zoom_manager->exportCoordinates();
			SDL_SetClipboardText(coords.c_str());
			ui_manager->notifications.add("Coordinates copied to clipboard!",
				ImVec4(0, 1, 0, 1));
		}
		};

	// Deep zoom settings
	ui_manager->on_deep_zoom_use_perturbation = [this](bool enabled) {
		if (deep_zoom_manager) {
			deep_zoom_manager->state.use_perturbation = enabled;
		}
		};

	ui_manager->on_deep_zoom_use_series = [this](bool enabled) {
		if (deep_zoom_manager) {
			deep_zoom_manager->state.use_series_approximation = enabled;
		}
		};

	ui_manager->on_deep_zoom_samples_changed = [this](int samples) {
		if (deep_zoom_manager) {
			deep_zoom_manager->state.samples_per_pixel = samples;
		}
		};

	// ========================================================================
	// Animation Playback Callbacks
	// ========================================================================

	ui_manager->on_animation_play = [this]() {
		if (animation_system) {
			animation_system->play();
		}
		};

	ui_manager->on_animation_pause = [this]() {
		if (animation_system) {
			animation_system->pause();
		}
		};

	ui_manager->on_animation_stop = [this]() {
		if (animation_system) {
			animation_system->stop();
		}
		};

	ui_manager->on_animation_seek = [this](float time) {
		if (animation_system) {
			animation_system->seek(time);
		}
		};

	ui_manager->on_animation_loop_changed = [this](bool loop) {
		if (animation_system) {
			animation_system->get_animation().loop = loop;
		}
		};

	// ========================================================================
	// Keyframe Management Callbacks
	// ========================================================================

	ui_manager->on_keyframe_add = [this](float time, const FractalState& state) {
		if (animation_system) {
			animation_system->add_keyframe(time, state);
			ui_manager->notifications.add("Keyframe added", ImVec4(0, 1, 0, 1));
		}
		};

	ui_manager->on_keyframe_update = [this](size_t index, const FractalState& state) {
		if (animation_system) {
			animation_system->update_keyframe(index, state);
			ui_manager->notifications.add("Keyframe updated", ImVec4(0, 1, 0, 1));
		}
		};

	ui_manager->on_keyframe_delete = [this](size_t index) {
		if (animation_system) {
			animation_system->remove_keyframe(index);
			ui_manager->notifications.add("Keyframe deleted", ImVec4(1, 0.5, 0, 1));
		}
		};

	ui_manager->on_keyframes_clear = [this]() {
		if (animation_system) {
			animation_system->clear_keyframes();
			ui_manager->notifications.add("All keyframes cleared", ImVec4(1, 0.5, 0, 1));
		}
		};

	// ========================================================================
	// Animation Export Callbacks
	// ========================================================================

	ui_manager->on_export_animation = [this](const Animation& animation) {
		animation_renderer->start_render(
			animation,
			animation_system.get(),
			"animation_frames"
		);
		};

	ui_manager->on_render_encode = [this](const Animation& animation,
		const VideoEncodeSettings& settings) {
			if (animation_renderer) {
				animation_renderer->render_and_encode(
					animation,
					animation_system.get(),
					settings,
					"animation_frames"
				);
			}
		};

	ui_manager->on_cancel_render = [this]() {
		if (animation_renderer) {
			animation_renderer->cancel_render();
			ui_manager->notifications.add("Render cancelled", ImVec4(1, 1, 0, 1));
		}
		};

	ui_manager->on_cancel_encoding = [this]() {
		if (animation_renderer && animation_renderer->video_encoder) {
			animation_renderer->video_encoder->cancel();
			ui_manager->notifications.add("Encoding cancelled", ImVec4(1, 1, 0, 1));
		}
		};
}

bool VulkanEngine::render_animation_frame(
	const FractalState& state,
	uint32_t width,
	uint32_t height,
	const std::string& output_path
) {
	// ← ADD THIS: Wait for device to be idle before each frame
	vkDeviceWaitIdle(_device);

	// Save original state
	AllocatedImage originalDrawImage = _drawImage;
	VkImageLayout originalLayout = _drawImageLayout;
	VkExtent2D originalExtent = _drawExtent;
	FractalState originalFractalState = fractal_state;

	// Create offscreen render target
	VkExtent3D imageExtent = { width, height, 1 };

	AllocatedImage offscreenImage;
	offscreenImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	offscreenImage.imageExtent = imageExtent;

	VkImageUsageFlags usages = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo img_info = vkinit::image_create_info(
		offscreenImage.imageFormat, usages, imageExtent
	);

	VmaAllocationCreateInfo img_allocinfo = {};
	img_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	img_allocinfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	vmaCreateImage(_allocator, &img_info, &img_allocinfo,
		&offscreenImage.image, &offscreenImage.allocation, nullptr);

	VkImageViewCreateInfo view_info = vkinit::imageview_create_info(
		offscreenImage.imageFormat, offscreenImage.image, VK_IMAGE_ASPECT_COLOR_BIT
	);
	VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &offscreenImage.imageView));

	// Set up for rendering
	fractal_state = state;
	fractal_state.mark_dirty();

	_drawImage = offscreenImage;
	_drawImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	_drawExtent.width = width;
	_drawExtent.height = height;

	// ← ADD THIS: Ensure device is idle before updating descriptors
	vkDeviceWaitIdle(_device);

	// Update descriptor set
	VkDescriptorImageInfo imgInfo{};
	imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imgInfo.imageView = _drawImage.imageView;
	imgInfo.sampler = nullptr;

	VkWriteDescriptorSet drawImageWrite{};
	drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	drawImageWrite.pNext = nullptr;
	drawImageWrite.dstBinding = 0;
	drawImageWrite.dstSet = _drawImageDescriptors;
	drawImageWrite.descriptorCount = 1;
	drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	drawImageWrite.pImageInfo = &imgInfo;

	vkUpdateDescriptorSets(_device, 1, &drawImageWrite, 0, nullptr);

	// Transition to general layout
	immediate_submit([&](VkCommandBuffer cmd) {
		vkutil::transition_image(cmd, _drawImage.image,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL);
		});

	_drawImageLayout = VK_IMAGE_LAYOUT_GENERAL;

	// Render the fractal
	immediate_submit([&](VkCommandBuffer cmd) {
		draw_background(cmd);
		});

	// Copy to staging buffer
	VkDeviceSize imageSize = width * height * 8;
	AllocatedBuffer stagingBuffer = create_buffer(
		imageSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_TO_CPU
	);

	immediate_submit([&](VkCommandBuffer cmd) {
		vkutil::transition_image(cmd, offscreenImage.image,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = 0;
		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = { width, height, 1 };

		vkCmdCopyImageToBuffer(cmd, offscreenImage.image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			stagingBuffer.buffer, 1, &copyRegion);
		});

	// Ensure data visibility
	vmaInvalidateAllocation(_allocator, stagingBuffer.allocation, 0, imageSize);

	// Map memory and convert to PNG
	uint16_t* srcData = static_cast<uint16_t*>(stagingBuffer.info.pMappedData);
	if (!srcData) {
		fmt::print("Error: Failed to map staging buffer\n");
		destroy_buffer(stagingBuffer);
		vkDestroyImageView(_device, offscreenImage.imageView, nullptr);
		vmaDestroyImage(_allocator, offscreenImage.image, offscreenImage.allocation);

		// Restore before returning
		_drawImage = originalDrawImage;
		_drawImageLayout = originalLayout;
		_drawExtent = originalExtent;
		fractal_state = originalFractalState;

		return false;
	}

	// Half to float converter
	auto half_to_float = [](uint16_t h) -> float {
		uint16_t h_exp = (h & 0x7C00u);
		uint16_t h_sig = (h & 0x03FFu);
		uint32_t f_sgn = (uint32_t)(h & 0x8000u) << 16;
		uint32_t f_exp, f_sig;

		if (h_exp == 0x7C00u) {
			f_exp = 0x7F800000u;
			f_sig = (uint32_t)h_sig << 13;
		}
		else if (h_exp != 0) {
			f_exp = (uint32_t)(h_exp >> 10) + 112;
			f_sig = (uint32_t)h_sig << 13;
		}
		else if (h_sig != 0) {
			int shift = 0;
			while ((h_sig & 0x0400u) == 0) { h_sig <<= 1; shift++; }
			h_sig &= 0x03FFu;
			f_exp = 113 - shift;
			f_sig = (uint32_t)h_sig << 13;
		}
		else {
			f_exp = 0;
			f_sig = 0;
		}

		uint32_t f = f_sgn | (f_exp << 23) | f_sig;
		return *reinterpret_cast<float*>(&f);
		};

	// Tone mapping
	auto tone_map = [](float x) -> float {
		const float a = 2.51f;
		const float b = 0.03f;
		const float c = 2.43f;
		const float d = 0.59f;
		const float e = 0.14f;
		return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
		};

	const float gamma = 1.0f / 2.2f;

	// Convert to 8-bit RGB for PNG
	std::vector<uint8_t> rgb8Data(width * height * 3);

	for (uint32_t y = 0; y < height; y++) {
		uint32_t flippedY = height - 1 - y;
		for (uint32_t x = 0; x < width; x++) {
			uint32_t srcIdx = (flippedY * width + x) * 4;
			uint32_t dstIdx = (y * width + x) * 3;

			for (int c = 0; c < 3; c++) {
				float value = half_to_float(srcData[srcIdx + c]);
				value = tone_map(value);
				value = powf(value, gamma);
				rgb8Data[dstIdx + c] = static_cast<uint8_t>(value * 255.0f);
			}
		}
	}

	// Save as PNG
	int result = stbi_write_png(
		output_path.c_str(),
		width,
		height,
		3,
		rgb8Data.data(),
		width * 3
	);

	// Cleanup
	destroy_buffer(stagingBuffer);
	vkDestroyImageView(_device, offscreenImage.imageView, nullptr);
	vmaDestroyImage(_allocator, offscreenImage.image, offscreenImage.allocation);

	// ← ADD THIS: Wait before restoring state
	vkDeviceWaitIdle(_device);

	// Restore original state
	_drawImage = originalDrawImage;
	_drawImageLayout = originalLayout;
	_drawExtent = originalExtent;
	fractal_state = originalFractalState;

	// ← ADD THIS: Wait before updating descriptors back
	vkDeviceWaitIdle(_device);

	// Restore descriptor set
	VkDescriptorImageInfo restoreImgInfo{};
	restoreImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	restoreImgInfo.imageView = originalDrawImage.imageView;
	restoreImgInfo.sampler = nullptr;

	VkWriteDescriptorSet restoreDrawImageWrite{};
	restoreDrawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	restoreDrawImageWrite.pNext = nullptr;
	restoreDrawImageWrite.dstBinding = 0;
	restoreDrawImageWrite.dstSet = _drawImageDescriptors;
	restoreDrawImageWrite.descriptorCount = 1;
	restoreDrawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	restoreDrawImageWrite.pImageInfo = &restoreImgInfo;

	vkUpdateDescriptorSets(_device, 1, &restoreDrawImageWrite, 0, nullptr);

	return result != 0;
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
	vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU,_device,_surface };

	_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		//.use_default_format_selection()
		.set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		//use vsync present mode
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	_swapchainExtent = vkbSwapchain.extent;
	//store swapchain and its related images
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();

	// Create render semaphores - one per swapchain image
	_renderSemaphores.resize(_swapchainImages.size());
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (size_t i = 0; i < _renderSemaphores.size(); i++) {
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_renderSemaphores[i]));
	}
}

void VulkanEngine::init_swapchain()
{
	create_swapchain(_windowExtent.width, _windowExtent.height);

	//draw image size will match the window
	VkExtent3D drawImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	//hardcoding the draw format to 32 bit float
	_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo rimg_info = vkinit::image_create_info(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

	//for the draw image, we want to allocate it from gpu local memory
	VmaAllocationCreateInfo rimg_allocinfo = {};
	rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//allocate and create the image
	vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr);

	//build a image-view for the draw image to use for rendering
	VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));

	_drawImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	//> depthimg
	_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthImage.imageExtent = drawImageExtent;
	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthImage.imageFormat, depthImageUsages, drawImageExtent);

	//allocate and create the image
	vmaCreateImage(_allocator, &dimg_info, &rimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr);

	//build a image-view for the draw image to use for rendering
	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView));
	//< depthimg

	//add to deletion queues
	_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(_device, _drawImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);

		vkDestroyImageView(_device, _depthImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
		});
}

void VulkanEngine::init_commands()
{
	//create a command pool for commands submitted to the graphics queue.
	//we also want the pool to allow for resetting of individual command buffers
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++) {

		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		// allocate the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
	}

	//> imm_cmd
	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

	// allocate the command buffer for immediate submits
	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_immCommandPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _immCommandPool, nullptr);
		});

	//< imm_cmd
}

void VulkanEngine::init_sync_structures()
{
	//create syncronization structures
	//one fence to control when the gpu has finished rendering the frame,
	//and 2 semaphores to syncronize rendering with swapchain
	//we want the fence to start signalled so we can wait on it on the first frame
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));

	}

	//> imm_fence
	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
	_mainDeletionQueue.push_function([=]() { vkDestroyFence(_device, _immFence, nullptr); });
	//< imm_fence
}

void VulkanEngine::save_screenshot()
{
	// --- 1. Generate timestamped filename ---
	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);
	std::tm tm = *std::localtime(&t);

	std::ostringstream oss;
	oss << "fractal_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_16bit.png";
	std::string filename = oss.str();

	// --- 2. Compute buffer size (R16G16B16A16_SFLOAT = 8 bytes per pixel) ---
	uint32_t width = _drawImage.imageExtent.width;
	uint32_t height = _drawImage.imageExtent.height;
	VkDeviceSize imageSize = width * height * 8;

	// --- 3. Create staging buffer ---
	AllocatedBuffer stagingBuffer = create_buffer(
		imageSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_TO_CPU
	);



	// --- 4. GPU → CPU copy ---
	immediate_submit([&](VkCommandBuffer cmd) {
		VkImageLayout oldLayout = _drawImageLayout;

		if (oldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			vkutil::transition_image(
				cmd, _drawImage.image, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
			);
		}

		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = 0;
		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = { width, height, 1 };

		vkCmdCopyImageToBuffer(
			cmd,
			_drawImage.image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			stagingBuffer.buffer,
			1,
			&copyRegion
		);

		if (oldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			vkutil::transition_image(
				cmd, _drawImage.image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, oldLayout
			);
		}
		});

	// --- 5. Ensure data visibility ---
	vmaInvalidateAllocation(_allocator, stagingBuffer.allocation, 0, imageSize);

	// --- 6. Map memory ---
	uint16_t* srcData = static_cast<uint16_t*>(stagingBuffer.info.pMappedData);
	if (!srcData) {
		fmt::print("Error: Failed to map staging buffer\n");
		destroy_buffer(stagingBuffer);
		return;
	}

	// --- 7. Half → Float converter ---
	auto half_to_float = [](uint16_t h) -> float {
		uint16_t h_exp = (h & 0x7C00u);
		uint16_t h_sig = (h & 0x03FFu);
		uint32_t f_sgn = (uint32_t)(h & 0x8000u) << 16;
		uint32_t f_exp, f_sig;

		if (h_exp == 0x7C00u) {
			f_exp = 0x7F800000u;
			f_sig = (uint32_t)h_sig << 13;
		}
		else if (h_exp != 0) {
			f_exp = (uint32_t)(h_exp >> 10) + 112;
			f_sig = (uint32_t)h_sig << 13;
		}
		else if (h_sig != 0) {
			int shift = 0;
			while ((h_sig & 0x0400u) == 0) { h_sig <<= 1; shift++; }
			h_sig &= 0x03FFu;
			f_exp = 113 - shift;
			f_sig = (uint32_t)h_sig << 13;
		}
		else {
			f_exp = 0;
			f_sig = 0;
		}

		uint32_t f = f_sgn | (f_exp << 23) | f_sig;
		return *reinterpret_cast<float*>(&f);
		};

	// --- 8. Tone mapping + gamma correction ---
	auto tone_map = [](float x) -> float {
		// ACES tone mapping
		const float a = 2.51f;
		const float b = 0.03f;
		const float c = 2.43f;
		const float d = 0.59f;
		const float e = 0.14f;
		return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
		};

	const float gamma = 1.0f / 2.2f;

	std::vector<uint16_t> rgb16Data(width * height * 3);

	for (uint32_t y = 0; y < height; y++) {
		uint32_t flippedY = height - 1 - y;
		for (uint32_t x = 0; x < width; x++) {
			uint32_t srcIdx = (flippedY * width + x) * 4; // RGBA16F
			uint32_t dstIdx = (y * width + x) * 3;        // RGB16

			for (int c = 0; c < 3; c++) {
				float value = half_to_float(srcData[srcIdx + c]);
				value = tone_map(value);
				value = powf(value, gamma);
				rgb16Data[dstIdx + c] = static_cast<uint16_t>(value * 65535.0f);
			}
		}
	}

	// --- 9. Save as 16-bit PNG ---
	int result = stbi_write_png(
		filename.c_str(),
		width,
		height,
		3,
		rgb16Data.data(),
		width * 3 * sizeof(uint16_t)
	);

	if (result) {
		fmt::print("High-quality 16-bit screenshot saved: {}\n", filename);
		ui_manager->notifications.add(
			std::string("Screenshot saved: ") + filename,
			ImVec4(0.0f, 1.0f, 0.0f, 1.0f)
	
		);
	}
	else {
		fmt::print("Error: Failed to save screenshot\n");
		ui_manager->notifications.add(
			"Failed to save screenshot",
			ImVec4(1.0f, 0.0f, 0.0f, 1.0f)
		);
	}

	// --- 10. Cleanup ---
	destroy_buffer(stagingBuffer);
}

void VulkanEngine::handle_zoom(bool zoom_in)
{
	// Lower zoom = more detail(zoomed in)
		// So zoom_in should DECREASE zoom value
		double zoom_factor = zoom_in ? 0.8 : 1.25;  // ← zoom_in DECREASES zoom
	fractal_state.zoom *= zoom_factor;

	// Adjust iterations (lower zoom = more detail = more iterations)
	if (fractal_state.zoom < 0.01) {
		fractal_state.max_iterations = 2048;
	}
	else if (fractal_state.zoom < 0.1) {
		fractal_state.max_iterations = 1536;
	}
	else if (fractal_state.zoom < 1.0) {
		fractal_state.max_iterations = 1024;
	}
	else if (fractal_state.zoom < 10.0) {
		fractal_state.max_iterations = 512;
	}
	else {
		fractal_state.max_iterations = 256;
	}

	fractal_state.mark_dirty();
}

void VulkanEngine::zoom_to_point(int mouse_x, int mouse_y, bool zoom_in)
{
	if (_windowExtent.width == 0 || _windowExtent.height == 0) return;

	float aspect = (float)_windowExtent.width / (float)_windowExtent.height;
	float normalized_x = (mouse_x / (float)_windowExtent.width - 0.5f);
	float normalized_y = (mouse_y / (float)_windowExtent.height - 0.5f);

	double world_x = fractal_state.center_x + normalized_x * fractal_state.zoom * aspect;
	double world_y = fractal_state.center_y + normalized_y * fractal_state.zoom;

	// SWAPPED: zoom IN = DECREASE zoom value
	double zoom_factor = zoom_in ? 0.7 : 1.4;  // ← zoom_in DECREASES zoom
	fractal_state.zoom *= zoom_factor;

	fractal_state.center_x = world_x - normalized_x * fractal_state.zoom * aspect;
	fractal_state.center_y = world_y - normalized_y * fractal_state.zoom;

	// Same iteration logic
	if (fractal_state.zoom < 0.00001) {
		fractal_state.max_iterations = 2048;
	}
	else if (fractal_state.zoom < 0.0001) {
		fractal_state.max_iterations = 1536;
	}
	else if (fractal_state.zoom < 0.001) {
		fractal_state.max_iterations = 1024;
	}
	else if (fractal_state.zoom < 0.01) {
		fractal_state.max_iterations = 512;
	}
	else {
		fractal_state.max_iterations = 384;
	}

	fractal_state.mark_dirty();
}

void VulkanEngine::export_print_quality(uint32_t width, uint32_t height, bool supersample)
{
	

	
	// ← CRITICAL: Snapshot the EXACT state RIGHT NOW
	// FractalState snapshotState = fractal_state;
	FractalState snapshotState = ui_manager->get_state();

	
	// --- 1. Calculate render dimensions ---
	uint32_t renderWidth = supersample ? width * 2 : width;
	uint32_t renderHeight = supersample ? height * 2 : height;

	fmt::print("\n=== EXPORT STARTING ===\n");
	fmt::print("Capturing screen view: Center=({}, {}), Zoom={}\n",
		snapshotState.center_x, snapshotState.center_y, snapshotState.zoom);
	fmt::print("Iterations: {}, Palette: {}\n",
		snapshotState.max_iterations, snapshotState.palette_mode);
	fmt::print("Starting print export: {}x{}{}\n", renderWidth, renderHeight,
		supersample ? " (2x supersampled)" : "");

	ui_manager->notifications.add(
		"Starting high-resolution export...",
		ImVec4(1.0f, 1.0f, 0.0f, 1.0f)
	);

	// --- 2. Create offscreen render target ---
	fmt::print("[1/6] Creating offscreen render target...\n");
	ui_manager->notifications.add(
		"[1/6] Creating render target...",
		ImVec4(0.5f, 0.5f, 1.0f, 1.0f)
	);

	VkExtent3D imageExtent = {
		renderWidth,
		renderHeight,
		1
	};

	AllocatedImage offscreenImage;
	offscreenImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	offscreenImage.imageExtent = imageExtent;

	VkImageUsageFlags usages = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo img_info = vkinit::image_create_info(
		offscreenImage.imageFormat, usages, imageExtent
	);

	VmaAllocationCreateInfo img_allocinfo = {};
	img_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	img_allocinfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	vmaCreateImage(_allocator, &img_info, &img_allocinfo,
		&offscreenImage.image, &offscreenImage.allocation, nullptr);

	VkImageViewCreateInfo view_info = vkinit::imageview_create_info(
		offscreenImage.imageFormat, offscreenImage.image, VK_IMAGE_ASPECT_COLOR_BIT
	);
	VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &offscreenImage.imageView));

	// --- 3. Save original state and apply snapshot ---
	AllocatedImage originalDrawImage = _drawImage;
	VkImageLayout originalLayout = _drawImageLayout;
	VkExtent2D originalExtent = _drawExtent;
	FractalState originalFractalState = fractal_state;

	// Calculate aspect ratios
	float screenAspect = (float)_windowExtent.width / (float)_windowExtent.height;
	float exportAspect = (float)renderWidth / (float)renderHeight;

	fmt::print("Screen aspect: {:.3f} ({}×{}), Export aspect: {:.3f} ({}×{})\n",
		screenAspect, _windowExtent.width, _windowExtent.height,
		exportAspect, renderWidth, renderHeight);

	// DON'T adjust zoom - export exactly what's on screen
	// (different aspect ratios will show more/less area, but same center and scale)
	fractal_state = snapshotState;

	fractal_state.mark_dirty();

	

	

	fmt::print("Using exact screen state: Center=({}, {}), Zoom={}\n",
		fractal_state.center_x, fractal_state.center_y, fractal_state.zoom);

	_drawImage = offscreenImage;
	_drawImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	_drawExtent.width = renderWidth;
	_drawExtent.height = renderHeight;

	fmt::print("[2/6] Rendering fractal at {}x{}...\n", renderWidth, renderHeight);
	ui_manager->notifications.add(
		"[2/6] Rendering fractal at high resolution...",
		ImVec4(0.5f, 0.5f, 1.0f, 1.0f)
	);

	// Transition offscreen image to general layout
	immediate_submit([&](VkCommandBuffer cmd) {
		vkutil::transition_image(cmd, _drawImage.image,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL);
		});

	_drawImageLayout = VK_IMAGE_LAYOUT_GENERAL;

	// Update descriptor set to point to offscreen image
	VkDescriptorImageInfo imgInfo{};
	imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imgInfo.imageView = _drawImage.imageView;
	imgInfo.sampler = nullptr;

	VkWriteDescriptorSet drawImageWrite{};
	drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	drawImageWrite.pNext = nullptr;
	drawImageWrite.dstBinding = 0;
	drawImageWrite.dstSet = _drawImageDescriptors;
	drawImageWrite.descriptorCount = 1;
	drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	drawImageWrite.pImageInfo = &imgInfo;

	vkUpdateDescriptorSets(_device, 1, &drawImageWrite, 0, nullptr);

	
	// Render the fractal
	immediate_submit([&](VkCommandBuffer cmd) {
		draw_background(cmd);
		});

	fmt::print("Fractal render complete\n");

	fmt::print("[3/6] Copying data from GPU to CPU...\n");
	ui_manager->notifications.add(
		"[3/6] Transferring data from GPU...",
		ImVec4(0.5f, 0.5f, 1.0f, 1.0f)
	);

	// --- 4. Create staging buffer ---
	VkDeviceSize imageSize = renderWidth * renderHeight * 8;
	AllocatedBuffer stagingBuffer = create_buffer(
		imageSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_TO_CPU
	);

	// --- 5. Copy offscreen image to staging buffer ---
	immediate_submit([&](VkCommandBuffer cmd) {
		vkutil::transition_image(cmd, offscreenImage.image,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = 0;
		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = { renderWidth, renderHeight, 1 };

		vkCmdCopyImageToBuffer(cmd, offscreenImage.image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			stagingBuffer.buffer, 1, &copyRegion);
		});

	// --- 6. Restore original state ---
	_drawImage = originalDrawImage;
	_drawImageLayout = originalLayout;
	_drawExtent = originalExtent;
	fractal_state = originalFractalState;

	// Restore descriptor set to original image
	VkDescriptorImageInfo restoreImgInfo{};
	restoreImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	restoreImgInfo.imageView = originalDrawImage.imageView;
	restoreImgInfo.sampler = nullptr;

	VkWriteDescriptorSet restoreDrawImageWrite{};
	restoreDrawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	restoreDrawImageWrite.pNext = nullptr;
	restoreDrawImageWrite.dstBinding = 0;
	restoreDrawImageWrite.dstSet = _drawImageDescriptors;
	restoreDrawImageWrite.descriptorCount = 1;
	restoreDrawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	restoreDrawImageWrite.pImageInfo = &restoreImgInfo;

	vkUpdateDescriptorSets(_device, 1, &restoreDrawImageWrite, 0, nullptr);

	// --- 7. Ensure data visibility ---
	vmaInvalidateAllocation(_allocator, stagingBuffer.allocation, 0, imageSize);

	// --- 8. Map memory ---
	uint16_t* srcData = static_cast<uint16_t*>(stagingBuffer.info.pMappedData);
	if (!srcData) {
		fmt::print("ERROR: Failed to map staging buffer\n");
		ui_manager->notifications.add(
			"Export failed: Memory mapping error",
			ImVec4(1.0f, 0.0f, 0.0f, 1.0f)
		);
		destroy_buffer(stagingBuffer);
		vkDestroyImageView(_device, offscreenImage.imageView, nullptr);
		vmaDestroyImage(_allocator, offscreenImage.image, offscreenImage.allocation);
		return;
	}

	fmt::print("[4/6] Processing {:.1f} million pixels...\n", (renderWidth * renderHeight) / 1000000.0f);
	ui_manager->notifications.add(
		"[4/6] Processing image data...",
		ImVec4(0.5f, 0.5f, 1.0f, 1.0f)
	);

	// --- 9. Half → Float converter ---
	auto half_to_float = [](uint16_t h) -> float {
		uint16_t h_exp = (h & 0x7C00u);
		uint16_t h_sig = (h & 0x03FFu);
		uint32_t f_sgn = (uint32_t)(h & 0x8000u) << 16;
		uint32_t f_exp, f_sig;

		if (h_exp == 0x7C00u) {
			f_exp = 0x7F800000u;
			f_sig = (uint32_t)h_sig << 13;
		}
		else if (h_exp != 0) {
			f_exp = (uint32_t)(h_exp >> 10) + 112;
			f_sig = (uint32_t)h_sig << 13;
		}
		else if (h_sig != 0) {
			int shift = 0;
			while ((h_sig & 0x0400u) == 0) { h_sig <<= 1; shift++; }
			h_sig &= 0x03FFu;
			f_exp = 113 - shift;
			f_sig = (uint32_t)h_sig << 13;
		}
		else {
			f_exp = 0;
			f_sig = 0;
		}

		uint32_t f = f_sgn | (f_exp << 23) | f_sig;
		return *reinterpret_cast<float*>(&f);
		};

	/* --- 10. Tone mapping + gamma correction-- -
	auto tone_map = [](float x) -> float {
		const float a = 2.51f;
		const float b = 0.03f;
		const float c = 2.43f;
		const float d = 0.59f;
		const float e = 0.14f;
		return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
		};

	const float gamma = 1.0f / 2.2f; */

	std::vector<uint16_t> rgb16Data(renderWidth * renderHeight * 3);

	// Progress tracking
	uint32_t totalPixels = renderWidth * renderHeight;
	uint32_t progressStep = totalPixels / 10;
	uint32_t nextProgressUpdate = progressStep;
	int lastPercent = 0;

	for (uint32_t y = 0; y < renderHeight; y++) {
		uint32_t flippedY = renderHeight - 1 - y;
		for (uint32_t x = 0; x < renderWidth; x++) {
			uint32_t srcIdx = (flippedY * renderWidth + x) * 4;
			uint32_t dstIdx = (y * renderWidth + x) * 3;

			for (int c = 0; c < 3; c++) {
				float value = half_to_float(srcData[srcIdx + c]);
			//	value = tone_map(value);
			//	value = powf(value, gamma);
				value = std::clamp(value, 0.0f, 1.0f);
				rgb16Data[dstIdx + c] = static_cast<uint16_t>(value * 65535.0f);
			}

			uint32_t currentPixel = y * renderWidth + x;
			if (currentPixel >= nextProgressUpdate && progressStep > 0) {
				int percent = (currentPixel * 100) / totalPixels;
				if (percent != lastPercent && percent % 10 == 0) {
					fmt::print("  Processing: {}%\n", percent);
					lastPercent = percent;
				}
				nextProgressUpdate += progressStep;
			}
		}
	}

	// --- 11. Generate filename ---
	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);
	std::tm tm = *std::localtime(&t);

	std::ostringstream oss;
	oss << "fractal_print_" << width << "x" << height
		<< (supersample ? "_2xAA" : "")
		<< "_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_16bit.png";
	std::string filename = oss.str();

	fmt::print("[5/6] Encoding and writing PNG: {}...\n", filename);
	ui_manager->notifications.add(
		"[5/6] Writing 16-bit PNG file...",
		ImVec4(0.5f, 0.5f, 1.0f, 1.0f)
	);

	// --- 12. Save as 16-bit PNG using libpng ---
	{
		FILE* fp = nullptr;
		errno_t err = fopen_s(&fp, filename.c_str(), "wb");
		if (!fp || err) {
			fmt::print("[6/6] Failed to open file for writing\n");
			ui_manager->notifications.add("Export failed: Could not open file", ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
		}
		else {
			png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
			png_infop info_ptr = png_create_info_struct(png_ptr);
			if (!png_ptr || !info_ptr) {
				fmt::print("ERROR: Failed to create PNG write struct\n");
				fclose(fp);
				return;
			}

			if (setjmp(png_jmpbuf(png_ptr))) {
				fmt::print("ERROR: libpng error during write\n");
				png_destroy_write_struct(&png_ptr, &info_ptr);
				fclose(fp);
				return;
			}

			png_init_io(png_ptr, fp);
			
			png_set_compression_level(png_ptr, 9);

			// --- Header ---
			png_set_IHDR(
				png_ptr, info_ptr,
				renderWidth, renderHeight,
				16, PNG_COLOR_TYPE_RGB,
				PNG_INTERLACE_NONE,
				PNG_COMPRESSION_TYPE_BASE,
				PNG_FILTER_TYPE_BASE
			);

			// --- Gamma and sRGB ---
			png_set_gAMA(png_ptr, info_ptr, 1.0 / 2.2);
			png_set_sRGB(png_ptr, info_ptr, PNG_sRGB_INTENT_PERCEPTUAL);

			// --- DPI / physical resolution ---
			const double dpi = 300.0;
			const double meters_per_inch = 0.0254;
			const png_uint_32 ppm = static_cast<png_uint_32>(dpi / meters_per_inch + 0.5);
			png_set_pHYs(png_ptr, info_ptr, ppm, ppm, PNG_RESOLUTION_METER);

			// --- Metadata ---
			std::vector<png_text> text_ptrs;
			auto add_text = [&](const char* key, const std::string& value) {
				png_text t{};
				t.compression = PNG_TEXT_COMPRESSION_NONE;
				t.key = const_cast<char*>(key);
				t.text = const_cast<char*>(value.c_str());
				t.text_length = value.size();
				text_ptrs.push_back(t);
				};

			add_text("Software", "Vulkan Fractal Renderer v1.4");
			add_text("Engine", "Vulkan + libpng 1.6");

			// Timestamp
			{
				std::ostringstream ts;
				ts << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
				add_text("Export Time", ts.str());
			}

			// Physical size
			double width_inch = renderWidth / dpi;
			double height_inch = renderHeight / dpi;
			add_text("Print Size (inches)", fmt::format("{:.2f} × {:.2f}", width_inch, height_inch));
			add_text("Print Size (cm)", fmt::format("{:.2f} × {:.2f}", width_inch * 2.54, height_inch * 2.54));

			// Fractal metadata
			
			add_text("Center", fmt::format("({}, {})", snapshotState.center_x, snapshotState.center_y));
			add_text("Zoom", fmt::format("{:.9f}", snapshotState.zoom));
			add_text("Iterations", fmt::format("{}", snapshotState.max_iterations));
			add_text("Palette", fmt::format("{}", snapshotState.palette_mode));
			add_text("Orbit Trap", snapshotState.orbit_trap_enabled ? "Enabled" : "Disabled");
			

			if (!text_ptrs.empty())
				png_set_text(png_ptr, info_ptr, text_ptrs.data(), (int)text_ptrs.size());

			// Creation time chunk
			png_time pngtime;
			png_convert_from_time_t(&pngtime, std::time(nullptr));
			png_set_tIME(png_ptr, info_ptr, &pngtime);

			// --- Write image ---
			png_write_info(png_ptr, info_ptr);
			png_set_swap(png_ptr); // fix endianness

			std::vector<png_bytep> row_pointers(renderHeight);
			for (uint32_t y = 0; y < renderHeight; y++)
				row_pointers[y] = reinterpret_cast<png_bytep>(&rgb16Data[y * renderWidth * 3]);

			png_write_image(png_ptr, row_pointers.data());
			png_write_end(png_ptr, nullptr);
			png_destroy_write_struct(&png_ptr, &info_ptr);
			fclose(fp);

			// --- Report ---
			float megapixels = (renderWidth * renderHeight) / 1e6f;
			fmt::print("[6/6]  Export complete!\n");
			fmt::print("  File: {}\n", filename);
			fmt::print("  Resolution: {}x{} ({:.1f} MP)\n", renderWidth, renderHeight, megapixels);
			fmt::print("  Physical size: {:.2f}×{:.2f} inches @ {} DPI\n", width_inch, height_inch, (int)dpi);

			std::ostringstream msg;
			msg << " Export complete! " << renderWidth << "×" << renderHeight
				<< " (" << std::fixed << std::setprecision(1) << megapixels << " MP, "
				<< (int)dpi << " DPI)";
			ui_manager->notifications.add(msg.str(), ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
		}
	}

	// --- 13. Cleanup ---
	destroy_buffer(stagingBuffer);
	vkDestroyImageView(_device, offscreenImage.imageView, nullptr);
	vmaDestroyImage(_allocator, offscreenImage.image, offscreenImage.allocation);

	fmt::print("=== EXPORT FINISHED ===\n\n");

}

void VulkanEngine::toggle_fullscreen()
{
	bool isFullscreen = SDL_GetWindowFlags(_window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
	SDL_SetWindowFullscreen(_window, isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	// allocate buffer
	VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;

	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	AllocatedBuffer newBuffer{};

	// allocate the buffer
	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation,
		&newBuffer.info));

	

	return newBuffer;
}

void VulkanEngine::destroy_buffer(const AllocatedBuffer& buffer)
{
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

void VulkanEngine::handle_continuous_input(const uint8_t* keyState, float deltaTime)
{
	if (_windowExtent.width == 0 || _windowExtent.height == 0) return;

	// Pan speed scales with zoom level for consistent feel
	float pan_speed = fractal_state.zoom * deltaTime * 2.0f;  // ← CHANGE
	float aspect = (float)_windowExtent.width / (float)_windowExtent.height;

	// WASD or Arrow keys for panning
	if (keyState[SDL_SCANCODE_W] || keyState[SDL_SCANCODE_UP]) {
		fractal_state.center_y -= pan_speed;  // ← CHANGE
	}
	if (keyState[SDL_SCANCODE_S] || keyState[SDL_SCANCODE_DOWN]) {
		fractal_state.center_y += pan_speed;  // ← CHANGE
	}
	if (keyState[SDL_SCANCODE_A] || keyState[SDL_SCANCODE_LEFT]) {
		fractal_state.center_x -= pan_speed * aspect;  // ← CHANGE
	}
	if (keyState[SDL_SCANCODE_D] || keyState[SDL_SCANCODE_RIGHT]) {
		fractal_state.center_x += pan_speed * aspect;  // ← CHANGE
	}

	// Q/E for zooming
	if (keyState[SDL_SCANCODE_Q]) {
		handle_zoom(false); // Zoom out
	}
	if (keyState[SDL_SCANCODE_E]) {
		handle_zoom(true); // Zoom in
	}
}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VK_CHECK(vkResetFences(_device, 1, &_immFence));
	VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBuffer cmd = _immCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
	VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

	// submit command buffer to the queue and execute it.
	//  _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

void VulkanEngine::init_descriptors()
{
	// Update pool sizes to include storage buffer for deep zoom
	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes =
	{
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }  // ← ADD THIS for reference orbit

	};

	globalDescriptorAllocator.init(_device, 10, sizes);

	// Regular compute shader descriptor layout (image only)
	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}



	// Allocate descriptor for draw image
	_drawImageDescriptors = globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);

	{
		DescriptorWriter writer;
		writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE,
			VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.update_set(_device, _drawImageDescriptors);
	}

	// ═══════════════════════════════════════════════════════════════
	// NEW: Deep zoom descriptor layout (image + buffer)
	// ═══════════════════════════════════════════════════════════════
	{
		DescriptorLayoutBuilder builder;
		builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);   // Image binding
		builder.add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);  // Buffer binding
		_deepZoomDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}



	for (int i = 0; i < FRAME_OVERLAP; i++) {
		_frames[i]._deepZoomDescriptorSet = globalDescriptorAllocator.allocate(
			_device, _deepZoomDescriptorLayout);
	}

	fmt::print("Allocated {} deep zoom descriptor sets (one per frame)\n", FRAME_OVERLAP);


	// Note: We'll update the descriptor set contents in draw() when we have actual buffer
	// ═══════════════════════════════════════════════════════════════

	// Cleanup
	_mainDeletionQueue.push_function([&]() {
		globalDescriptorAllocator.destroy_pools(_device);
		vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _deepZoomDescriptorLayout, nullptr);  // ← ADD THIS
		});

	// Frame descriptors (keep minimal for potential future use)
	for (int i = 0; i < FRAME_OVERLAP; i++) {
		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
		};

		_frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
		_frames[i]._frameDescriptors.init(_device, 100, frame_sizes);

		_mainDeletionQueue.push_function([this, i]() {
			_frames[i]._frameDescriptors.destroy_pools(_device);
			});
	}
}



void VulkanEngine::init_pipelines()
{
	//COMPUTE PIPELINES	
	

	

}





void VulkanEngine::init_imgui()
{
	// 1: create descriptor pool for IMGUI
	//  the size of the pool is very oversize, but it's copied from imgui demo
	//  itself.
	VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

	// 2: initialize imgui library

	// this initializes the core structures of imgui
	ImGui::CreateContext();

	// this initializes imgui for SDL
	ImGui_ImplSDL2_InitForVulkan(_window);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _chosenGPU;
	init_info.Device = _device;
	init_info.Queue = _graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	init_info.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;


	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	ImGui_ImplVulkan_CreateFontsTexture();

	// add the destroy the imgui created structures
	_mainDeletionQueue.push_function([=]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
		});
}

void VulkanEngine::apply_ui_theme()
{
	ImGuiStyle& style = ImGui::GetStyle();

	// Modern dark theme with blue accents (matching your screenshot)
	style.WindowRounding = 8.0f;
	style.FrameRounding = 4.0f;
	style.ScrollbarRounding = 4.0f;
	style.GrabRounding = 4.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize = 1.0f;
	style.WindowPadding = ImVec2(8, 8);
	style.FramePadding = ImVec2(8, 4);
	style.ItemSpacing = ImVec2(8, 4);

	// Color scheme
	ImVec4* colors = style.Colors;
	colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.15f, 0.95f);
	colors[ImGuiCol_Header] = ImVec4(0.20f, 0.40f, 0.70f, 0.80f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.50f, 0.85f, 0.80f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.60f, 1.00f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.20f, 0.40f, 0.70f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.50f, 0.85f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.60f, 1.00f, 1.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.20f, 0.30f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.25f, 0.35f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.30f, 0.40f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.60f, 1.00f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.40f, 0.70f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.60f, 1.00f, 1.00f);
}


void VulkanEngine::resize_swapchain()
{
	vkDeviceWaitIdle(_device);

	int w, h;
	SDL_GetWindowSize(_window, &w, &h);

	if (w <= 0 || h <= 0) {
		return;
	}

	_windowExtent.width = w;
	_windowExtent.height = h;

	// NEW: Update input handler with new size
	if (input_handler) {
		input_handler->update_window_size(w, h);
	}

	// Rest of existing resize code...
	destroy_swapchain();
	create_swapchain(_windowExtent.width, _windowExtent.height);

	// ... recreate draw image ...

	resize_requested = false;
}

AllocatedImage VulkanEngine::create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
	AllocatedImage newImage;
	newImage.imageFormat = format;
	newImage.imageExtent = size;

	VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
	if (mipmapped) {
		img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
	}

	// always allocate images on dedicated GPU memory
	VmaAllocationCreateInfo allocinfo = {};
	allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// allocate and create the image
	VK_CHECK(vmaCreateImage(_allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

	// if the format is a depth format, we will need to have it use the correct
	// aspect flag
	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format == VK_FORMAT_D32_SFLOAT) {
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	// build a image-view for the image
	VkImageViewCreateInfo view_info = vkinit::imageview_create_info(format, newImage.image, aspectFlag);
	view_info.subresourceRange.levelCount = img_info.mipLevels;

	VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

	return newImage;
}

AllocatedImage VulkanEngine::create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
	size_t data_size = size.depth * size.width * size.height * 4;
	AllocatedBuffer uploadbuffer = create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	memcpy(uploadbuffer.info.pMappedData, data, data_size);

	AllocatedImage new_image = create_image(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

	immediate_submit([&](VkCommandBuffer cmd) {
		vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = size;

		// copy the buffer into the image
		vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
			&copyRegion);

		vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});

	destroy_buffer(uploadbuffer);

	return new_image;
}

void VulkanEngine::destroy_image(const AllocatedImage& img)
{
	vkDestroyImageView(_device, img.imageView, nullptr);
	vmaDestroyImage(_allocator, img.image, img.allocation);
}







