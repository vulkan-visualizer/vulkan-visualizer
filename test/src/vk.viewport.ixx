module;
#include <SDL3/SDL.h>
#include <cstdint>
#include <vulkan/vulkan.h>
export module vk.plugins.viewport;
import vk.engine;
import vk.context;
import vk.camera;

namespace vk::plugins {
    export class ViewportRenderer {
    public:
        void query_required_device_caps(context::RendererCaps& caps);
        void get_capabilities(context::RendererCaps& caps);
        void initialize(const context::EngineContext& eng, const context::RendererCaps& caps);
        void destroy(const context::EngineContext& eng);
        void record_graphics(VkCommandBuffer& cmd, const context::EngineContext& eng, const context::FrameContext& frm);

        void set_camera(camera::Camera* cam) { camera_ = cam; }

    protected:
        void create_pipeline_layout(const context::EngineContext& eng);
        void create_graphics_pipeline(const context::EngineContext& eng);
        void draw_cube(VkCommandBuffer cmd, VkExtent2D extent) const;

        static void begin_rendering(VkCommandBuffer& cmd, const context::AttachmentView& target, VkExtent2D extent);
        static void end_rendering(VkCommandBuffer& cmd);

    private:
        VkPipelineLayout layout{VK_NULL_HANDLE};
        VkPipeline pipe{VK_NULL_HANDLE};
        VkFormat fmt{VK_FORMAT_B8G8R8A8_UNORM};
        VkDynamicState m_dynamic_states[2]{};
        VkPipelineColorBlendAttachmentState m_color_blend_attachment{};
        camera::Camera* camera_{nullptr};

        struct {
            VkPipelineRenderingCreateInfo rendering_info;
            VkPipelineVertexInputStateCreateInfo vertex_input_state;
            VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
            VkPipelineViewportStateCreateInfo viewport_state;
            VkPipelineRasterizationStateCreateInfo rasterization_state;
            VkPipelineMultisampleStateCreateInfo multisample_state;
            VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
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

        void set_camera(camera::Camera* cam) { camera_ = cam; }

    private:
        void draw_mini_axis_gizmo() const;
        camera::Camera* camera_{nullptr};
    };
    export class ViewpoertPlugin {
    public:
        void initialize();
        void update();
        void shutdown();
        void handle_event(const SDL_Event& event);
        void set_viewport_size(int width, int height) { viewport_w_ = width; viewport_h_ = height; }

        camera::Camera& get_camera() { return camera_; }

    private:
        camera::Camera camera_;
        int viewport_w_{1920};
        int viewport_h_{1280};
        uint64_t last_time_ms_{0};
    };
} // namespace vk::plugins
