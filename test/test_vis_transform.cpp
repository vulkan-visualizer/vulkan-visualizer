#include <SDL3/SDL.h>
#include <imgui.h>
#include <print>
#include <vector>
#include <vulkan/vulkan.h>
import vk.engine;
import vk.context;
import vk.toolkit.camera;
import vk.toolkit.geometry;
import vk.toolkit.vulkan;

namespace vk::plugins {
    class TransformViewer {
    public:
        [[nodiscard]] static constexpr context::PluginPhase phases() noexcept {
            return context::PluginPhase::Setup | context::PluginPhase::Initialize | context::PluginPhase::PreRender | context::PluginPhase::Render | context::PluginPhase::ImGUI | context::PluginPhase::Cleanup;
        }
        static void on_setup(const context::PluginContext& ctx) {
            if (!ctx.caps) return;
            ctx.caps->allow_async_compute        = false;
            ctx.caps->presentation_mode          = context::PresentationMode::EngineBlit;
            ctx.caps->preferred_swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
            ctx.caps->color_samples              = VK_SAMPLE_COUNT_1_BIT;
            ctx.caps->uses_depth                 = VK_FALSE;
            ctx.caps->color_attachments       = {context::AttachmentRequest{.name = "color", .format = VK_FORMAT_B8G8R8A8_UNORM, .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, .samples = VK_SAMPLE_COUNT_1_BIT, .aspect = VK_IMAGE_ASPECT_COLOR_BIT, .initial_layout = VK_IMAGE_LAYOUT_GENERAL}};
            ctx.caps->presentation_attachment = "color";
        }
        static void on_initialize(context::PluginContext& ctx) {}
        void on_pre_render(context::PluginContext& ctx) const {
            camera_->update(ctx.delta_time, ctx.frame->extent.width, ctx.frame->extent.height);
        }
        static void on_render(context::PluginContext& ctx) {
            constexpr VkClearValue clear_value{.color = {{0.f, 0.f, 0.f, 1.f}}};
            const auto& target = ctx.frame->color_attachments.front();
            toolkit::vulkan::transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            const VkRenderingAttachmentInfo color_attachment{
                .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView   = target.view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue  = clear_value,
            };
            const VkRenderingInfo render_info{
                .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea           = {{0, 0}, ctx.frame->extent},
                .layerCount           = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments    = &color_attachment,
            };
            vkCmdBeginRendering(*ctx.cmd, &render_info);

            vkCmdEndRendering(*ctx.cmd);
            toolkit::vulkan::transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        }
        static void on_post_render(context::PluginContext& ctx) {}
        void on_imgui(context::PluginContext& ctx) const {
            // FPS inspector overlay in top-left corner
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

            if (ImGui::Begin("##FPSOverlay", nullptr, window_flags)) {
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
            }
            ImGui::End();

            this->camera_->draw_mini_axis_gizmo();
        }
        static void on_present(context::PluginContext&) {}
        static void on_cleanup(context::PluginContext& ctx) {}
        void on_event(const SDL_Event& event) const {
            const auto& io = ImGui::GetIO();
            if (const bool imgui_wants_input = io.WantCaptureMouse || io.WantCaptureKeyboard; !imgui_wants_input) {
                this->camera_->handle_event(event);
            }
        }
        static void on_resize(uint32_t width, uint32_t height) {}

        explicit TransformViewer(std::shared_ptr<toolkit::camera::Camera> camera) : camera_(std::move(camera)) {}
        ~TransformViewer()                                     = default;
        TransformViewer(const TransformViewer&)                = delete;
        TransformViewer& operator=(const TransformViewer&)     = delete;
        TransformViewer(TransformViewer&&) noexcept            = default;
        TransformViewer& operator=(TransformViewer&&) noexcept = default;

    protected:
        void create_pipeline(const context::EngineContext& eng, VkFormat color_format) {}
        void destroy_pipeline(const context::EngineContext& eng) {
            if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(eng.device, pipeline_, nullptr);
            if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(eng.device, pipeline_layout_, nullptr);
            pipeline_        = VK_NULL_HANDLE;
            pipeline_layout_ = VK_NULL_HANDLE;
        }

    private:
        std::shared_ptr<toolkit::camera::Camera> camera_{};

        struct MeshBuffer {
            VkBuffer buffer{VK_NULL_HANDLE};
            VkDeviceMemory memory{VK_NULL_HANDLE};
            uint32_t vertex_count{0};
        } mesh_buffer_{};
        VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
        VkPipeline pipeline_{VK_NULL_HANDLE};

        std::vector<toolkit::geometry::ColoredLine> frustum_lines_{};
        std::vector<toolkit::geometry::ColoredLine> axis_lines_{};
        std::vector<toolkit::geometry::ColoredLine> path_lines_{};
    };
} // namespace vk::plugins

int main() {
    vk::engine::VulkanEngine engine;
    vk::plugins::TransformViewer viewer(std::make_shared<vk::toolkit::camera::Camera>());

    engine.init(viewer);
    engine.run(viewer);
    engine.cleanup();
    return 0;
}
