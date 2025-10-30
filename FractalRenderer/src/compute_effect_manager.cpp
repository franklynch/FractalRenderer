#include "compute_effect_manager.h"
#include "vk_pipelines.h"
#include <fmt/core.h>

ComputeEffectManager::ComputeEffectManager(VkDevice dev,
    VkDescriptorSetLayout regular_layout,
    VkDescriptorSetLayout deep_zoom_layout)
    : device(dev),
    regular_descriptor_layout(regular_layout),
    deep_zoom_descriptor_layout(deep_zoom_layout)
{
}

ComputeEffectManager::~ComputeEffectManager()
{
    cleanup();
}

void ComputeEffectManager::create_pipeline_layouts()
{
    // Push constant range (same for both layouts)
    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(ComputePushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // ═══════════════════════════════════════════════════════════
    // Create REGULAR pipeline layout (image only)
    // ═══════════════════════════════════════════════════════════
    {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pSetLayouts = &regular_descriptor_layout;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;
        layoutInfo.pushConstantRangeCount = 1;

        VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr,
            &regular_pipeline_layout));

        fmt::print("ComputeEffectManager: Regular pipeline layout created\n");
    }

    // ═══════════════════════════════════════════════════════════
    // Create DEEP ZOOM pipeline layout (image + buffer)
    // ═══════════════════════════════════════════════════════════
    {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pSetLayouts = &deep_zoom_descriptor_layout;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;
        layoutInfo.pushConstantRangeCount = 1;

        VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr,
            &deep_zoom_pipeline_layout));

        fmt::print("ComputeEffectManager: Deep zoom pipeline layout created\n");
    }
}

VkPipeline ComputeEffectManager::create_compute_pipeline(const char* shader_path,
    VkPipelineLayout layout)
{
    VkShaderModule shaderModule;
    if (!vkutil::load_shader_module(shader_path, device, &shaderModule)) {
        fmt::print("ERROR: Failed to load shader: {}\n", shader_path);
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = layout;  // ← Use provided layout

    VkPipeline pipeline;
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
        &pipelineInfo, nullptr, &pipeline);

    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        fmt::print("ERROR: Failed to create compute pipeline for {}\n", shader_path);
        return VK_NULL_HANDLE;
    }

    return pipeline;
}

void ComputeEffectManager::init_effect(FractalType type,
    const char* shader_path,
    bool use_deep_zoom_layout)
{
    size_t index = static_cast<size_t>(type);

    // Select appropriate pipeline layout
    VkPipelineLayout selected_layout = use_deep_zoom_layout
        ? deep_zoom_pipeline_layout
        : regular_pipeline_layout;

    // Create pipeline with selected layout
    effects[index].type = type;
    effects[index].pipeline = create_compute_pipeline(shader_path, selected_layout);
    effects[index].layout = selected_layout;
    effects[index].uses_deep_zoom_layout = use_deep_zoom_layout;

    if (effects[index].pipeline != VK_NULL_HANDLE) {
        const char* layout_type = use_deep_zoom_layout ? "deep zoom" : "regular";
        fmt::print("Initialized fractal pipeline: {} (index {}, {} layout)\n",
            shader_path, index, layout_type);
    }
}

void ComputeEffectManager::init_pipelines()
{
    // Create BOTH pipeline layouts first
    create_pipeline_layouts();

    // Create regular fractal pipelines (use regular layout)
    init_effect(FractalType::Mandelbrot, "shaders/mandelbrot.comp.spv", false);
    init_effect(FractalType::JuliaSet, "shaders/julia.comp.spv", false);
    init_effect(FractalType::BurningShip, "shaders/burning_ship.comp.spv", false);
    init_effect(FractalType::Mandelbulb, "shaders/mandelbulb.comp.spv", false);
    init_effect(FractalType::Phoenix, "shaders/phoenix.comp.spv", false);

    // Create deep zoom pipeline (use deep zoom layout)
    init_effect(FractalType::Deep_Zoom,
        "shaders/test_deep_zoom.comp.spv",
        true);  // ← Uses deep zoom layout!

    fmt::print("ComputeEffectManager: All pipelines initialized\n");
}

void ComputeEffectManager::cleanup()
{
    // Destroy all pipelines
    for (auto& effect : effects) {
        if (effect.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, effect.pipeline, nullptr);
            effect.pipeline = VK_NULL_HANDLE;
        }
    }

    // Destroy BOTH pipeline layouts
    if (regular_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, regular_pipeline_layout, nullptr);
        regular_pipeline_layout = VK_NULL_HANDLE;
    }

    if (deep_zoom_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, deep_zoom_pipeline_layout, nullptr);
        deep_zoom_pipeline_layout = VK_NULL_HANDLE;
    }

    fmt::print("ComputeEffectManager: Cleanup complete\n");
}
