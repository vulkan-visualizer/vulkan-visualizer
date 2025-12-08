module;
#include <SDL3/SDL.h>
#include <array>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <imgui.h>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <stb_image_write.h>

module vk.plugins;
import vk.camera;

namespace {
    constexpr auto reset = "\033[0m";
    constexpr auto bold = "\033[1m";
    constexpr auto red = "\033[31m";
    constexpr auto green = "\033[32m";
    constexpr auto yellow = "\033[33m";
    constexpr auto blue = "\033[34m";
    constexpr auto magenta = "\033[35m";
    constexpr auto cyan = "\033[36m";

    void vk_check(const VkResult result, const char* operation) {
        if (result != VK_SUCCESS) {
            throw std::runtime_error(std::format("{}{}Vulkan Error{}: {} (code: {})",
                bold, red, reset, operation, static_cast<int>(result)));
        }
    }

    void log_plugin(const std::string_view plugin_name, const std::string_view message, const std::string_view color = cyan) {
        std::println("{}{}[{}]{} {}", bold, color, plugin_name, reset, message);
    }

    void log_success(const std::string_view plugin_name, const std::string_view message) {
        log_plugin(plugin_name, message, green);
    }

    void log_info(const std::string_view plugin_name, const std::string_view message) {
        log_plugin(plugin_name, message, cyan);
    }

    void log_warning(const std::string_view plugin_name, const std::string_view message) {
        log_plugin(plugin_name, message, yellow);
    }

    void log_error(const std::string_view plugin_name, const std::string_view message) {
        log_plugin(plugin_name, message, red);
    }

    void transition_image_layout(VkCommandBuffer cmd, const vk::context::AttachmentView& target,
                                  const VkImageLayout old_layout, const VkImageLayout new_layout) {
        const auto [src_stage, dst_stage, src_access, dst_access] = [&] {
            if (old_layout == VK_IMAGE_LAYOUT_GENERAL && new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                return std::tuple{
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                };
            }
            return std::tuple{
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT
            };
        }();

        const VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .image = target.image,
            .subresourceRange = {target.aspect, 0, 1, 0, 1}
        };

        const VkDependencyInfo dep_info{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };

        vkCmdPipelineBarrier2(cmd, &dep_info);
    }

    void transition_to_color_attachment(const VkCommandBuffer cmd, const VkImage image, const VkImageLayout old_layout) {
        const VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
            .oldLayout = old_layout,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };

        const VkDependencyInfo dep{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier
        };

        vkCmdPipelineBarrier2(cmd, &dep);
    }
}

namespace vk::plugins {
    Viewport3DPlugin::Viewport3DPlugin() {
        camera_.home_view();
        last_time_ms_ = SDL_GetTicks();
        log_info(name(), "Plugin created");
    }

    context::PluginPhase Viewport3DPlugin::phases() const noexcept {
        return context::PluginPhase::Setup |
               context::PluginPhase::Initialize |
               context::PluginPhase::PreRender |
               context::PluginPhase::Render |
               context::PluginPhase::PostRender |
               context::PluginPhase::Cleanup;
    }

    void Viewport3DPlugin::on_setup(context::PluginContext& ctx) {
        ctx.caps->allow_async_compute = false;
        ctx.caps->presentation_mode = context::PresentationMode::EngineBlit;
        ctx.caps->preferred_swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
        ctx.caps->color_attachments = {
            context::AttachmentRequest{
                .name = "color",
                .format = VK_FORMAT_B8G8R8A8_UNORM,
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .aspect = VK_IMAGE_ASPECT_COLOR_BIT,
                .initial_layout = VK_IMAGE_LAYOUT_GENERAL
            }
        };
        ctx.caps->presentation_attachment = "color";

        log_success(name(), "Setup complete: renderer configured");
    }

    void Viewport3DPlugin::on_initialize(context::PluginContext& ctx) {
        create_imgui(*ctx.engine, *ctx.frame);
        log_success(name(), "Initialized: UI ready");
    }

    void Viewport3DPlugin::on_pre_render(context::PluginContext& ctx) {
        const auto current_time = SDL_GetTicks();
        const auto dt = static_cast<float>(current_time - last_time_ms_) / 1000.0f;
        last_time_ms_ = current_time;
        camera_.update(dt, viewport_width_, viewport_height_);
    }

    void Viewport3DPlugin::on_render(context::PluginContext& ctx) {
        const auto& target = ctx.frame->color_attachments.front();
        transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        begin_rendering(*ctx.cmd, target, ctx.frame->extent);
        end_rendering(*ctx.cmd);
        transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    void Viewport3DPlugin::on_post_render(context::PluginContext& ctx) {
        render_imgui(*ctx.cmd, *ctx.frame);
    }

    void Viewport3DPlugin::on_cleanup(context::PluginContext& ctx) {
        if (ctx.engine) {
            vkDeviceWaitIdle(ctx.engine->device);
            destroy_imgui(*ctx.engine);
        }
        log_success(name(), "Cleanup complete");
    }

    void Viewport3DPlugin::on_event(const SDL_Event& event) {
        const auto& io = ImGui::GetIO();
        if (const bool imgui_wants_input = io.WantCaptureMouse || io.WantCaptureKeyboard; !imgui_wants_input) {
            camera_.handle_event(event);
        }
    }

    void Viewport3DPlugin::on_resize(const uint32_t width, const uint32_t height) noexcept {
        viewport_width_ = static_cast<int>(width);
        viewport_height_ = static_cast<int>(height);
        log_info(name(), std::format("Viewport resized: {}x{}", width, height));
    }

    void Viewport3DPlugin::begin_rendering(VkCommandBuffer& cmd, const context::AttachmentView& target, const VkExtent2D extent) {
        constexpr VkClearValue clear_value{.color = {{0.1f, 0.1f, 0.12f, 1.0f}}};
        const VkRenderingAttachmentInfo color_attachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = target.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clear_value
        };

        const VkRenderingInfo render_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {{0, 0}, extent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment
        };

        vkCmdBeginRendering(cmd, &render_info);
    }

    void Viewport3DPlugin::end_rendering(VkCommandBuffer& cmd) noexcept {
        vkCmdEndRendering(cmd);
    }

    void Viewport3DPlugin::create_imgui(context::EngineContext& eng, const context::FrameContext& frm) {
        constexpr std::array pool_sizes{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
        };

        const VkDescriptorPoolCreateInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 1000u * static_cast<uint32_t>(pool_sizes.size()),
            .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
            .pPoolSizes = pool_sizes.data()
        };

        vk_check(vkCreateDescriptorPool(eng.device, &pool_info, nullptr, &eng.descriptor_allocator.pool),
                 "Failed to create descriptor pool");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        ImGui::StyleColorsDark();

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            auto& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        if (!ImGui_ImplSDL3_InitForVulkan(eng.window)) {
            throw std::runtime_error("Failed to initialize ImGui SDL3 backend");
        }

        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance = eng.instance;
        init_info.PhysicalDevice = eng.physical;
        init_info.Device = eng.device;
        init_info.QueueFamily = eng.graphics_queue_family;
        init_info.Queue = eng.graphics_queue;
        init_info.DescriptorPool = eng.descriptor_allocator.pool;
        init_info.MinImageCount = context::FRAME_OVERLAP;
        init_info.ImageCount = context::FRAME_OVERLAP;
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.UseDynamicRendering = VK_TRUE;
        init_info.CheckVkResultFn = [](const VkResult res) { vk_check(res, "ImGui Vulkan operation"); };

        VkPipelineRenderingCreateInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &frm.swapchain_format
        };

        init_info.PipelineRenderingCreateInfo = rendering_info;

        if (!ImGui_ImplVulkan_Init(&init_info)) {
            throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
        }

        log_success(name(), "ImGui initialized: docking enabled");
    }

    void Viewport3DPlugin::destroy_imgui(const context::EngineContext& /*eng*/) const {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        log_info(name(), "ImGui destroyed");
    }

    void Viewport3DPlugin::render_imgui(VkCommandBuffer& cmd, const context::FrameContext& frm) {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (show_camera_panel_) {
            draw_camera_panel();
        }

        draw_mini_axis_gizmo();

        ImGui::Render();

        VkImage target_image = VK_NULL_HANDLE;
        VkImageView target_view = VK_NULL_HANDLE;

        if (frm.presentation_mode == context::PresentationMode::DirectToSwapchain) {
            target_image = frm.swapchain_image;
            target_view = frm.swapchain_image_view;
            transition_to_color_attachment(cmd, target_image, VK_IMAGE_LAYOUT_UNDEFINED);
        } else {
            if (!frm.color_attachments.empty()) {
                target_image = frm.color_attachments[0].image;
                target_view = frm.color_attachments[0].view;
                transition_to_color_attachment(cmd, target_image, VK_IMAGE_LAYOUT_GENERAL);
            }
        }

        if (target_image != VK_NULL_HANDLE && target_view != VK_NULL_HANDLE) {
            const VkRenderingAttachmentInfo color_attachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = target_view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE
            };

            const VkRenderingInfo rendering_info{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {{0, 0}, frm.extent},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &color_attachment
            };

            vkCmdBeginRendering(cmd, &rendering_info);
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
            vkCmdEndRendering(cmd);

            if (const auto& io = ImGui::GetIO(); io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }

            if (frm.presentation_mode != context::PresentationMode::DirectToSwapchain) {
                const VkImageMemoryBarrier2 barrier{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .image = target_image,
                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
                };

                const VkDependencyInfo dep{
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .imageMemoryBarrierCount = 1,
                    .pImageMemoryBarriers = &barrier
                };

                vkCmdPipelineBarrier2(cmd, &dep);
            }
        }
    }

    void Viewport3DPlugin::draw_camera_panel() {
        ImGui::Begin("Camera Controls", &show_camera_panel_);

        auto state = camera_.state();
        auto changed = false;

        const auto mode = static_cast<int>(state.mode);
        if (ImGui::RadioButton("Orbit Mode", mode == 0)) {
            state.mode = camera::CameraMode::Orbit;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Fly Mode", mode == 1)) {
            state.mode = camera::CameraMode::Fly;
            changed = true;
        }

        ImGui::Separator();

        if (state.mode == camera::CameraMode::Orbit) {
            ImGui::Text("Orbit Mode Controls:");
            changed |= ImGui::DragFloat3("Target", &state.target.x, 0.01f);
            changed |= ImGui::DragFloat("Distance", &state.distance, 0.01f, 0.1f, 100.0f);
            changed |= ImGui::DragFloat("Yaw", &state.yaw_deg, 0.5f);
            changed |= ImGui::DragFloat("Pitch", &state.pitch_deg, 0.5f, -89.5f, 89.5f);
        } else {
            ImGui::Text("Fly Mode Controls (WASD+QE):");
            changed |= ImGui::DragFloat3("Eye Position", &state.eye.x, 0.01f);
            changed |= ImGui::DragFloat("Yaw", &state.fly_yaw_deg, 0.5f);
            changed |= ImGui::DragFloat("Pitch", &state.fly_pitch_deg, 0.5f, -89.0f, 89.0f);
        }

        ImGui::Separator();
        ImGui::Text("Projection:");
        changed |= ImGui::DragFloat("FOV (deg)", &state.fov_y_deg, 0.5f, 10.0f, 120.0f);
        changed |= ImGui::DragFloat("Near", &state.znear, 0.001f, 0.001f, state.zfar - 0.1f);
        changed |= ImGui::DragFloat("Far", &state.zfar, 1.0f, state.znear + 0.1f, 10000.0f);

        if (ImGui::Button("Home View (H)")) {
            camera_.home_view();
        }

        ImGui::Separator();
        ImGui::Text("Navigation:");
        ImGui::BulletText("Hold Space/Alt + LMB: Rotate");
        ImGui::BulletText("Hold Space/Alt + MMB: Pan");
        ImGui::BulletText("Hold Space/Alt + RMB: Zoom");
        ImGui::BulletText("Mouse Wheel: Zoom");
        ImGui::BulletText("Fly Mode: Hold RMB + WASDQE");

        if (changed) {
            camera_.set_state(state);
        }

        ImGui::End();
    }

    void Viewport3DPlugin::draw_mini_axis_gizmo() const {
        auto* viewport = ImGui::GetMainViewport();
        if (!viewport) return;

        auto* draw_list = ImGui::GetForegroundDrawList(viewport);
        if (!draw_list) return;

        constexpr auto size = 80.0f;
        constexpr auto margin = 16.0f;
        const ImVec2 center(
            viewport->Pos.x + viewport->Size.x - margin - size * 0.5f,
            viewport->Pos.y + margin + size * 0.5f
        );
        constexpr auto radius = size * 0.42f;

        draw_list->AddCircleFilled(center, size * 0.5f, IM_COL32(30, 32, 36, 180), 48);
        draw_list->AddCircle(center, size * 0.5f, IM_COL32(255, 255, 255, 60), 48, 1.5f);

        const auto& view = camera_.view_matrix();

        struct AxisInfo {
            camera::Vec3 direction;
            ImU32 color;
            const char* label;
        };

        constexpr std::array axes{
            AxisInfo{{1, 0, 0}, IM_COL32(255, 80, 80, 255), "X"},
            AxisInfo{{0, 1, 0}, IM_COL32(80, 255, 80, 255), "Y"},
            AxisInfo{{0, 0, 1}, IM_COL32(100, 140, 255, 255), "Z"}
        };

        struct TransformedAxis {
            camera::Vec3 view_dir;
            AxisInfo info;
        };

        std::array<TransformedAxis, 3> transformed{};
        for (size_t i = 0; i < 3; ++i) {
            const auto& dir = axes[i].direction;
            const camera::Vec3 view_dir{
                view.m[0] * dir.x + view.m[4] * dir.y + view.m[8] * dir.z,
                view.m[1] * dir.x + view.m[5] * dir.y + view.m[9] * dir.z,
                view.m[2] * dir.x + view.m[6] * dir.y + view.m[10] * dir.z
            };
            transformed[i] = {view_dir, axes[i]};
        }

        const auto draw_axis = [&](const TransformedAxis& axis, const bool is_back) {
            const auto thickness = is_back ? 2.0f : 3.0f;
            const auto base_color = axis.info.color;
            const auto color = is_back
                ? IM_COL32(
                    (base_color >> IM_COL32_R_SHIFT) & 0xFF,
                    (base_color >> IM_COL32_G_SHIFT) & 0xFF,
                    (base_color >> IM_COL32_B_SHIFT) & 0xFF,
                    120)
                : base_color;

            const ImVec2 end_point(
                center.x + axis.view_dir.x * radius,
                center.y - axis.view_dir.y * radius
            );

            draw_list->AddLine(center, end_point, color, thickness);
            const auto circle_radius = is_back ? 3.0f : 4.5f;
            draw_list->AddCircleFilled(end_point, circle_radius, color, 12);

            if (!is_back) {
                const auto label_offset_x = axis.view_dir.x >= 0 ? 8.0f : -20.0f;
                const auto label_offset_y = axis.view_dir.y >= 0 ? -18.0f : 4.0f;
                const ImVec2 label_pos(
                    end_point.x + label_offset_x,
                    end_point.y + label_offset_y
                );
                draw_list->AddText(label_pos, color, axis.info.label);
            }
        };

        for (const auto& axis : transformed) {
            if (axis.view_dir.z > 0.0f) {
                draw_axis(axis, true);
            }
        }

        for (const auto& axis : transformed) {
            if (axis.view_dir.z <= 0.0f) {
                draw_axis(axis, false);
            }
        }
    }

    context::PluginPhase ScreenshotPlugin::phases() const noexcept {
        return context::PluginPhase::Initialize |
               context::PluginPhase::PreRender |
               context::PluginPhase::Present |
               context::PluginPhase::Cleanup;
    }

    void ScreenshotPlugin::on_initialize(context::PluginContext& /*ctx*/) {
        log_success(name(), "Initialized: Press F1 to capture");
    }

    void ScreenshotPlugin::on_pre_render(context::PluginContext& ctx) {
        if (pending_capture_.buffer != VK_NULL_HANDLE && ctx.engine) {
            vkQueueWaitIdle(ctx.engine->graphics_queue);

            void* pixel_data = nullptr;
            vmaMapMemory(ctx.engine->allocator, pending_capture_.allocation, &pixel_data);

            if (pixel_data) {
                save_screenshot(
                    pixel_data,
                    pending_capture_.width,
                    pending_capture_.height,
                    pending_capture_.output_path
                );

                vmaUnmapMemory(ctx.engine->allocator, pending_capture_.allocation);
            }

            vmaDestroyBuffer(ctx.engine->allocator, pending_capture_.buffer, pending_capture_.allocation);
            pending_capture_ = {};
        }
    }

    void ScreenshotPlugin::on_present(context::PluginContext& ctx) {
        if (!screenshot_requested_) return;

        const auto image_index = ctx.frame->image_index;
        capture_swapchain(ctx, image_index);
        screenshot_requested_ = false;
    }

    void ScreenshotPlugin::on_cleanup(context::PluginContext& ctx) {
        if (pending_capture_.buffer != VK_NULL_HANDLE && ctx.engine) {
            vmaDestroyBuffer(ctx.engine->allocator, pending_capture_.buffer, pending_capture_.allocation);
            pending_capture_ = {};
        }
        log_success(name(), "Cleanup complete");
    }

    void ScreenshotPlugin::on_event(const SDL_Event& event) {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1) {
            request_screenshot();
        }
    }

    void ScreenshotPlugin::request_screenshot() {
        screenshot_requested_ = true;
        log_info(name(), "Screenshot requested");
    }

    void ScreenshotPlugin::request_screenshot(const ScreenshotConfig& config) {
        config_ = config;
        request_screenshot();
    }

    void ScreenshotPlugin::capture_swapchain(context::PluginContext& ctx, const uint32_t /*image_index*/) {
        if (!ctx.engine || !ctx.cmd || !ctx.frame) return;

        const auto width = ctx.frame->extent.width;
        const auto height = ctx.frame->extent.height;
        const auto img = ctx.frame->swapchain_image;

        if (img == VK_NULL_HANDLE) {
            log_error(name(), "Invalid swapchain image");
            return;
        }

        const auto buffer_size = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;

        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation alloc{};
        VmaAllocationInfo alloc_info{};

        const VkBufferCreateInfo buffer_ci{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buffer_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        constexpr VmaAllocationCreateInfo alloc_ci{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };

        vk_check(vmaCreateBuffer(ctx.engine->allocator, &buffer_ci, &alloc_ci, &buffer, &alloc, &alloc_info),
                 "Failed to create screenshot buffer");

        const VkImageMemoryBarrier2 to_src{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .image = img,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };

        const VkDependencyInfo dep_to_src{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &to_src
        };

        vkCmdPipelineBarrier2(*ctx.cmd, &dep_to_src);

        const VkBufferImageCopy region{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, 1}
        };

        vkCmdCopyImageToBuffer(*ctx.cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);

        const VkImageMemoryBarrier2 back_present{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = img,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };

        const VkDependencyInfo dep_back{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &back_present
        };

        vkCmdPipelineBarrier2(*ctx.cmd, &dep_back);

        pending_capture_ = {buffer, alloc, width, height, generate_filename()};

        log_info(name(), std::format("Capture queued: {}x{}", width, height));
    }

    void ScreenshotPlugin::save_screenshot(void* pixel_data, const uint32_t width, const uint32_t height,
                                           const std::string& path) const {
        if (!pixel_data) return;

        const auto* bgra = static_cast<const uint8_t*>(pixel_data);
        std::vector<uint8_t> rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

        for (size_t i = 0, n = static_cast<size_t>(width) * static_cast<size_t>(height); i < n; ++i) {
            rgba[i * 4 + 0] = bgra[i * 4 + 2];
            rgba[i * 4 + 1] = bgra[i * 4 + 1];
            rgba[i * 4 + 2] = bgra[i * 4 + 0];
            rgba[i * 4 + 3] = bgra[i * 4 + 3];
        }

        switch (config_.format) {
        case ScreenshotFormat::PNG:
            stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                          rgba.data(), static_cast<int>(width) * 4);
            break;
        case ScreenshotFormat::JPG:
            stbi_write_jpg(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                          rgba.data(), config_.jpeg_quality);
            break;
        case ScreenshotFormat::BMP:
            stbi_write_bmp(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data());
            break;
        case ScreenshotFormat::TGA:
            stbi_write_tga(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data());
            break;
        }

        log_success(name(), std::format("Saved: {}", path));
    }

    std::string ScreenshotPlugin::generate_filename() const {
        if (!config_.auto_filename) {
            return std::format("{}/{}", config_.output_directory, config_.filename_prefix);
        }

        const auto now = std::chrono::system_clock::now();
        const auto time_t_now = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &time_t_now);
#else
        localtime_r(&time_t_now, &tm);
#endif

        std::ostringstream oss;
        oss << config_.output_directory << "/"
            << config_.filename_prefix << "_"
            << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_"
            << std::setfill('0') << std::setw(3) << ms.count();

        switch (config_.format) {
        case ScreenshotFormat::PNG: oss << ".png"; break;
        case ScreenshotFormat::JPG: oss << ".jpg"; break;
        case ScreenshotFormat::BMP: oss << ".bmp"; break;
        case ScreenshotFormat::TGA: oss << ".tga"; break;
        }

        return oss.str();
    }

    // ============================================================================
    // Geometry Plugin Implementation
    // ============================================================================

    GeometryPlugin::GeometryPlugin() {
        log_info(name(), "Plugin created");
    }

    context::PluginPhase GeometryPlugin::phases() const noexcept {
        return context::PluginPhase::Setup |
               context::PluginPhase::Initialize |
               context::PluginPhase::PreRender |
               context::PluginPhase::Render |
               context::PluginPhase::Cleanup;
    }

    void GeometryPlugin::on_setup(context::PluginContext& ctx) {
        if (!ctx.caps) return;

        ctx.caps->uses_depth = VK_TRUE;
        if (!ctx.caps->depth_attachment) {
            ctx.caps->depth_attachment = context::AttachmentRequest{
                .name = "depth",
                .format = ctx.caps->preferred_depth_format,
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                .samples = ctx.caps->color_samples,
                .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
                .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED
            };
        } else {
            ctx.caps->depth_attachment->usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            if (ctx.caps->depth_attachment->aspect == 0) {
                ctx.caps->depth_attachment->aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            }
        }
    }

    void GeometryPlugin::on_initialize(context::PluginContext& ctx) {
        if (!ctx.frame || ctx.frame->color_attachments.empty()) {
            throw std::runtime_error("GeometryPlugin requires at least one color attachment");
        }

        color_format_ = ctx.frame->color_attachments.front().format;
        depth_format_ = ctx.frame->depth_attachment ? ctx.frame->depth_attachment->format : VK_FORMAT_UNDEFINED;
        depth_layout_ = ctx.frame->depth_attachment ? ctx.frame->depth_attachment->current_layout : VK_IMAGE_LAYOUT_UNDEFINED;

        create_pipelines(*ctx.engine, color_format_, depth_format_);
        create_geometry_meshes(*ctx.engine);
        log_success(name(), "Initialized - geometry meshes and pipelines ready");
    }


    void GeometryPlugin::on_pre_render(context::PluginContext& ctx) {
        if (!enabled_ || batches_.empty()) return;
        update_instance_buffers(*ctx.engine);
    }

    void GeometryPlugin::on_render(context::PluginContext& ctx) {
        if (!enabled_ || batches_.empty() || !viewport_plugin_) return;
        if (!ctx.frame || ctx.frame->color_attachments.empty() || !ctx.cmd) return;

        auto& cmd = *ctx.cmd;
        const auto& target = ctx.frame->color_attachments.front();

        transition_image_layout(cmd, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        constexpr VkClearValue clear_value{.color = {{0.1f, 0.1f, 0.12f, 1.0f}}};
        VkRenderingAttachmentInfo color_attachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = target.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clear_value
        };

        VkRenderingAttachmentInfo depth_attachment{};
        bool has_depth = ctx.frame->depth_attachment && ctx.frame->depth_attachment->view != VK_NULL_HANDLE;
        if (has_depth) {
            const auto* depth = ctx.frame->depth_attachment;
            const VkImageMemoryBarrier2 depth_barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = depth_layout_,
                .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .image = depth->image,
                .subresourceRange = {depth->aspect, 0, 1, 0, 1}
            };

            const VkDependencyInfo dep{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &depth_barrier
            };

            vkCmdPipelineBarrier2(cmd, &dep);
            depth_layout_ = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

            depth_attachment = VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = depth->view,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.depthStencil = {1.0f, 0}}
            };
        }

        const VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {{0, 0}, ctx.frame->extent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment,
            .pDepthAttachment = has_depth ? &depth_attachment : nullptr
        };

        vkCmdBeginRendering(cmd, &rendering_info);

        const VkViewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(ctx.frame->extent.width),
            .height = static_cast<float>(ctx.frame->extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        const VkRect2D scissor{{0, 0}, ctx.frame->extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const auto& camera = viewport_plugin_->get_camera();
        const auto view_proj = camera.proj_matrix() * camera.view_matrix();

        // Render all batches with instancing
        for (size_t i = 0; i < batches_.size(); ++i) {
            if (!batches_[i].instances.empty() && i < instance_buffers_.size()) {
                render_batch(cmd, batches_[i], instance_buffers_[i], view_proj);
            }
        }

        vkCmdEndRendering(cmd);

        transition_image_layout(cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }
    void GeometryPlugin::on_cleanup(context::PluginContext& ctx) {
        if (ctx.engine) {
            vkDeviceWaitIdle(ctx.engine->device);
            destroy_geometry_meshes(*ctx.engine);
            destroy_pipelines(*ctx.engine);
        }
        log_success(name(), "Cleanup complete");
    }

    void GeometryPlugin::add_batch(const GeometryBatch& batch) {
        batches_.push_back(batch);
    }

    void GeometryPlugin::clear_batches() {
        batches_.clear();
    }

    void GeometryPlugin::add_sphere(const camera::Vec3& position, const float radius,
                                    const camera::Vec3& color, const RenderMode mode) {
        GeometryBatch batch{GeometryType::Sphere, mode};
        batch.instances.push_back({position, {0, 0, 0}, {radius, radius, radius}, color, 1.0f});
        add_batch(batch);
    }

    void GeometryPlugin::add_box(const camera::Vec3& position, const camera::Vec3& size,
                                 const camera::Vec3& color, const RenderMode mode) {
        GeometryBatch batch{GeometryType::Box, mode};
        batch.instances.push_back({position, {0, 0, 0}, size, color, 1.0f});
        add_batch(batch);
    }

    void GeometryPlugin::add_line(const camera::Vec3& start, const camera::Vec3& end, const camera::Vec3& color) {
        const auto mid = (start + end) * 0.5f;
        const auto diff = end - start;
        const auto length = diff.length();

        if (length < 1e-6f) return; // Skip degenerate lines

        // Calculate rotation to align line with direction
        const auto dir = diff / length;

        // Calculate euler angles to rotate from X-axis to direction
        // Yaw (rotation around Y axis)
        const float yaw = std::atan2(dir.z, dir.x) * 180.0f / 3.14159265359f;

        // Pitch (rotation around Z axis)
        const float pitch = std::asin(-dir.y) * 180.0f / 3.14159265359f;

        GeometryBatch batch{GeometryType::Line, RenderMode::Wireframe};
        batch.instances.push_back({mid, {0, yaw, pitch}, {length, 1, 1}, color, 1.0f});
        add_batch(batch);
    }

    void GeometryPlugin::add_ray(const camera::Vec3& origin, const camera::Vec3& direction,
                                 const float length, const camera::Vec3& color) {
        add_line(origin, origin + direction.normalized() * length, color);
    }

    void GeometryPlugin::add_grid(const camera::Vec3& position, const float size,
                                  const int divisions, const camera::Vec3& color) {
        const float step = size / static_cast<float>(divisions);
        const float half_size = size * 0.5f;

        // Generate grid lines along X axis (parallel to Z)
        for (int i = 0; i <= divisions; ++i) {
            const float offset = -half_size + i * step;
            const camera::Vec3 start = position + camera::Vec3{-half_size, 0, offset};
            const camera::Vec3 end = position + camera::Vec3{half_size, 0, offset};
            add_line(start, end, color);
        }

        // Generate grid lines along Z axis (parallel to X)
        for (int i = 0; i <= divisions; ++i) {
            const float offset = -half_size + i * step;
            const camera::Vec3 start = position + camera::Vec3{offset, 0, -half_size};
            const camera::Vec3 end = position + camera::Vec3{offset, 0, half_size};
            add_line(start, end, color);
        }
    }

    // ============================================================================
    // Advanced Ray Visualization Methods for NeRF Debugging
    // ============================================================================

    void GeometryPlugin::add_camera_frustum(const camera::Vec3& position, const camera::Vec3& forward,
                                            const camera::Vec3& up, float fov_deg, float aspect,
                                            float near_dist, float far_dist, const camera::Vec3& color) {
        // Calculate frustum corners
        const float fov_rad = fov_deg * 3.14159265359f / 180.0f;
        const float tan_half_fov = std::tan(fov_rad * 0.5f);

        const auto right = forward.cross(up).normalized();
        const auto true_up = right.cross(forward).normalized();

        // Near plane
        const float near_height = 2.0f * tan_half_fov * near_dist;
        const float near_width = near_height * aspect;
        const auto near_center = position + forward * near_dist;

        // Far plane
        const float far_height = 2.0f * tan_half_fov * far_dist;
        const float far_width = far_height * aspect;
        const auto far_center = position + forward * far_dist;

        // Near corners
        const auto ntr = near_center + true_up * (near_height * 0.5f) + right * (near_width * 0.5f);
        const auto ntl = near_center + true_up * (near_height * 0.5f) - right * (near_width * 0.5f);
        const auto nbr = near_center - true_up * (near_height * 0.5f) + right * (near_width * 0.5f);
        const auto nbl = near_center - true_up * (near_height * 0.5f) - right * (near_width * 0.5f);

        // Far corners
        const auto ftr = far_center + true_up * (far_height * 0.5f) + right * (far_width * 0.5f);
        const auto ftl = far_center + true_up * (far_height * 0.5f) - right * (far_width * 0.5f);
        const auto fbr = far_center - true_up * (far_height * 0.5f) + right * (far_width * 0.5f);
        const auto fbl = far_center - true_up * (far_height * 0.5f) - right * (far_width * 0.5f);

        // Draw frustum edges
        // Near plane
        add_line(ntl, ntr, color);
        add_line(ntr, nbr, color);
        add_line(nbr, nbl, color);
        add_line(nbl, ntl, color);

        // Far plane
        add_line(ftl, ftr, color);
        add_line(ftr, fbr, color);
        add_line(fbr, fbl, color);
        add_line(fbl, ftl, color);

        // Connecting edges
        add_line(ntl, ftl, color);
        add_line(ntr, ftr, color);
        add_line(nbr, fbr, color);
        add_line(nbl, fbl, color);

        // Camera position marker
        add_sphere(position, near_dist * 0.1f, color, RenderMode::Filled);
    }

    void GeometryPlugin::add_image_plane(const camera::Vec3& camera_pos, const camera::Vec3& forward,
                                         const camera::Vec3& up, float fov_deg, float aspect,
                                         float distance, int grid_divisions, const camera::Vec3& color) {
        const float fov_rad = fov_deg * 3.14159265359f / 180.0f;
        const float tan_half_fov = std::tan(fov_rad * 0.5f);

        const auto right = forward.cross(up).normalized();
        const auto true_up = right.cross(forward).normalized();

        const float plane_height = 2.0f * tan_half_fov * distance;
        const float plane_width = plane_height * aspect;

        const auto plane_center = camera_pos + forward * distance;

        // Draw grid on image plane
        for (int i = 0; i <= grid_divisions; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(grid_divisions);
            const float offset = (t - 0.5f) * plane_height;

            // Horizontal lines
            const auto h_start = plane_center + true_up * offset - right * (plane_width * 0.5f);
            const auto h_end = plane_center + true_up * offset + right * (plane_width * 0.5f);
            add_line(h_start, h_end, color);
        }

        for (int i = 0; i <= grid_divisions; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(grid_divisions);
            const float offset = (t - 0.5f) * plane_width;

            // Vertical lines
            const auto v_start = plane_center + right * offset - true_up * (plane_height * 0.5f);
            const auto v_end = plane_center + right * offset + true_up * (plane_height * 0.5f);
            add_line(v_start, v_end, color);
        }
    }

    void GeometryPlugin::add_aabb(const camera::Vec3& min, const camera::Vec3& max,
                                   const camera::Vec3& color, RenderMode mode) {
        const auto center = (min + max) * 0.5f;
        const auto size = max - min;
        add_box(center, size, color, mode);
    }

    void GeometryPlugin::add_ray_with_aabb_intersection(const camera::Vec3& ray_origin, const camera::Vec3& ray_dir,
                                                         float ray_length, const camera::Vec3& aabb_min,
                                                         const camera::Vec3& aabb_max, const camera::Vec3& ray_color,
                                                         const camera::Vec3& hit_color) {
        // Ray-AABB intersection test (slab method)
        const auto dir_normalized = ray_dir.normalized();

        float t_min = 0.0f;
        float t_max = ray_length;

        bool intersects = true;

        // Test all three slabs
        for (int i = 0; i < 3; ++i) {
            const float origin_comp = (i == 0) ? ray_origin.x : (i == 1) ? ray_origin.y : ray_origin.z;
            const float dir_comp = (i == 0) ? dir_normalized.x : (i == 1) ? dir_normalized.y : dir_normalized.z;
            const float min_comp = (i == 0) ? aabb_min.x : (i == 1) ? aabb_min.y : aabb_min.z;
            const float max_comp = (i == 0) ? aabb_max.x : (i == 1) ? aabb_max.y : aabb_max.z;

            if (std::abs(dir_comp) < 1e-8f) {
                // Ray is parallel to slab
                if (origin_comp < min_comp || origin_comp > max_comp) {
                    intersects = false;
                    break;
                }
            } else {
                const float t1 = (min_comp - origin_comp) / dir_comp;
                const float t2 = (max_comp - origin_comp) / dir_comp;

                const float t_near = std::min(t1, t2);
                const float t_far = std::max(t1, t2);

                t_min = std::max(t_min, t_near);
                t_max = std::min(t_max, t_far);

                if (t_min > t_max) {
                    intersects = false;
                    break;
                }
            }
        }

        if (intersects && t_min >= 0.0f && t_min <= ray_length) {
            // Draw ray up to intersection point
            const auto hit_point = ray_origin + dir_normalized * t_min;
            add_line(ray_origin, hit_point, ray_color);

            // Mark intersection point
            add_sphere(hit_point, 0.05f, hit_color, RenderMode::Filled);

            // Draw ray segment inside AABB if t_max is valid
            if (t_max >= 0.0f && t_max <= ray_length && t_max > t_min) {
                const auto exit_point = ray_origin + dir_normalized * t_max;
                add_line(hit_point, exit_point, hit_color);
                add_sphere(exit_point, 0.05f, hit_color, RenderMode::Filled);

                // Continue ray after exit
                if (t_max < ray_length) {
                    const auto ray_end = ray_origin + dir_normalized * ray_length;
                    add_line(exit_point, ray_end, ray_color * 0.5f);
                }
            }
        } else {
            // No intersection, draw full ray
            const auto ray_end = ray_origin + dir_normalized * ray_length;
            add_line(ray_origin, ray_end, ray_color);
        }
    }

    void GeometryPlugin::add_coordinate_axes(const camera::Vec3& position, float size) {
        // X axis (red)
        add_line(position, position + camera::Vec3{size, 0, 0}, {1, 0, 0});
        add_sphere(position + camera::Vec3{size, 0, 0}, size * 0.05f, {1, 0, 0}, RenderMode::Filled);

        // Y axis (green)
        add_line(position, position + camera::Vec3{0, size, 0}, {0, 1, 0});
        add_sphere(position + camera::Vec3{0, size, 0}, size * 0.05f, {0, 1, 0}, RenderMode::Filled);

        // Z axis (blue)
        add_line(position, position + camera::Vec3{0, 0, size}, {0, 0, 1});
        add_sphere(position + camera::Vec3{0, 0, size}, size * 0.05f, {0, 0, 1}, RenderMode::Filled);
    }

    void GeometryPlugin::add_ray_batch(const std::vector<RayInfo>& rays, const camera::Vec3* aabb_min,
                                       const camera::Vec3* aabb_max) {
        if (aabb_min && aabb_max) {
            // Use ray-AABB intersection for each ray
            for (const auto& ray : rays) {
                add_ray_with_aabb_intersection(ray.origin, ray.direction, ray.length,
                                               *aabb_min, *aabb_max, ray.color, {0, 1, 0});
            }
        } else {
            // Just draw rays
            for (const auto& ray : rays) {
                add_ray(ray.origin, ray.direction, ray.length, ray.color);
            }
        }
    }

    void GeometryPlugin::on_resize(const uint32_t /*width*/, const uint32_t /*height*/) noexcept {
        depth_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
        color_format_ = VK_FORMAT_UNDEFINED;
        depth_format_ = VK_FORMAT_UNDEFINED;
    }

    // Helper function to create buffer
    namespace {
        void create_buffer_with_data(const context::EngineContext& eng, const void* data, VkDeviceSize size,
                                    VkBufferUsageFlags usage, VkBuffer& buffer, VmaAllocation& allocation) {
            const VkBufferCreateInfo buffer_ci{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = size,
                .usage = usage,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE
            };

            constexpr VmaAllocationCreateInfo alloc_ci{
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            };

            VmaAllocationInfo alloc_info{};
            vk_check(vmaCreateBuffer(eng.allocator, &buffer_ci, &alloc_ci, &buffer, &allocation, &alloc_info),
                     "Failed to create geometry buffer");

            void* mapped = nullptr;
            vmaMapMemory(eng.allocator, allocation, &mapped);
            std::memcpy(mapped, data, size);
            vmaUnmapMemory(eng.allocator, allocation);
        }
    }

    void GeometryPlugin::destroy_mesh(const context::EngineContext& eng, GeometryMesh& mesh) const {
        if (mesh.vertex_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(eng.allocator, mesh.vertex_buffer, mesh.vertex_allocation);
            mesh.vertex_buffer = VK_NULL_HANDLE;
        }
        if (mesh.index_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(eng.allocator, mesh.index_buffer, mesh.index_allocation);
            mesh.index_buffer = VK_NULL_HANDLE;
        }
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_face_normal_mesh(
        const context::EngineContext& eng,
        const std::vector<float>& vertices,
        const std::vector<uint32_t>& indices) const {

        constexpr uint32_t stride = 6; // position + normal
        if (vertices.size() < stride || indices.size() < 3) {
            return {};
        }

        std::vector<float> line_vertices;
        std::vector<uint32_t> line_indices;
        line_vertices.reserve(indices.size() * 4); // rough guess
        line_indices.reserve(indices.size() * 2);

        const auto normalize = [](const camera::Vec3& v) {
            const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            return len > 0.0f ? v / len : camera::Vec3{0, 1, 0};
        };

        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const auto i0 = static_cast<size_t>(indices[i]);
            const auto i1 = static_cast<size_t>(indices[i + 1]);
            const auto i2 = static_cast<size_t>(indices[i + 2]);
            if ((i0 + 1) * stride > vertices.size() ||
                (i1 + 1) * stride > vertices.size() ||
                (i2 + 1) * stride > vertices.size()) {
                continue;
            }

            const camera::Vec3 p0{vertices[i0 * stride + 0], vertices[i0 * stride + 1], vertices[i0 * stride + 2]};
            const camera::Vec3 p1{vertices[i1 * stride + 0], vertices[i1 * stride + 1], vertices[i1 * stride + 2]};
            const camera::Vec3 p2{vertices[i2 * stride + 0], vertices[i2 * stride + 1], vertices[i2 * stride + 2]};

            const auto edge1 = p1 - p0;
            const auto edge2 = p2 - p0;
            const auto normal = normalize(edge1.cross(edge2));
            const auto center = (p0 + p1 + p2) / 3.0f;

            const auto start = center;
            const auto end = center + normal * normal_length_;

            const uint32_t base_index = static_cast<uint32_t>(line_vertices.size() / stride);

            // start
            line_vertices.push_back(start.x);
            line_vertices.push_back(start.y);
            line_vertices.push_back(start.z);
            line_vertices.push_back(normal.x);
            line_vertices.push_back(normal.y);
            line_vertices.push_back(normal.z);
            // end
            line_vertices.push_back(end.x);
            line_vertices.push_back(end.y);
            line_vertices.push_back(end.z);
            line_vertices.push_back(normal.x);
            line_vertices.push_back(normal.y);
            line_vertices.push_back(normal.z);

            line_indices.push_back(base_index);
            line_indices.push_back(base_index + 1);
        }

        if (line_vertices.empty()) {
            return {};
        }

        GeometryMesh mesh;
        mesh.vertex_count = static_cast<uint32_t>(line_vertices.size() / stride);
        mesh.index_count = static_cast<uint32_t>(line_indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

        create_buffer_with_data(eng, line_vertices.data(), line_vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, line_indices.data(), line_indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    // ============================================================================
    // GeometryPlugin Pipeline and Rendering Implementation
    // ============================================================================

    void GeometryPlugin::create_pipelines(const context::EngineContext& eng, const VkFormat color_format, const VkFormat depth_format) {
        // Load shaders
        auto load_shader = [&](const char* filename) -> VkShaderModule {
            std::ifstream file(std::string("shader/") + filename, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                throw std::runtime_error(std::format("Failed to open shader file: {}", filename));
            }

            const size_t file_size = file.tellg();
            std::vector<char> code(file_size);
            file.seekg(0);
            file.read(code.data(), static_cast<std::streamsize>(file_size));

            const VkShaderModuleCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = code.size(),
                .pCode = reinterpret_cast<const uint32_t*>(code.data())
            };

            VkShaderModule module = VK_NULL_HANDLE;
            vk_check(vkCreateShaderModule(eng.device, &create_info, nullptr, &module),
                    "Failed to create shader module");
            return module;
        };

        const auto vert_module = load_shader("geometry.vert.spv");
        const auto frag_module = load_shader("geometry.frag.spv");

        // Push constant for MVP matrix
        const VkPushConstantRange push_constant{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(float) * 16
        };

        const VkPipelineLayoutCreateInfo layout_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant
        };

        vk_check(vkCreatePipelineLayout(eng.device, &layout_info, nullptr, &pipeline_layout_),
                "Failed to create geometry pipeline layout");

        const VkPipelineShaderStageCreateInfo vert_stage{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_module,
            .pName = "main"
        };

        const VkPipelineShaderStageCreateInfo frag_stage{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_module,
            .pName = "main"
        };

        const VkPipelineShaderStageCreateInfo filled_stages[] = {vert_stage, frag_stage};
        const VkPipelineShaderStageCreateInfo wire_stages[] = {vert_stage, frag_stage};
        const VkPipelineShaderStageCreateInfo line_stages[] = {vert_stage, frag_stage};

        // Vertex input bindings (per-vertex and per-instance)
        const VkVertexInputBindingDescription bindings[] = {
            {0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX},     // position + normal
            {1, sizeof(float) * 13, VK_VERTEX_INPUT_RATE_INSTANCE}  // instance data
        };

        const VkVertexInputAttributeDescription attributes[] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},                  // position
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3},  // normal
            {2, 1, VK_FORMAT_R32G32B32_SFLOAT, 0},                  // instance position
            {3, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3},  // instance rotation
            {4, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 6},  // instance scale
            {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 9} // instance color + alpha
        };

        const VkPipelineVertexInputStateCreateInfo vertex_input{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 2,
            .pVertexBindingDescriptions = bindings,
            .vertexAttributeDescriptionCount = 6,
            .pVertexAttributeDescriptions = attributes
        };

        const VkPipelineInputAssemblyStateCreateInfo input_assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
        };

        const VkPipelineViewportStateCreateInfo viewport_state{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1
        };

        const VkPipelineRasterizationStateCreateInfo rasterizer{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,  // Disable culling to see both front and back faces
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f
        };

        const VkPipelineMultisampleStateCreateInfo multisampling{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
        };

        const bool has_depth = depth_format != VK_FORMAT_UNDEFINED;
        const VkPipelineDepthStencilStateCreateInfo depth_stencil{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = has_depth ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = has_depth ? VK_TRUE : VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS
        };

        const VkPipelineColorBlendAttachmentState color_blend_attachment{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        const VkPipelineColorBlendStateCreateInfo color_blending{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &color_blend_attachment
        };

        constexpr VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        const VkPipelineDynamicStateCreateInfo dynamic_state{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = dynamic_states
        };

        const VkPipelineRenderingCreateInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &color_format,
            .depthAttachmentFormat = depth_format
        };

        const VkGraphicsPipelineCreateInfo pipeline_info{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rendering_info,
            .stageCount = 2,
            .pStages = filled_stages,
            .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depth_stencil,
            .pColorBlendState = &color_blending,
            .pDynamicState = &dynamic_state,
            .layout = pipeline_layout_
        };

        vk_check(vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &filled_pipeline_),
                "Failed to create filled geometry pipeline");

        // Create wireframe pipeline
        auto wireframe_rasterizer = rasterizer;
        wireframe_rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        wireframe_rasterizer.cullMode = VK_CULL_MODE_NONE;

        auto wireframe_pipeline_info = pipeline_info;
        wireframe_pipeline_info.pRasterizationState = &wireframe_rasterizer;
        wireframe_pipeline_info.pStages = wire_stages;

        vk_check(vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &wireframe_pipeline_info, nullptr, &wireframe_pipeline_),
                "Failed to create wireframe geometry pipeline");

        // Create line pipeline for LINE_LIST topology
        const VkPipelineInputAssemblyStateCreateInfo line_input_assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST
        };

        auto line_rasterizer = rasterizer;
        line_rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        line_rasterizer.cullMode = VK_CULL_MODE_NONE;
        line_rasterizer.lineWidth = 2.0f;

        auto line_pipeline_info = pipeline_info;
        line_pipeline_info.pInputAssemblyState = &line_input_assembly;
        line_pipeline_info.pRasterizationState = &line_rasterizer;
        line_pipeline_info.pStages = line_stages;

        vk_check(vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &line_pipeline_info, nullptr, &line_pipeline_),
                "Failed to create line geometry pipeline");

        vkDestroyShaderModule(eng.device, vert_module, nullptr);
        vkDestroyShaderModule(eng.device, frag_module, nullptr);

        log_success(name(), "Pipelines created: filled, wireframe, and line modes ready");
    }

    void GeometryPlugin::destroy_pipelines(const context::EngineContext& eng) {
        if (filled_pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(eng.device, filled_pipeline_, nullptr);
            filled_pipeline_ = VK_NULL_HANDLE;
        }
        if (wireframe_pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(eng.device, wireframe_pipeline_, nullptr);
            wireframe_pipeline_ = VK_NULL_HANDLE;
        }
        if (line_pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(eng.device, line_pipeline_, nullptr);
            line_pipeline_ = VK_NULL_HANDLE;
        }
        if (pipeline_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(eng.device, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
        }
    }

    void GeometryPlugin::create_geometry_meshes(const context::EngineContext& eng) {
        {
            std::vector<float> v; std::vector<uint32_t> i;
            geometry_meshes_[GeometryType::Sphere] = create_sphere_mesh(eng, 32, &v, &i);
            normal_meshes_[GeometryType::Sphere] = create_face_normal_mesh(eng, v, i);
        }
        {
            std::vector<float> v; std::vector<uint32_t> i;
            geometry_meshes_[GeometryType::Box] = create_box_mesh(eng, &v, &i);
            normal_meshes_[GeometryType::Box] = create_face_normal_mesh(eng, v, i);
        }
        {
            std::vector<float> v; std::vector<uint32_t> i;
            geometry_meshes_[GeometryType::Cylinder] = create_cylinder_mesh(eng, 32, &v, &i);
            normal_meshes_[GeometryType::Cylinder] = create_face_normal_mesh(eng, v, i);
        }
        {
            std::vector<float> v; std::vector<uint32_t> i;
            geometry_meshes_[GeometryType::Cone] = create_cone_mesh(eng, 32, &v, &i);
            normal_meshes_[GeometryType::Cone] = create_face_normal_mesh(eng, v, i);
        }
        {
            std::vector<float> v; std::vector<uint32_t> i;
            geometry_meshes_[GeometryType::Torus] = create_torus_mesh(eng, 32, 16, &v, &i);
            normal_meshes_[GeometryType::Torus] = create_face_normal_mesh(eng, v, i);
        }
        {
            std::vector<float> v; std::vector<uint32_t> i;
            geometry_meshes_[GeometryType::Capsule] = create_capsule_mesh(eng, 16, &v, &i);
            normal_meshes_[GeometryType::Capsule] = create_face_normal_mesh(eng, v, i);
        }
        {
            std::vector<float> v; std::vector<uint32_t> i;
            geometry_meshes_[GeometryType::Plane] = create_plane_mesh(eng, &v, &i);
            normal_meshes_[GeometryType::Plane] = create_face_normal_mesh(eng, v, i);
        }
        {
            std::vector<float> v; std::vector<uint32_t> i;
            geometry_meshes_[GeometryType::Circle] = create_circle_mesh(eng, 32, &v, &i);
            normal_meshes_[GeometryType::Circle] = create_face_normal_mesh(eng, v, i);
        }
        geometry_meshes_[GeometryType::Line] = create_line_mesh(eng);
        geometry_meshes_[GeometryType::Grid] = create_line_mesh(eng);
        geometry_meshes_[GeometryType::Ray] = create_line_mesh(eng);
    }

    void GeometryPlugin::destroy_geometry_meshes(const context::EngineContext& eng) {
        std::println("[CLEANUP]     Freeing {} geometry mesh types", geometry_meshes_.size());
        for (auto& [type, mesh] : geometry_meshes_) {
            destroy_mesh(eng, mesh);
        }
        geometry_meshes_.clear();

        std::println("[CLEANUP]     Freeing {} normal debug mesh types", normal_meshes_.size());
        for (auto& [type, mesh] : normal_meshes_) {
            destroy_mesh(eng, mesh);
        }
        normal_meshes_.clear();

        std::println("[CLEANUP]     Freeing {} instance buffers", instance_buffers_.size());
        for (size_t i = 0; i < instance_buffers_.size(); ++i) {
            auto& inst_buf = instance_buffers_[i];
            if (inst_buf.buffer != VK_NULL_HANDLE) {
                std::println("[CLEANUP]       Destroying instance buffer {}", i);
                vmaDestroyBuffer(eng.allocator, inst_buf.buffer, inst_buf.allocation);
            }
        }
        instance_buffers_.clear();
        std::println("[CLEANUP]     All geometry VMA resources freed");
    }

    void GeometryPlugin::update_instance_buffers(const context::EngineContext& eng) {
        // Resize instance buffers vector if needed
        if (instance_buffers_.size() < batches_.size()) {
            instance_buffers_.resize(batches_.size());
        }

        for (size_t i = 0; i < batches_.size(); ++i) {
            const auto& batch = batches_[i];
            auto& inst_buf = instance_buffers_[i];

            if (batch.instances.empty()) continue;

            const uint32_t required_capacity = static_cast<uint32_t>(batch.instances.size());
            const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(required_capacity) * sizeof(GeometryInstance);

            // Reallocate if needed
            if (inst_buf.capacity < required_capacity) {
                if (inst_buf.buffer != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(eng.allocator, inst_buf.buffer, inst_buf.allocation);
                }

                const VkBufferCreateInfo buffer_ci{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .size = buffer_size,
                    .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE
                };

                constexpr VmaAllocationCreateInfo alloc_ci{
                    .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                    .usage = VMA_MEMORY_USAGE_AUTO
                };

                VmaAllocationInfo alloc_info{};
                vk_check(vmaCreateBuffer(eng.allocator, &buffer_ci, &alloc_ci,
                                        &inst_buf.buffer, &inst_buf.allocation, &alloc_info),
                        "Failed to create instance buffer");

                inst_buf.capacity = required_capacity;
            }

            // Update buffer data
            void* data = nullptr;
            vmaMapMemory(eng.allocator, inst_buf.allocation, &data);
            std::memcpy(data, batch.instances.data(), buffer_size);
            vmaUnmapMemory(eng.allocator, inst_buf.allocation);
        }
    }

    void GeometryPlugin::render_batch(VkCommandBuffer cmd, const GeometryBatch& batch,
                                      const InstanceBuffer& instance_buffer, const camera::Mat4& view_proj) {
        if (batch.instances.empty()) return;

        const auto mesh_it = geometry_meshes_.find(batch.type);
        if (mesh_it == geometry_meshes_.end()) return;

        const auto& mesh = mesh_it->second;

        // Choose pipeline based on geometry type and render mode
        VkPipeline pipeline = filled_pipeline_;
        if (batch.type == GeometryType::Line || batch.type == GeometryType::Ray || batch.type == GeometryType::Grid) {
            pipeline = line_pipeline_;
        } else if (batch.mode == RenderMode::Wireframe) {
            pipeline = wireframe_pipeline_;
        }

        // Bind pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        // Push constants (view-proj matrix)
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, view_proj.m.data());

        // Bind vertex buffers
        const VkBuffer vertex_buffers[] = {mesh.vertex_buffer, instance_buffer.buffer};
        const VkDeviceSize offsets[] = {0, 0};
        vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);

        // Bind index buffer and draw
        if (mesh.index_buffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(cmd, mesh.index_buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh.index_count, static_cast<uint32_t>(batch.instances.size()), 0, 0, 0);
        } else {
            vkCmdDraw(cmd, mesh.vertex_count, static_cast<uint32_t>(batch.instances.size()), 0, 0);
        }

        // If RenderMode::Both, render wireframe on top (only for non-line geometries)
        if (batch.mode == RenderMode::Both && batch.type != GeometryType::Line &&
            batch.type != GeometryType::Ray && batch.type != GeometryType::Grid) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe_pipeline_);
            vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, view_proj.m.data());
            vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);

            if (mesh.index_buffer != VK_NULL_HANDLE) {
                vkCmdBindIndexBuffer(cmd, mesh.index_buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, mesh.index_count, static_cast<uint32_t>(batch.instances.size()), 0, 0, 0);
            } else {
                vkCmdDraw(cmd, mesh.vertex_count, static_cast<uint32_t>(batch.instances.size()), 0, 0);
            }
        }

        // Optional face-normal visualization overlay
        if (show_face_normals_ &&
            batch.type != GeometryType::Line &&
            batch.type != GeometryType::Ray &&
            batch.type != GeometryType::Grid) {
            const auto n_it = normal_meshes_.find(batch.type);
            if (n_it != normal_meshes_.end()) {
                const auto& nmesh = n_it->second;
                if (nmesh.vertex_buffer != VK_NULL_HANDLE) {
                    const VkBuffer normal_vertex_buffers[] = {nmesh.vertex_buffer, instance_buffer.buffer};
                    const VkDeviceSize normal_offsets[] = {0, 0};
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline_);
                    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, view_proj.m.data());
                    vkCmdBindVertexBuffers(cmd, 0, 2, normal_vertex_buffers, normal_offsets);

                    if (nmesh.index_buffer != VK_NULL_HANDLE) {
                        vkCmdBindIndexBuffer(cmd, nmesh.index_buffer, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(cmd, nmesh.index_count, static_cast<uint32_t>(batch.instances.size()), 0, 0, 0);
                    } else {
                        vkCmdDraw(cmd, nmesh.vertex_count, static_cast<uint32_t>(batch.instances.size()), 0, 0);
                    }
                }
            }
        }
    }

    // ============================================================================
    // Geometry Mesh Generation
    // ============================================================================

    GeometryPlugin::GeometryMesh GeometryPlugin::create_sphere_mesh(const context::EngineContext& eng, const uint32_t segments,
                                                                    std::vector<float>* out_vertices,
                                                                    std::vector<uint32_t>* out_indices) {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;

        const auto rings = segments;
        const auto sectors = segments * 2;

        // Generate vertices
        for (uint32_t r = 0; r <= rings; ++r) {
            const auto phi = static_cast<float>(r) / static_cast<float>(rings) * 3.14159265359f;
            for (uint32_t s = 0; s <= sectors; ++s) {
                const auto theta = static_cast<float>(s) / static_cast<float>(sectors) * 2.0f * 3.14159265359f;

                const auto x = std::sin(phi) * std::cos(theta);
                const auto y = std::cos(phi);
                const auto z = std::sin(phi) * std::sin(theta);

                vertices.push_back(x); vertices.push_back(y); vertices.push_back(z); // position
                vertices.push_back(x); vertices.push_back(y); vertices.push_back(z); // normal
            }
        }

        // Generate indices (CCW from outside)
        for (uint32_t r = 0; r < rings; ++r) {
            for (uint32_t s = 0; s < sectors; ++s) {
                const auto current = r * (sectors + 1) + s;
                const auto next = current + sectors + 1;

                // Reversed winding
                indices.push_back(current);
                indices.push_back(current + 1);
                indices.push_back(next);

                indices.push_back(current + 1);
                indices.push_back(next + 1);
                indices.push_back(next);
            }
        }

        GeometryMesh mesh;
        mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
        mesh.index_count = static_cast<uint32_t>(indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        if (out_vertices) *out_vertices = vertices;
        if (out_indices) *out_indices = indices;

        create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_box_mesh(const context::EngineContext& eng,
                                                                 std::vector<float>* out_vertices,
                                                                 std::vector<uint32_t>* out_indices) {
        constexpr float vertices[] = {
            // positions          // normals
            // Front
            -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
            -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
            // Back
            -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
            -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
            // Top
            -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
            -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
            // Bottom
            -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
            -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
            // Right
             0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
            // Left
            -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        };

        constexpr uint32_t indices[] = {
            0, 1, 2, 2, 3, 0,       // front
            4, 5, 6, 6, 7, 4,       // back
            8, 9, 10, 10, 11, 8,    // top
            12, 13, 14, 14, 15, 12, // bottom
            16, 17, 18, 18, 19, 16, // right
            20, 21, 22, 22, 23, 20  // left
        };

        GeometryMesh mesh;
        mesh.vertex_count = 24;
        mesh.index_count = 36;
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        if (out_vertices) {
            out_vertices->assign(std::begin(vertices), std::end(vertices));
        }
        if (out_indices) {
            out_indices->assign(std::begin(indices), std::end(indices));
        }

        create_buffer_with_data(eng, vertices, sizeof(vertices),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices, sizeof(indices),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_cylinder_mesh(const context::EngineContext& eng, const uint32_t segments,
                                                                      std::vector<float>* out_vertices,
                                                                      std::vector<uint32_t>* out_indices) {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;

        // Generate side vertices (with radial normals)
        for (uint32_t i = 0; i <= segments; ++i) {
            const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
            const auto x = std::cos(angle);
            const auto z = std::sin(angle);

            // Bottom vertex
            vertices.push_back(x); vertices.push_back(-0.5f); vertices.push_back(z);
            vertices.push_back(x); vertices.push_back(0.0f); vertices.push_back(z);  // radial normal

            // Top vertex
            vertices.push_back(x); vertices.push_back(0.5f); vertices.push_back(z);
            vertices.push_back(x); vertices.push_back(0.0f); vertices.push_back(z);  // radial normal
        }

        const auto side_vertex_count = static_cast<uint32_t>(vertices.size() / 6);

        // Generate side indices (CCW from outside)
        for (uint32_t i = 0; i < segments; ++i) {
            const auto base = i * 2;
            // Reversed winding for correct CCW from outside
            indices.push_back(base);      // bottom current
            indices.push_back(base + 1);  // top current
            indices.push_back(base + 2);  // bottom next

            indices.push_back(base + 2);  // bottom next
            indices.push_back(base + 1);  // top current
            indices.push_back(base + 3);  // top next
        }

        // Add bottom cap center vertex
        const auto bottom_center_idx = side_vertex_count;
        vertices.push_back(0.0f); vertices.push_back(-0.5f); vertices.push_back(0.0f);
        vertices.push_back(0.0f); vertices.push_back(-1.0f); vertices.push_back(0.0f);  // down normal

        // Add bottom cap ring vertices
        for (uint32_t i = 0; i <= segments; ++i) {
            const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
            const auto x = std::cos(angle);
            const auto z = std::sin(angle);
            vertices.push_back(x); vertices.push_back(-0.5f); vertices.push_back(z);
            vertices.push_back(0.0f); vertices.push_back(-1.0f); vertices.push_back(0.0f);  // down normal
        }

        // Generate bottom cap indices (CCW from outside = CW when viewed from below)
        for (uint32_t i = 0; i < segments; ++i) {
            indices.push_back(bottom_center_idx);
            indices.push_back(bottom_center_idx + i + 1);
            indices.push_back(bottom_center_idx + i + 2);
        }

        // Add top cap center vertex
        const auto top_center_idx = static_cast<uint32_t>(vertices.size() / 6);
        vertices.push_back(0.0f); vertices.push_back(0.5f); vertices.push_back(0.0f);
        vertices.push_back(0.0f); vertices.push_back(1.0f); vertices.push_back(0.0f);  // up normal

        // Add top cap ring vertices
        for (uint32_t i = 0; i <= segments; ++i) {
            const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
            const auto x = std::cos(angle);
            const auto z = std::sin(angle);
            vertices.push_back(x); vertices.push_back(0.5f); vertices.push_back(z);
            vertices.push_back(0.0f); vertices.push_back(1.0f); vertices.push_back(0.0f);  // up normal
        }

        // Generate top cap indices (CCW from outside)
        for (uint32_t i = 0; i < segments; ++i) {
            indices.push_back(top_center_idx);
            indices.push_back(top_center_idx + i + 2);
            indices.push_back(top_center_idx + i + 1);
        }

        GeometryMesh mesh;
        mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
        mesh.index_count = static_cast<uint32_t>(indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        if (out_vertices) *out_vertices = vertices;
        if (out_indices) *out_indices = indices;

        create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_cone_mesh(const context::EngineContext& eng, const uint32_t segments,
                                                                  std::vector<float>* out_vertices,
                                                                  std::vector<uint32_t>* out_indices) {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;

        // Apex
        vertices.push_back(0.0f); vertices.push_back(0.5f); vertices.push_back(0.0f);
        vertices.push_back(0.0f); vertices.push_back(1.0f); vertices.push_back(0.0f);

        // Side vertices (base circle with radial normals)
        for (uint32_t i = 0; i <= segments; ++i) {
            const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
            const auto x = std::cos(angle);
            const auto z = std::sin(angle);

            // Calculate cone side normal (pointing outward and upward)
            const auto ny = 0.707f;  // 45 degree slope
            const auto nr = 0.707f;
            vertices.push_back(x); vertices.push_back(-0.5f); vertices.push_back(z);
            vertices.push_back(x * nr); vertices.push_back(ny); vertices.push_back(z * nr);
        }

        // Generate side indices (CCW from outside)
        for (uint32_t i = 0; i < segments; ++i) {
            indices.push_back(0);      // apex
            indices.push_back(i + 2);  // next base vertex
            indices.push_back(i + 1);  // current base vertex
        }

        // Add base cap center
        const auto base_center_idx = static_cast<uint32_t>(vertices.size() / 6);
        vertices.push_back(0.0f); vertices.push_back(-0.5f); vertices.push_back(0.0f);
        vertices.push_back(0.0f); vertices.push_back(-1.0f); vertices.push_back(0.0f);

        // Add base cap ring (with downward normals)
        for (uint32_t i = 0; i <= segments; ++i) {
            const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
            const auto x = std::cos(angle);
            const auto z = std::sin(angle);
            vertices.push_back(x); vertices.push_back(-0.5f); vertices.push_back(z);
            vertices.push_back(0.0f); vertices.push_back(-1.0f); vertices.push_back(0.0f);
        }

        // Generate base cap indices (CCW from outside)
        for (uint32_t i = 0; i < segments; ++i) {
            indices.push_back(base_center_idx);
            indices.push_back(base_center_idx + i + 1);
            indices.push_back(base_center_idx + i + 2);
        }

        GeometryMesh mesh;
        mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
        mesh.index_count = static_cast<uint32_t>(indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        if (out_vertices) *out_vertices = vertices;
        if (out_indices) *out_indices = indices;

        create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_torus_mesh(const context::EngineContext& eng,
                                                                   const uint32_t segments, const uint32_t tube_segments,
                                                                   std::vector<float>* out_vertices,
                                                                   std::vector<uint32_t>* out_indices) {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;

        constexpr auto major_radius = 0.4f;
        constexpr auto minor_radius = 0.15f;

        for (uint32_t i = 0; i <= segments; ++i) {
            const auto u = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
            for (uint32_t j = 0; j <= tube_segments; ++j) {
                const auto v = static_cast<float>(j) / static_cast<float>(tube_segments) * 2.0f * 3.14159265359f;

                const auto x = (major_radius + minor_radius * std::cos(v)) * std::cos(u);
                const auto y = minor_radius * std::sin(v);
                const auto z = (major_radius + minor_radius * std::cos(v)) * std::sin(u);

                const auto nx = std::cos(v) * std::cos(u);
                const auto ny = std::sin(v);
                const auto nz = std::cos(v) * std::sin(u);

                vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);
                vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
            }
        }

        for (uint32_t i = 0; i < segments; ++i) {
            for (uint32_t j = 0; j < tube_segments; ++j) {
                const auto a = i * (tube_segments + 1) + j;
                const auto b = a + tube_segments + 1;

                // Reversed winding
                indices.push_back(a);
                indices.push_back(a + 1);
                indices.push_back(b);

                indices.push_back(a + 1);
                indices.push_back(b + 1);
                indices.push_back(b);
            }
        }

        GeometryMesh mesh;
        mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
        mesh.index_count = static_cast<uint32_t>(indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        if (out_vertices) *out_vertices = vertices;
        if (out_indices) *out_indices = indices;

        create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_capsule_mesh(const context::EngineContext& eng, const uint32_t segments,
                                                                    std::vector<float>* out_vertices,
                                                                    std::vector<uint32_t>* out_indices) {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;

        constexpr auto height = 0.5f;  // Half-height of cylinder section
        constexpr auto radius = 0.25f;

        // Top hemisphere
        for (uint32_t r = 0; r <= segments / 2; ++r) {
            const auto phi = static_cast<float>(r) / static_cast<float>(segments / 2) * 3.14159265359f * 0.5f;
            for (uint32_t s = 0; s <= segments; ++s) {
                const auto theta = static_cast<float>(s) / static_cast<float>(segments) * 2.0f * 3.14159265359f;

                const auto x = radius * std::cos(phi) * std::cos(theta);
                const auto y = height + radius * std::sin(phi);
                const auto z = radius * std::cos(phi) * std::sin(theta);

                const auto nx = std::cos(phi) * std::cos(theta);
                const auto ny = std::sin(phi);
                const auto nz = std::cos(phi) * std::sin(theta);

                vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);
                vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
            }
        }

        const auto top_hemisphere_count = static_cast<uint32_t>(vertices.size() / 6);

        // Generate top hemisphere indices
        for (uint32_t r = 0; r < segments / 2; ++r) {
            for (uint32_t s = 0; s < segments; ++s) {
                const auto current = r * (segments + 1) + s;
                const auto next = current + segments + 1;

                // Reversed winding
                indices.push_back(current);
                indices.push_back(next);
                indices.push_back(current + 1);

                indices.push_back(current + 1);
                indices.push_back(next);
                indices.push_back(next + 1);
            }
        }

        // Cylinder middle section
        const auto cylinder_start_idx = static_cast<uint32_t>(vertices.size() / 6);
        for (uint32_t i = 0; i <= segments; ++i) {
            const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
            const auto x = radius * std::cos(angle);
            const auto z = radius * std::sin(angle);
            const auto nx = std::cos(angle);
            const auto nz = std::sin(angle);

            // Bottom of cylinder (at +height)
            vertices.push_back(x); vertices.push_back(height); vertices.push_back(z);
            vertices.push_back(nx); vertices.push_back(0.0f); vertices.push_back(nz);

            // Top of cylinder (at -height)
            vertices.push_back(x); vertices.push_back(-height); vertices.push_back(z);
            vertices.push_back(nx); vertices.push_back(0.0f); vertices.push_back(nz);
        }

        // Generate cylinder indices
        for (uint32_t i = 0; i < segments; ++i) {
            const auto base = cylinder_start_idx + i * 2;
            // Reversed winding
            indices.push_back(base);
            indices.push_back(base + 2);
            indices.push_back(base + 1);

            indices.push_back(base + 2);
            indices.push_back(base + 3);
            indices.push_back(base + 1);
        }

        // Bottom hemisphere
        const auto bottom_hemisphere_start = static_cast<uint32_t>(vertices.size() / 6);
        for (uint32_t r = 0; r <= segments / 2; ++r) {
            const auto phi = static_cast<float>(r) / static_cast<float>(segments / 2) * 3.14159265359f * 0.5f;
            for (uint32_t s = 0; s <= segments; ++s) {
                const auto theta = static_cast<float>(s) / static_cast<float>(segments) * 2.0f * 3.14159265359f;

                const auto x = radius * std::cos(phi) * std::cos(theta);
                const auto y = -height - radius * std::sin(phi);
                const auto z = radius * std::cos(phi) * std::sin(theta);

                const auto nx = std::cos(phi) * std::cos(theta);
                const auto ny = -std::sin(phi);
                const auto nz = std::cos(phi) * std::sin(theta);

                vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);
                vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
            }
        }

        // Generate bottom hemisphere indices
        for (uint32_t r = 0; r < segments / 2; ++r) {
            for (uint32_t s = 0; s < segments; ++s) {
                const auto current = bottom_hemisphere_start + r * (segments + 1) + s;
                const auto next = current + segments + 1;

                // Reversed winding
                indices.push_back(current);
                indices.push_back(current + 1);
                indices.push_back(next);

                indices.push_back(current + 1);
                indices.push_back(next + 1);
                indices.push_back(next);
            }
        }

        GeometryMesh mesh;
        mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
        mesh.index_count = static_cast<uint32_t>(indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        if (out_vertices) *out_vertices = vertices;
        if (out_indices) *out_indices = indices;

        create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_plane_mesh(const context::EngineContext& eng,
                                                                   std::vector<float>* out_vertices,
                                                                   std::vector<uint32_t>* out_indices) {
        constexpr float vertices[] = {
            // positions          // normals
            -0.5f, 0.0f, -0.5f,   0.0f, 1.0f, 0.0f,
             0.5f, 0.0f, -0.5f,   0.0f, 1.0f, 0.0f,
             0.5f, 0.0f,  0.5f,   0.0f, 1.0f, 0.0f,
            -0.5f, 0.0f,  0.5f,   0.0f, 1.0f, 0.0f,
        };

        constexpr uint32_t indices[] = {
            0, 2, 1, 2, 0, 3
        };

        GeometryMesh mesh;
        mesh.vertex_count = 4;
        mesh.index_count = 6;
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        if (out_vertices) {
            out_vertices->assign(std::begin(vertices), std::end(vertices));
        }
        if (out_indices) {
            out_indices->assign(std::begin(indices), std::end(indices));
        }

        create_buffer_with_data(eng, vertices, sizeof(vertices),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices, sizeof(indices),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_circle_mesh(const context::EngineContext& eng, const uint32_t segments,
                                                                    std::vector<float>* out_vertices,
                                                                    std::vector<uint32_t>* out_indices) {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;

        // Center
        vertices.push_back(0.0f); vertices.push_back(0.0f); vertices.push_back(0.0f);
        vertices.push_back(0.0f); vertices.push_back(1.0f); vertices.push_back(0.0f);

        // Circle points
        for (uint32_t i = 0; i <= segments; ++i) {
            const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
            const auto x = std::cos(angle) * 0.5f;
            const auto z = std::sin(angle) * 0.5f;

            vertices.push_back(x); vertices.push_back(0.0f); vertices.push_back(z);
            vertices.push_back(0.0f); vertices.push_back(1.0f); vertices.push_back(0.0f);
        }

        // Generate indices
        for (uint32_t i = 1; i <= segments; ++i) {
            indices.push_back(0);
            indices.push_back(i + 1);
            indices.push_back(i);
        }

        GeometryMesh mesh;
        mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
        mesh.index_count = static_cast<uint32_t>(indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        if (out_vertices) *out_vertices = vertices;
        if (out_indices) *out_indices = indices;

        create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_line_mesh(const context::EngineContext& eng) {
        constexpr float vertices[] = {
            // positions          // normals (unused for lines)
            -0.5f, 0.0f, 0.0f,    0.0f, 1.0f, 0.0f,
             0.5f, 0.0f, 0.0f,    0.0f, 1.0f, 0.0f,
        };

        constexpr uint32_t indices[] = {
            0, 1
        };

        GeometryMesh mesh;
        mesh.vertex_count = 2;
        mesh.index_count = 2;
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

        create_buffer_with_data(eng, vertices, sizeof(vertices),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices, sizeof(indices),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }
}
