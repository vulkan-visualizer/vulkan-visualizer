module;
#include <SDL3/SDL.h>
#include <array>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <imgui.h>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "vk_mem_alloc.h"
#include <stb_image_write.h>

module vk.plugins;
import vk.camera;

// clang-format off
#ifndef VK_CHECK
#define VK_CHECK(x) do { VkResult _vk_check_res = (x); if (_vk_check_res != VK_SUCCESS) { throw std::runtime_error(std::string("Vulkan error ") + std::to_string(_vk_check_res) + " at " #x); } } while (false)
#endif
// clang-format on

namespace vk::plugins {
    // ============================================================================
    // Helper Functions
    // ============================================================================

    static void transition_image_layout(VkCommandBuffer& cmd, const context::AttachmentView& target, VkImageLayout old_layout, VkImageLayout new_layout) {
        auto [src_stage, dst_stage, src_access, dst_access] = [&]() -> std::array<std::uint64_t, 4> {
            if (old_layout == VK_IMAGE_LAYOUT_GENERAL && new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                return {
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                };
            return {
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            };
        }();

        VkImageMemoryBarrier2 barrier{
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask     = src_stage,
            .srcAccessMask    = src_access,
            .dstStageMask     = dst_stage,
            .dstAccessMask    = dst_access,
            .oldLayout        = old_layout,
            .newLayout        = new_layout,
            .image            = target.image,
            .subresourceRange = {target.aspect, 0, 1, 0, 1},
        };
        const VkDependencyInfo depInfo{
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers    = &barrier,
        };
        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    static void transition_to_color_attachment(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout) {
        VkImageMemoryBarrier2 barrier{
            .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext            = nullptr,
            .srcStageMask     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask    = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask    = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
            .oldLayout        = old_layout,
            .newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image            = image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u},
        };
        VkDependencyInfo dep{
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1u,
            .pImageMemoryBarriers    = &barrier,
        };
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    // ============================================================================
    // Viewport3DPlugin Implementation
    // ============================================================================

    Viewport3DPlugin::Viewport3DPlugin() {
        camera_.home_view();
        last_time_ms_ = SDL_GetTicks();
    }

    engine::PluginPhase Viewport3DPlugin::phases() const {
        return engine::PluginPhase::Setup |
               engine::PluginPhase::Initialize |
               engine::PluginPhase::PreRender |
               engine::PluginPhase::Render |
               engine::PluginPhase::PostRender |
               engine::PluginPhase::Cleanup;
    }

    void Viewport3DPlugin::on_setup(engine::PluginContext& ctx) {
        // Configure renderer capabilities
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

        std::println("[{}] Setup complete - configured renderer capabilities", name());
    }

    void Viewport3DPlugin::on_initialize(engine::PluginContext& ctx) {
        create_imgui(*ctx.engine, *ctx.frame);
        std::println("[{}] Initialized - UI created", name());
    }

    void Viewport3DPlugin::on_pre_render(engine::PluginContext& ctx) {
        // Update camera
        const uint64_t current_time = SDL_GetTicks();
        const float dt = (current_time - last_time_ms_) / 1000.0f;
        last_time_ms_ = current_time;

        camera_.update(dt, viewport_width_, viewport_height_);
    }

    void Viewport3DPlugin::on_render(engine::PluginContext& ctx) {
        const auto& target = ctx.frame->color_attachments.front();

        transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        begin_rendering(*ctx.cmd, target, ctx.frame->extent);
        // Clear only - no built-in objects
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
        std::println("[{}] Cleanup complete", name());
    }

    void Viewport3DPlugin::on_event(const SDL_Event& event) {
        ImGuiIO& io = ImGui::GetIO();
        const bool imgui_wants_input = io.WantCaptureMouse || io.WantCaptureKeyboard;

        if (!imgui_wants_input) {
            camera_.handle_event(event);
        }
    }

    void Viewport3DPlugin::on_resize(uint32_t width, uint32_t height) {
        viewport_width_ = static_cast<int>(width);
        viewport_height_ = static_cast<int>(height);
    }

    void Viewport3DPlugin::begin_rendering(VkCommandBuffer& cmd, const context::AttachmentView& target, VkExtent2D extent) {
        constexpr VkClearValue clear_value{.color = {{0.1f, 0.1f, 0.12f, 1.0f}}};
        VkRenderingAttachmentInfo color_attachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = target.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clear_value,
        };
        VkRenderingInfo render_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {{0, 0}, extent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment,
        };
        vkCmdBeginRendering(cmd, &render_info);
    }

    void Viewport3DPlugin::end_rendering(VkCommandBuffer& cmd) {
        vkCmdEndRendering(cmd);
    }

    // ============================================================================
    // UI Implementation
    // ============================================================================

    void Viewport3DPlugin::create_imgui(context::EngineContext& eng, const context::FrameContext& frm) {
        std::array<VkDescriptorPoolSize, 11> pool_sizes{{
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
        }};
        VkDescriptorPoolCreateInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 1000u * static_cast<uint32_t>(pool_sizes.size()),
            .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
            .pPoolSizes = pool_sizes.data(),
        };
        VK_CHECK(vkCreateDescriptorPool(eng.device, &pool_info, nullptr, &eng.descriptor_allocator.pool));

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        ImGui::StyleColorsDark();

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        if (!ImGui_ImplSDL3_InitForVulkan(eng.window)) {
            throw std::runtime_error("Failed to initialize ImGui SDL3 backend.");
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
        init_info.CheckVkResultFn = [](VkResult res) { VK_CHECK(res); };
        init_info.UseDynamicRendering = VK_TRUE;

        VkPipelineRenderingCreateInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &frm.swapchain_format,
        };
        init_info.PipelineRenderingCreateInfo = rendering_info;

        if (!ImGui_ImplVulkan_Init(&init_info)) {
            throw std::runtime_error("Failed to initialize ImGui Vulkan backend.");
        }
    }

    void Viewport3DPlugin::destroy_imgui(const context::EngineContext& eng) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
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

        // Render to target
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
            VkRenderingAttachmentInfo color_attachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = target_view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            };
            VkRenderingInfo rendering_info{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {{0, 0}, frm.extent},
                .layerCount = 1u,
                .colorAttachmentCount = 1u,
                .pColorAttachments = &color_attachment,
            };
            vkCmdBeginRendering(cmd, &rendering_info);
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
            vkCmdEndRendering(cmd);

            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }

            if (frm.presentation_mode != context::PresentationMode::DirectToSwapchain) {
                VkImageMemoryBarrier2 barrier{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .image = target_image,
                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u},
                };
                VkDependencyInfo dep{
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .imageMemoryBarrierCount = 1u,
                    .pImageMemoryBarriers = &barrier,
                };
                vkCmdPipelineBarrier2(cmd, &dep);
            }
        }
    }

    void Viewport3DPlugin::draw_camera_panel() {
        ImGui::Begin("Camera Controls", &show_camera_panel_);

        auto state = camera_.state();
        bool changed = false;

        // Mode selection
        int mode = static_cast<int>(state.mode);
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
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport) return;

        ImDrawList* draw_list = ImGui::GetForegroundDrawList(viewport);
        if (!draw_list) return;

        constexpr float size = 80.0f;
        constexpr float margin = 16.0f;
        const ImVec2 center(
            viewport->Pos.x + viewport->Size.x - margin - size * 0.5f,
            viewport->Pos.y + margin + size * 0.5f
        );
        const float radius = size * 0.42f;

        draw_list->AddCircleFilled(center, size * 0.5f, IM_COL32(30, 32, 36, 180), 48);
        draw_list->AddCircle(center, size * 0.5f, IM_COL32(255, 255, 255, 60), 48, 1.5f);

        const auto& view = camera_.view_matrix();

        struct AxisInfo {
            camera::Vec3 direction;
            ImU32 color;
            const char* label;
        };

        const AxisInfo axes[3] = {
            {{1, 0, 0}, IM_COL32(255, 80, 80, 255), "X"},
            {{0, 1, 0}, IM_COL32(80, 255, 80, 255), "Y"},
            {{0, 0, 1}, IM_COL32(100, 140, 255, 255), "Z"}
        };

        struct TransformedAxis {
            camera::Vec3 view_dir;
            AxisInfo info;
        };

        TransformedAxis transformed[3];
        for (int i = 0; i < 3; ++i) {
            const auto& dir = axes[i].direction;
            camera::Vec3 view_dir{
                view.m[0] * dir.x + view.m[4] * dir.y + view.m[8] * dir.z,
                view.m[1] * dir.x + view.m[5] * dir.y + view.m[9] * dir.z,
                view.m[2] * dir.x + view.m[6] * dir.y + view.m[10] * dir.z
            };
            transformed[i] = {view_dir, axes[i]};
        }

        auto draw_axis = [&](const TransformedAxis& axis, bool is_back) {
            const float thickness = is_back ? 2.0f : 3.0f;
            const ImU32 base_color = axis.info.color;
            const ImU32 color = is_back
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
            const float circle_radius = is_back ? 3.0f : 4.5f;
            draw_list->AddCircleFilled(end_point, circle_radius, color, 12);

            if (!is_back) {
                const float label_offset_x = axis.view_dir.x >= 0 ? 8.0f : -20.0f;
                const float label_offset_y = axis.view_dir.y >= 0 ? -18.0f : 4.0f;
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

    // ============================================================================
    // Screenshot Plugin Implementation
    // ============================================================================

    engine::PluginPhase ScreenshotPlugin::phases() const {
        return engine::PluginPhase::Initialize |
               engine::PluginPhase::PreRender |
               engine::PluginPhase::Present |
               engine::PluginPhase::Cleanup;
    }

    void ScreenshotPlugin::on_initialize(engine::PluginContext& ctx) {
        std::println("[{}] Initialized - Ready to capture screenshots (F1 to capture)", name());
    }

    void ScreenshotPlugin::on_pre_render(engine::PluginContext& ctx) {
        // Process pending screenshot from previous frame
        if (pending_capture_.buffer != VK_NULL_HANDLE && ctx.engine) {
            // Wait for GPU to complete
            vkQueueWaitIdle(ctx.engine->graphics_queue);

            // Map buffer and save screenshot
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

            // Cleanup buffer
            vmaDestroyBuffer(ctx.engine->allocator, pending_capture_.buffer, pending_capture_.allocation);
            pending_capture_ = {};
        }
    }

    void ScreenshotPlugin::on_present(engine::PluginContext& ctx) {
        if (!screenshot_requested_) return;

        // Capture the current frame
        const uint32_t image_index = ctx.frame->image_index;
        capture_swapchain(ctx, image_index);
        screenshot_requested_ = false;
    }

    void ScreenshotPlugin::on_cleanup(engine::PluginContext& ctx) {
        // Clean up any pending capture resources
        if (pending_capture_.buffer != VK_NULL_HANDLE && ctx.engine) {
            vmaDestroyBuffer(ctx.engine->allocator, pending_capture_.buffer, pending_capture_.allocation);
            pending_capture_ = {};
        }
        std::println("[{}] Cleanup complete", name());
    }

    void ScreenshotPlugin::on_event(const SDL_Event& event) {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1) {
            request_screenshot();
        }
    }

    void ScreenshotPlugin::request_screenshot() {
        screenshot_requested_ = true;
        std::println("[{}] Screenshot requested", name());
    }

    void ScreenshotPlugin::request_screenshot(const ScreenshotConfig& config) {
        config_ = config;
        request_screenshot();
    }

    void ScreenshotPlugin::capture_swapchain(engine::PluginContext& ctx, uint32_t image_index) {
        if (!ctx.engine || !ctx.cmd || !ctx.frame) return;

        // Get swapchain image info
        const uint32_t w = ctx.frame->extent.width;
        const uint32_t h = ctx.frame->extent.height;
        VkImage img = ctx.frame->swapchain_image;

        if (img == VK_NULL_HANDLE) {
            std::println("[{}] Error: Invalid swapchain image", name());
            return;
        }

        // Calculate buffer size
        const VkDeviceSize buffer_size = static_cast<VkDeviceSize>(w) * static_cast<VkDeviceSize>(h) * 4u;

        // Create staging buffer
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation alloc{};
        VmaAllocationInfo alloc_info{};

        VkBufferCreateInfo buffer_ci{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buffer_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        VmaAllocationCreateInfo alloc_ci{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };

        VK_CHECK(vmaCreateBuffer(ctx.engine->allocator, &buffer_ci, &alloc_ci, &buffer, &alloc, &alloc_info));

        // Transition image to transfer source
        VkImageMemoryBarrier2 to_src{
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

        VkDependencyInfo dep_to_src{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &to_src
        };

        vkCmdPipelineBarrier2(*ctx.cmd, &dep_to_src);

        // Copy image to buffer
        VkBufferImageCopy region{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {w, h, 1}
        };

        vkCmdCopyImageToBuffer(*ctx.cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);

        // Transition image back to present
        VkImageMemoryBarrier2 back_present{
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

        VkDependencyInfo dep_back{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &back_present
        };

        vkCmdPipelineBarrier2(*ctx.cmd, &dep_back);

        // Store capture data for processing after frame submission
        pending_capture_ = {buffer, alloc, w, h, generate_filename()};

        std::println("[{}] Screenshot capture queued", name());
    }

    void ScreenshotPlugin::save_screenshot(void* pixel_data, uint32_t width, uint32_t height, const std::string& path) {
        if (!pixel_data) return;

        const uint8_t* bgra = static_cast<const uint8_t*>(pixel_data);
        std::vector<uint8_t> rgba;
        rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4ull);

        // Convert BGRA to RGBA
        for (size_t i = 0, n = static_cast<size_t>(width) * static_cast<size_t>(height); i < n; ++i) {
            rgba[i * 4 + 0] = bgra[i * 4 + 2];  // R
            rgba[i * 4 + 1] = bgra[i * 4 + 1];  // G
            rgba[i * 4 + 2] = bgra[i * 4 + 0];  // B
            rgba[i * 4 + 3] = bgra[i * 4 + 3];  // A
        }

        // Save based on format
        switch (config_.format) {
        case ScreenshotFormat::PNG:
            stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data(), static_cast<int>(width) * 4);
            break;
        case ScreenshotFormat::JPG:
            stbi_write_jpg(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data(), config_.jpeg_quality);
            break;
        case ScreenshotFormat::BMP:
            stbi_write_bmp(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data());
            break;
        case ScreenshotFormat::TGA:
            stbi_write_tga(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data());
            break;
        }

        std::println("[{}] Screenshot saved: {}", name(), path);
    }

    std::string ScreenshotPlugin::generate_filename() const {
        if (!config_.auto_filename) {
            return config_.output_directory + "/" + config_.filename_prefix;
        }

        // Generate timestamp-based filename
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

        // Add extension based on format
        switch (config_.format) {
        case ScreenshotFormat::PNG: oss << ".png"; break;
        case ScreenshotFormat::JPG: oss << ".jpg"; break;
        case ScreenshotFormat::BMP: oss << ".bmp"; break;
        case ScreenshotFormat::TGA: oss << ".tga"; break;
        }

        return oss.str();
    }

} // namespace vk::plugins
