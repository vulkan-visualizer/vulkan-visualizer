module;
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
export module vk.plugins.viewport;
import vk.engine;
import vk.context;

namespace vk::plugins {
    export class ViewportRenderer {
    public:
        void query_required_device_caps(context::RendererCaps& caps);
        void get_capabilities(context::RendererCaps& caps);
        void initialize(const context::EngineContext& eng, const context::RendererCaps& caps);
        void destroy(const context::EngineContext& eng);
        void record_graphics(VkCommandBuffer& cmd, const context::EngineContext& eng, const context::FrameContext& frm);

    protected:
        void create_pipeline_layout(const context::EngineContext& eng);
        void create_graphics_pipeline(const context::EngineContext& eng);
        void draw_triangle(VkCommandBuffer cmd, VkExtent2D extent) const;

        static void begin_rendering(VkCommandBuffer& cmd, const context::AttachmentView& target, VkExtent2D extent);
        static void end_rendering(VkCommandBuffer& cmd);

    private:
        VkPipelineLayout layout{VK_NULL_HANDLE};
        VkPipeline pipe{VK_NULL_HANDLE};
        VkFormat fmt{VK_FORMAT_B8G8R8A8_UNORM};
        VkDynamicState m_dynamic_states[2]{};
        VkPipelineColorBlendAttachmentState m_color_blend_attachment{};

        struct {
            VkPipelineRenderingCreateInfo rendering_info;
            VkPipelineVertexInputStateCreateInfo vertex_input_state;
            VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
            VkPipelineViewportStateCreateInfo viewport_state;
            VkPipelineRasterizationStateCreateInfo rasterization_state;
            VkPipelineMultisampleStateCreateInfo multisample_state;
            VkPipelineColorBlendStateCreateInfo color_blend_state;
            VkPipelineDynamicStateCreateInfo dynamic_state;
        } m_graphics_pipeline{};
    };
    export class ViewportUI {
    public:
        void create_imgui(context::EngineContext& eng, const context::FrameContext& frm);
        void destroy_imgui(const context::EngineContext& eng);
        void process_event(const SDL_Event& event);
        void record_imgui(VkCommandBuffer& cmd, const context::FrameContext& frm);
    };
    export class ViewpoertPlugin {
    public:
        void initialize();
        void update();
        void shutdown();
    };
} // namespace vk::plugins
