#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <string>

import vk.engine;
import vk.context;
import vk.plugins;
import vk.camera;

// clang-format off
#ifndef VK_CHECK
#define VK_CHECK(x) do { VkResult _vk_check_res = (x); if (_vk_check_res != VK_SUCCESS) { throw std::runtime_error(std::string("Vulkan error ") + std::to_string(_vk_check_res)); } } while (false)
#endif
// clang-format on

// ============================================================================
// Wireframe Box Plugin - Example External Plugin
// ============================================================================
class WireframeBoxPlugin {
public:
    WireframeBoxPlugin() = default;

    // Plugin metadata (required by CPlugin concept)
    const char* name() const { return "Wireframe Box"; }
    vk::context::PluginPhase phases() const {
        return vk::context::PluginPhase::Initialize |
               vk::context::PluginPhase::Render |
               vk::context::PluginPhase::Cleanup;
    }
    bool is_enabled() const { return enabled_; }
    void set_enabled(bool e) { enabled_ = e; }

    // Phase callbacks
    void on_setup(vk::context::PluginContext& ctx) {}

    void on_initialize(vk::context::PluginContext& ctx) {
        create_pipeline(*ctx.engine);
    }

    void on_pre_render(vk::context::PluginContext& ctx) {}

    void on_render(vk::context::PluginContext& ctx) {
        if (!is_enabled() || !viewport_plugin_) return;

        vk::camera::Camera& camera = viewport_plugin_->get_camera();

        vkCmdBindPipeline(*ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

        VkViewport viewport{
            .width = static_cast<float>(ctx.frame->extent.width),
            .height = static_cast<float>(ctx.frame->extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(*ctx.cmd, 0, 1, &viewport);

        VkRect2D scissor{.offset = {0, 0}, .extent = ctx.frame->extent};
        vkCmdSetScissor(*ctx.cmd, 0, 1, &scissor);

        // Compute MVP matrix
        const auto& view = camera.view_matrix();
        const auto& proj = camera.proj_matrix();
        const auto model = vk::camera::Mat4::identity();
        const auto mvp = proj * view * model;

        // Push constants
        vkCmdPushConstants(*ctx.cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, mvp.m.data());

        // Draw wireframe box (24 vertices = 12 edges * 2 vertices)
        vkCmdDraw(*ctx.cmd, 24, 1, 0, 0);
    }

    void on_post_render(vk::context::PluginContext& ctx) {}
    void on_present(vk::context::PluginContext& ctx) {}

    void on_cleanup(vk::context::PluginContext& ctx) {
        if (ctx.engine) {
            vkDestroyPipeline(ctx.engine->device, pipeline_, nullptr);
            vkDestroyPipelineLayout(ctx.engine->device, layout_, nullptr);
        }
    }

    void on_event(const SDL_Event& event) {}
    void on_resize(uint32_t width, uint32_t height) {}

    void set_viewport_plugin(vk::plugins::Viewport3DPlugin* vp) {
        viewport_plugin_ = vp;
    }

private:
    void create_pipeline(const vk::context::EngineContext& eng) {
        // Create pipeline layout with MVP push constant
        VkPushConstantRange push_constant{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(float) * 16  // mat4
        };

        VkPipelineLayoutCreateInfo layout_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant
        };
        VK_CHECK(vkCreatePipelineLayout(eng.device, &layout_info, nullptr, &layout_));

        // Load shaders
        VkShaderModule vs = load_shader(eng.device, "test/shader/wireframe.vert.spv");
        VkShaderModule fs = load_shader(eng.device, "test/shader/wireframe.frag.spv");

        VkPipelineShaderStageCreateInfo stages[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vs,
                .pName = "main"
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fs,
                .pName = "main"
            }
        };

        VkPipelineVertexInputStateCreateInfo vertex_input{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };

        VkPipelineInputAssemblyStateCreateInfo input_assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST  // Wireframe!
        };

        VkPipelineViewportStateCreateInfo viewport_state{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1
        };

        VkPipelineRasterizationStateCreateInfo rasterization{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_LINE,  // Wireframe mode
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f
        };

        VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
        };

        VkPipelineColorBlendAttachmentState color_blend_attachment{
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        VkPipelineColorBlendStateCreateInfo color_blend{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &color_blend_attachment
        };

        VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = dynamic_states
        };

        VkFormat color_format = VK_FORMAT_B8G8R8A8_UNORM;
        VkPipelineRenderingCreateInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &color_format
        };

        VkGraphicsPipelineCreateInfo pipeline_info{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rendering_info,
            .stageCount = 2,
            .pStages = stages,
            .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterization,
            .pMultisampleState = &multisample,
            .pColorBlendState = &color_blend,
            .pDynamicState = &dynamic_state,
            .layout = layout_
        };

        VK_CHECK(vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_));

        vkDestroyShaderModule(eng.device, vs, nullptr);
        vkDestroyShaderModule(eng.device, fs, nullptr);
    }

    VkShaderModule load_shader(VkDevice device, const char* path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error(std::string("Failed to open shader: ") + path);
        }

        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0);
        std::vector<char> code(size);
        file.read(code.data(), static_cast<std::streamsize>(size));

        VkShaderModuleCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code.size(),
            .pCode = reinterpret_cast<const uint32_t*>(code.data())
        };

        VkShaderModule module;
        VK_CHECK(vkCreateShaderModule(device, &create_info, nullptr, &module));
        return module;
    }

private:
    bool enabled_{true};
    VkPipelineLayout layout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    vk::plugins::Viewport3DPlugin* viewport_plugin_{nullptr};
};

int main() {
    vk::engine::VulkanEngine engine;
    vk::plugins::Viewport3DPlugin viewport_plugin;
    vk::plugins::ScreenshotPlugin screenshot_plugin;
    WireframeBoxPlugin box_plugin;

    // Wire up plugins
    box_plugin.set_viewport_plugin(&viewport_plugin);

    // Initialize engine with both plugins
    engine.init(viewport_plugin, box_plugin, screenshot_plugin);

    // Run main loop
    engine.run(viewport_plugin, box_plugin, screenshot_plugin);

    // Cleanup
    engine.cleanup();

    return 0;
}

