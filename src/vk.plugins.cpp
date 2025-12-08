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

    engine::PluginPhase Viewport3DPlugin::phases() const noexcept {
        return engine::PluginPhase::Setup |
               engine::PluginPhase::Initialize |
               engine::PluginPhase::PreRender |
               engine::PluginPhase::Render |
               engine::PluginPhase::PostRender |
               engine::PluginPhase::Cleanup;
    }

    void Viewport3DPlugin::on_setup(engine::PluginContext& ctx) {
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

        log_success(name(), "Setup complete → renderer configured");
    }

    void Viewport3DPlugin::on_initialize(engine::PluginContext& ctx) {
        create_imgui(*ctx.engine, *ctx.frame);
        log_success(name(), "Initialized → UI ready");
    }

    void Viewport3DPlugin::on_pre_render(engine::PluginContext& ctx) {
        const auto current_time = SDL_GetTicks();
        const auto dt = static_cast<float>(current_time - last_time_ms_) / 1000.0f;
        last_time_ms_ = current_time;
        camera_.update(dt, viewport_width_, viewport_height_);
    }

    void Viewport3DPlugin::on_render(engine::PluginContext& ctx) {
        const auto& target = ctx.frame->color_attachments.front();
        transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        begin_rendering(*ctx.cmd, target, ctx.frame->extent);
        end_rendering(*ctx.cmd);
        transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    void Viewport3DPlugin::on_post_render(engine::PluginContext& ctx) {
        render_imgui(*ctx.cmd, *ctx.frame);
    }

    void Viewport3DPlugin::on_cleanup(engine::PluginContext& ctx) {
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
        log_info(name(), std::format("Viewport resized → {}x{}", width, height));
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

        log_success(name(), "ImGui initialized → docking enabled");
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

    engine::PluginPhase ScreenshotPlugin::phases() const noexcept {
        return engine::PluginPhase::Initialize |
               engine::PluginPhase::PreRender |
               engine::PluginPhase::Present |
               engine::PluginPhase::Cleanup;
    }

    void ScreenshotPlugin::on_initialize(engine::PluginContext& /*ctx*/) {
        log_success(name(), "Initialized → Press F1 to capture");
    }

    void ScreenshotPlugin::on_pre_render(engine::PluginContext& ctx) {
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

    void ScreenshotPlugin::on_present(engine::PluginContext& ctx) {
        if (!screenshot_requested_) return;

        const auto image_index = ctx.frame->image_index;
        capture_swapchain(ctx, image_index);
        screenshot_requested_ = false;
    }

    void ScreenshotPlugin::on_cleanup(engine::PluginContext& ctx) {
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

    void ScreenshotPlugin::capture_swapchain(engine::PluginContext& ctx, const uint32_t /*image_index*/) {
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

        log_info(name(), std::format("Capture queued → {}x{}", width, height));
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

        log_success(name(), std::format("Saved → {}", path));
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

    engine::PluginPhase GeometryPlugin::phases() const noexcept {
        return engine::PluginPhase::Initialize |
               engine::PluginPhase::PreRender |
               engine::PluginPhase::Render |
               engine::PluginPhase::Cleanup;
    }

    void GeometryPlugin::on_initialize(engine::PluginContext& ctx) {
        create_pipelines(*ctx.engine);
        create_geometry_meshes(*ctx.engine);
        log_success(name(), "Initialized → geometry meshes and pipelines ready");
    }

    void GeometryPlugin::on_pre_render(engine::PluginContext& ctx) {
        if (!enabled_ || batches_.empty()) return;
        update_instance_buffers(*ctx.engine);
    }

    void GeometryPlugin::on_render(engine::PluginContext& ctx) {
        if (!enabled_ || batches_.empty() || !viewport_plugin_) return;

        const auto& camera = viewport_plugin_->get_camera();
        const auto view_proj = camera.proj_matrix() * camera.view_matrix();

        // Render all batches with instancing
        for (size_t i = 0; i < batches_.size(); ++i) {
            if (!batches_[i].instances.empty() && i < instance_buffers_.size()) {
                render_batch(*ctx.cmd, batches_[i], instance_buffers_[i], view_proj);
            }
        }
    }

    void GeometryPlugin::on_cleanup(engine::PluginContext& ctx) {
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
        GeometryBatch batch{GeometryType::Line, RenderMode::Wireframe};
        batch.instances.push_back({mid, {0, 0, 0}, {length, 1, 1}, color, 1.0f});
        add_batch(batch);
    }

    void GeometryPlugin::add_ray(const camera::Vec3& origin, const camera::Vec3& direction,
                                 const float length, const camera::Vec3& color) {
        add_line(origin, origin + direction.normalized() * length, color);
    }

    void GeometryPlugin::add_grid(const camera::Vec3& position, const float size,
                                  const int divisions, const camera::Vec3& color) {
        GeometryBatch batch{GeometryType::Grid, RenderMode::Wireframe};
        batch.instances.push_back({position, {0, 0, 0}, {size, 1, static_cast<float>(divisions)}, color, 1.0f});
        add_batch(batch);
    }

    void GeometryPlugin::create_pipelines(const context::EngineContext& eng) {
        // Push constant range for view-projection matrix
        const VkPushConstantRange push_constant{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(float) * 16  // mat4
        };

        const VkPipelineLayoutCreateInfo layout_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant
        };

        vk_check(vkCreatePipelineLayout(eng.device, &layout_info, nullptr, &pipeline_layout_),
                 "Failed to create geometry pipeline layout");

        // Load shaders
        auto load_shader = [&eng](const char* path) -> VkShaderModule {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                throw std::runtime_error(std::format("Failed to open shader: {}", path));
            }

            const auto size = static_cast<size_t>(file.tellg());
            file.seekg(0);
            std::vector<char> code(size);
            file.read(code.data(), static_cast<std::streamsize>(size));

            const VkShaderModuleCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = code.size(),
                .pCode = reinterpret_cast<const uint32_t*>(code.data())
            };

            VkShaderModule module;
            vk_check(vkCreateShaderModule(eng.device, &create_info, nullptr, &module),
                     "Failed to create shader module");
            return module;
        };

        VkShaderModule vs = load_shader("shader/geometry.vert.spv");
        VkShaderModule fs = load_shader("shader/geometry.frag.spv");

        const VkPipelineShaderStageCreateInfo stages[2] = {
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

        // Vertex input: mesh attributes + instance attributes
        const VkVertexInputBindingDescription bindings[] = {
            {
                .binding = 0,
                .stride = sizeof(float) * 6,  // position(3) + normal(3)
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            },
            {
                .binding = 1,
                .stride = sizeof(GeometryInstance),
                .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
            }
        };

        const VkVertexInputAttributeDescription attributes[] = {
            // Mesh vertex attributes
            {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0},                    // position
            {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = sizeof(float) * 3},    // normal
            // Instance attributes
            {.location = 2, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(GeometryInstance, position)},
            {.location = 3, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(GeometryInstance, rotation)},
            {.location = 4, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(GeometryInstance, scale)},
            {.location = 5, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(GeometryInstance, color)},
            {.location = 6, .binding = 1, .format = VK_FORMAT_R32_SFLOAT, .offset = offsetof(GeometryInstance, alpha)},
        };

        const VkPipelineVertexInputStateCreateInfo vertex_input{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 2,
            .pVertexBindingDescriptions = bindings,
            .vertexAttributeDescriptionCount = 7,
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

        const VkPipelineRasterizationStateCreateInfo rasterization_filled{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f
        };

        const VkPipelineRasterizationStateCreateInfo rasterization_wireframe{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_LINE,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f
        };

        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
        };

        const VkPipelineColorBlendAttachmentState color_blend_attachment{
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        };

        const VkPipelineColorBlendStateCreateInfo color_blend{
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

        const VkFormat color_format = VK_FORMAT_B8G8R8A8_UNORM;
        const VkPipelineRenderingCreateInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &color_format
        };

        // Create filled pipeline
        VkGraphicsPipelineCreateInfo pipeline_info{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rendering_info,
            .stageCount = 2,
            .pStages = stages,
            .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterization_filled,
            .pMultisampleState = &multisample,
            .pColorBlendState = &color_blend,
            .pDynamicState = &dynamic_state,
            .layout = pipeline_layout_
        };

        vk_check(vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &filled_pipeline_),
                 "Failed to create filled geometry pipeline");

        // Create wireframe pipeline
        pipeline_info.pRasterizationState = &rasterization_wireframe;
        vk_check(vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &wireframe_pipeline_),
                 "Failed to create wireframe geometry pipeline");

        vkDestroyShaderModule(eng.device, vs, nullptr);
        vkDestroyShaderModule(eng.device, fs, nullptr);

        log_success(name(), "Pipelines created → filled and wireframe modes ready");
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
        if (pipeline_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(eng.device, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
        }
    }

    void GeometryPlugin::create_geometry_meshes(const context::EngineContext& eng) {
        geometry_meshes_[GeometryType::Sphere] = create_sphere_mesh(eng);
        geometry_meshes_[GeometryType::Box] = create_box_mesh(eng);
        geometry_meshes_[GeometryType::Cylinder] = create_cylinder_mesh(eng);
        geometry_meshes_[GeometryType::Cone] = create_cone_mesh(eng);
        geometry_meshes_[GeometryType::Torus] = create_torus_mesh(eng);
        geometry_meshes_[GeometryType::Capsule] = create_capsule_mesh(eng);
        geometry_meshes_[GeometryType::Plane] = create_plane_mesh(eng);
        geometry_meshes_[GeometryType::Circle] = create_circle_mesh(eng);
        geometry_meshes_[GeometryType::Line] = create_line_mesh(eng);
        // Grid and Ray use the line mesh with modifications
        geometry_meshes_[GeometryType::Grid] = create_line_mesh(eng);
        geometry_meshes_[GeometryType::Ray] = create_line_mesh(eng);
    }

    void GeometryPlugin::destroy_geometry_meshes(const context::EngineContext& eng) {
        for (auto& [type, mesh] : geometry_meshes_) {
            if (mesh.vertex_buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(eng.allocator, mesh.vertex_buffer, mesh.vertex_allocation);
            }
            if (mesh.index_buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(eng.allocator, mesh.index_buffer, mesh.index_allocation);
            }
        }
        geometry_meshes_.clear();

        for (size_t i = 0; i < instance_buffers_.size(); ++i) {
            auto& inst_buf = instance_buffers_[i];
            if (inst_buf.buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(eng.allocator, inst_buf.buffer, inst_buf.allocation);
            }
        }
        instance_buffers_.clear();
    }

    void GeometryPlugin::update_instance_buffers(const context::EngineContext& eng) {
        // Resize instance buffers vector if needed
        if (instance_buffers_.size() < batches_.size()) {
            instance_buffers_.resize(batches_.size());
        }

        // Update each instance buffer
        for (size_t i = 0; i < batches_.size(); ++i) {
            const auto& batch = batches_[i];
            auto& inst_buf = instance_buffers_[i];

            if (batch.instances.empty()) continue;

            const auto required_size = batch.instances.size() * sizeof(GeometryInstance);

            // Recreate buffer if too small
            if (inst_buf.capacity < required_size) {
                if (inst_buf.buffer != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(eng.allocator, inst_buf.buffer, inst_buf.allocation);
                }

                const VkBufferCreateInfo buffer_ci{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .size = required_size,
                    .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE
                };

                constexpr VmaAllocationCreateInfo alloc_ci{
                    .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                    .usage = VMA_MEMORY_USAGE_AUTO,
                    .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                };

                VmaAllocationInfo alloc_info{};
                vk_check(vmaCreateBuffer(eng.allocator, &buffer_ci, &alloc_ci,
                    &inst_buf.buffer, &inst_buf.allocation, &alloc_info),
                    "Failed to create instance buffer");

                inst_buf.capacity = static_cast<uint32_t>(required_size);

                // Copy data
                void* data = nullptr;
                vmaMapMemory(eng.allocator, inst_buf.allocation, &data);
                std::memcpy(data, batch.instances.data(), required_size);
                vmaUnmapMemory(eng.allocator, inst_buf.allocation);
            }
        }
    }

    void GeometryPlugin::render_batch(VkCommandBuffer cmd, const GeometryBatch& batch,
                                      const InstanceBuffer& instance_buffer, const camera::Mat4& view_proj) {
        if (batch.instances.empty() || instance_buffer.buffer == VK_NULL_HANDLE) return;

        // Get geometry mesh
        const auto it = geometry_meshes_.find(batch.type);
        if (it == geometry_meshes_.end()) return;
        const auto& mesh = it->second;

        // Select pipeline based on render mode
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (batch.mode == RenderMode::Filled) {
            pipeline = filled_pipeline_;
        } else if (batch.mode == RenderMode::Wireframe) {
            pipeline = wireframe_pipeline_;
        } else if (batch.mode == RenderMode::Both) {
            // Render twice: first filled, then wireframe
            render_batch(cmd, {batch.type, RenderMode::Filled, batch.instances}, instance_buffer, view_proj);
            render_batch(cmd, {batch.type, RenderMode::Wireframe, batch.instances}, instance_buffer, view_proj);
            return;
        }

        if (pipeline == VK_NULL_HANDLE) return;

        // Bind pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        // Push view-projection matrix
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, view_proj.m.data());

        // Bind vertex buffers (mesh + instances)
        const VkBuffer vertex_buffers[] = {mesh.vertex_buffer, instance_buffer.buffer};
        constexpr VkDeviceSize offsets[] = {0, 0};
        vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);

        // Bind index buffer
        if (mesh.index_buffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(cmd, mesh.index_buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh.index_count, static_cast<uint32_t>(batch.instances.size()), 0, 0, 0);
        } else {
            vkCmdDraw(cmd, mesh.vertex_count, static_cast<uint32_t>(batch.instances.size()), 0, 0);
        }
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

    GeometryPlugin::GeometryMesh GeometryPlugin::create_sphere_mesh(const context::EngineContext& eng, const uint32_t segments) {
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

        // Generate indices
        for (uint32_t r = 0; r < rings; ++r) {
            for (uint32_t s = 0; s < sectors; ++s) {
                const auto current = r * (sectors + 1) + s;
                const auto next = current + sectors + 1;

                indices.push_back(current);
                indices.push_back(next);
                indices.push_back(current + 1);

                indices.push_back(current + 1);
                indices.push_back(next);
                indices.push_back(next + 1);
            }
        }

        GeometryMesh mesh;
        mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
        mesh.index_count = static_cast<uint32_t>(indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_box_mesh(const context::EngineContext& eng) {
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

        create_buffer_with_data(eng, vertices, sizeof(vertices),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices, sizeof(indices),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_cylinder_mesh(const context::EngineContext& eng, const uint32_t segments) {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;

        // Top and bottom caps + sides
        for (uint32_t i = 0; i <= segments; ++i) {
            const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
            const auto x = std::cos(angle);
            const auto z = std::sin(angle);

            // Bottom vertex
            vertices.push_back(x); vertices.push_back(-0.5f); vertices.push_back(z);
            vertices.push_back(x); vertices.push_back(0.0f); vertices.push_back(z);

            // Top vertex
            vertices.push_back(x); vertices.push_back(0.5f); vertices.push_back(z);
            vertices.push_back(x); vertices.push_back(0.0f); vertices.push_back(z);
        }

        // Generate side indices
        for (uint32_t i = 0; i < segments; ++i) {
            const auto base = i * 2;
            indices.push_back(base);
            indices.push_back(base + 2);
            indices.push_back(base + 1);

            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        }

        GeometryMesh mesh;
        mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
        mesh.index_count = static_cast<uint32_t>(indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_cone_mesh(const context::EngineContext& eng, const uint32_t segments) {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;

        // Apex
        vertices.push_back(0.0f); vertices.push_back(0.5f); vertices.push_back(0.0f);
        vertices.push_back(0.0f); vertices.push_back(1.0f); vertices.push_back(0.0f);

        // Base circle
        for (uint32_t i = 0; i <= segments; ++i) {
            const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
            const auto x = std::cos(angle);
            const auto z = std::sin(angle);

            vertices.push_back(x); vertices.push_back(-0.5f); vertices.push_back(z);
            vertices.push_back(x); vertices.push_back(0.0f); vertices.push_back(z);
        }

        // Generate side indices
        for (uint32_t i = 0; i < segments; ++i) {
            indices.push_back(0);
            indices.push_back(i + 1);
            indices.push_back(i + 2);
        }

        GeometryMesh mesh;
        mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
        mesh.index_count = static_cast<uint32_t>(indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_torus_mesh(const context::EngineContext& eng,
                                                                   const uint32_t segments, const uint32_t tube_segments) {
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

                indices.push_back(a);
                indices.push_back(b);
                indices.push_back(a + 1);

                indices.push_back(a + 1);
                indices.push_back(b);
                indices.push_back(b + 1);
            }
        }

        GeometryMesh mesh;
        mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
        mesh.index_count = static_cast<uint32_t>(indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_capsule_mesh(const context::EngineContext& eng, const uint32_t segments) {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;

        constexpr auto height = 0.5f;
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

        // Bottom hemisphere
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

        // Generate indices (simple approach)
        const auto vert_count = static_cast<uint32_t>(vertices.size() / 6);
        for (uint32_t i = 0; i < vert_count - segments - 2; ++i) {
            if ((i + 1) % (segments + 1) != 0) {
                indices.push_back(i);
                indices.push_back(i + segments + 1);
                indices.push_back(i + 1);

                indices.push_back(i + 1);
                indices.push_back(i + segments + 1);
                indices.push_back(i + segments + 2);
            }
        }

        GeometryMesh mesh;
        mesh.vertex_count = vert_count;
        mesh.index_count = static_cast<uint32_t>(indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_plane_mesh(const context::EngineContext& eng) {
        constexpr float vertices[] = {
            // positions          // normals
            -0.5f, 0.0f, -0.5f,   0.0f, 1.0f, 0.0f,
             0.5f, 0.0f, -0.5f,   0.0f, 1.0f, 0.0f,
             0.5f, 0.0f,  0.5f,   0.0f, 1.0f, 0.0f,
            -0.5f, 0.0f,  0.5f,   0.0f, 1.0f, 0.0f,
        };

        constexpr uint32_t indices[] = {
            0, 1, 2, 2, 3, 0
        };

        GeometryMesh mesh;
        mesh.vertex_count = 4;
        mesh.index_count = 6;
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        create_buffer_with_data(eng, vertices, sizeof(vertices),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
        create_buffer_with_data(eng, indices, sizeof(indices),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

        return mesh;
    }

    GeometryPlugin::GeometryMesh GeometryPlugin::create_circle_mesh(const context::EngineContext& eng, const uint32_t segments) {
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
            indices.push_back(i);
            indices.push_back(i + 1);
        }

        GeometryMesh mesh;
        mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
        mesh.index_count = static_cast<uint32_t>(indices.size());
        mesh.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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
