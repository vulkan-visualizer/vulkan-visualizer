module;
#include <SDL3/SDL.h>
#include <imgui.h>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
module vk.plugins.viewport3d;
import vk.toolkit.vulkan;

void vk::plugins::Viewport3D::on_setup(const context::PluginContext& ctx) {
    ctx.caps->allow_async_compute        = false;
    ctx.caps->presentation_mode          = context::PresentationMode::EngineBlit;
    ctx.caps->preferred_swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
    ctx.caps->color_attachments          = {context::AttachmentRequest{.name = "color", .format = VK_FORMAT_B8G8R8A8_UNORM, .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, .samples = VK_SAMPLE_COUNT_1_BIT, .aspect = VK_IMAGE_ASPECT_COLOR_BIT, .initial_layout = VK_IMAGE_LAYOUT_GENERAL}};
    ctx.caps->presentation_attachment    = "color";
}
void vk::plugins::Viewport3D::on_pre_render(context::PluginContext&) {
    const auto current_time = SDL_GetTicks();
    const auto dt           = static_cast<float>(current_time - last_time_ms_) / 1000.0f;
    last_time_ms_           = current_time;
    camera_->update(dt, viewport_width_, viewport_height_);
}
void vk::plugins::Viewport3D::on_render(const context::PluginContext& ctx) {
    const auto& target = ctx.frame->color_attachments.front();
    toolkit::vulkan::transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    constexpr VkClearValue clear_value{.color = {{0.f, 0.f, 0.f, 1.0f}}};
    const VkRenderingAttachmentInfo color_attachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = target.view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = clear_value};
    const VkRenderingInfo render_info{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, ctx.frame->extent}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color_attachment};
    vkCmdBeginRendering(*ctx.cmd, &render_info);

    vkCmdEndRendering(*ctx.cmd);
    toolkit::vulkan::transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
}
void vk::plugins::Viewport3D::on_imgui(context::PluginContext&) const {
    camera_->draw_imgui_panel();
    camera_->draw_mini_axis_gizmo();
}
void vk::plugins::Viewport3D::on_event(const SDL_Event& event) const {
    const auto& io = ImGui::GetIO();
    if (const bool imgui_wants_input = io.WantCaptureMouse || io.WantCaptureKeyboard; !imgui_wants_input) {
        camera_->handle_event(event);
    }
}
void vk::plugins::Viewport3D::on_resize(const uint32_t width, const uint32_t height) {
    viewport_width_  = static_cast<int>(width);
    viewport_height_ = static_cast<int>(height);
}
