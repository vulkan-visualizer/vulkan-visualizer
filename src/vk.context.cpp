module;
#include "VkBootstrap.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <numbers>
#include <vector>
#include <vulkan/vulkan.h>
module vk.context;

void vk::context::DescriptorAllocator::init_pool(VkDevice device, uint32_t maxSets, std::span<const PoolSizeRatio> ratios) {
    maxSets = std::max(1u, maxSets);
    std::vector<VkDescriptorPoolSize> sizes;
    sizes.reserve(ratios.size());
    for (const auto& [type, ratio] : ratios) {
        const uint32_t count = std::max(1u, static_cast<uint32_t>(ratio * static_cast<float>(maxSets)));
        sizes.push_back(VkDescriptorPoolSize{.type = type, .descriptorCount = count});
    }
    const VkDescriptorPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .pNext = nullptr, .flags = 0u, .maxSets = maxSets, .poolSizeCount = static_cast<uint32_t>(sizes.size()), .pPoolSizes = sizes.data()};
    vk_check(vkCreateDescriptorPool(device, &info, nullptr, &pool));
}
void vk::context::DescriptorAllocator::clear_descriptors(VkDevice device) const {
    if (pool) vkResetDescriptorPool(device, pool, 0);
}
void vk::context::DescriptorAllocator::destroy_pool(VkDevice device) const {
    if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
}
VkDescriptorSet vk::context::DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout) const {
    const VkDescriptorSetAllocateInfo ai{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .pNext = nullptr, .descriptorPool = pool, .descriptorSetCount = 1u, .pSetLayouts = &layout};
    VkDescriptorSet ds{};
    vk_check(vkAllocateDescriptorSets(device, &ai, &ds));
    return ds;
}


void vk::context::transition_image_layout(const VkCommandBuffer& cmd, const AttachmentView& target, const VkImageLayout old_layout, const VkImageLayout new_layout) {
    const auto [src_stage, dst_stage, src_access, dst_access] = [&] {
        if (old_layout == VK_IMAGE_LAYOUT_GENERAL && new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            return std::tuple{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
        }
        return std::tuple{VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    }();

    const VkImageMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = src_stage, .srcAccessMask = src_access, .dstStageMask = dst_stage, .dstAccessMask = dst_access, .oldLayout = old_layout, .newLayout = new_layout, .image = target.image, .subresourceRange = {target.aspect, 0, 1, 0, 1}};

    const VkDependencyInfo dep_info{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};

    vkCmdPipelineBarrier2(cmd, &dep_info);
}

void vk::context::transition_to_color_attachment(const VkCommandBuffer& cmd, const VkImage image, const VkImageLayout old_layout) {
    const VkImageMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask                          = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask                         = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask                          = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask                         = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
        .oldLayout                             = old_layout,
        .newLayout                             = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image                                 = image,
        .subresourceRange                      = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

    const VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};

    vkCmdPipelineBarrier2(cmd, &dep);
}


void vk::context::Camera::update(float dt_sec, int viewport_w, int viewport_h) {
    viewport_width_  = std::max(1, viewport_w);
    viewport_height_ = std::max(1, viewport_h);
    apply_inertia(dt_sec);

    // Update fly mode movement
    if (state_.mode == CameraMode::Fly) {
        const float speed     = 2.0f * (key_shift_ ? 3.5f : 1.0f) * (key_ctrl_ ? 0.25f : 1.0f);
        const float move      = speed * dt_sec;
        const float yaw_rad   = state_.fly_yaw_deg * std::numbers::pi_v<float> / 180.0f;
        const float pitch_rad = state_.fly_pitch_deg * std::numbers::pi_v<float> / 180.0f;

        const context::Vec3 fwd{std::cos(pitch_rad) * std::cos(yaw_rad), std::sin(pitch_rad), std::cos(pitch_rad) * std::sin(yaw_rad)};
        const context::Vec3 right = fwd.cross(context::Vec3{0, 1, 0}).normalized();
        const context::Vec3 up    = right.cross(fwd).normalized();

        if (key_w_) state_.eye += fwd * move;
        if (key_s_) state_.eye -= fwd * move;
        if (key_a_) state_.eye -= right * move;
        if (key_d_) state_.eye += right * move;
        if (key_q_) state_.eye -= up * move;
        if (key_e_) state_.eye += up * move;
    }

    recompute_matrices();
}
void vk::context::Camera::handle_event(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT) lmb_ = true;
        if (event.button.button == SDL_BUTTON_MIDDLE) mmb_ = true;
        if (event.button.button == SDL_BUTTON_RIGHT) rmb_ = true;
        last_mx_ = static_cast<int>(event.button.x);
        last_my_ = static_cast<int>(event.button.y);
        if (state_.mode == CameraMode::Fly && rmb_) {
            fly_capturing_ = true;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT) lmb_ = false;
        if (event.button.button == SDL_BUTTON_MIDDLE) mmb_ = false;
        if (event.button.button == SDL_BUTTON_RIGHT) {
            rmb_           = false;
            fly_capturing_ = false;
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        {
            const int mx = static_cast<int>(event.motion.x);
            const int my = static_cast<int>(event.motion.y);
            const int dx = static_cast<int>(event.motion.xrel);
            const int dy = static_cast<int>(event.motion.yrel);

            if (state_.mode == CameraMode::Orbit) {
                const bool houdini_nav = key_space_ || key_alt_;
                if (houdini_nav) {
                    if (lmb_) {
                        // Rotate
                        constexpr float sens = 0.25f;
                        state_.yaw_deg += dx * sens;
                        state_.pitch_deg += dy * sens;
                        state_.pitch_deg = std::clamp(state_.pitch_deg, -89.5f, 89.5f);
                        yaw_vel_         = dx * sens * 10.0f;
                        pitch_vel_       = dy * sens * 10.0f;
                    } else if (mmb_) {
                        // Pan
                        const float base          = state_.projection == ProjectionMode::Orthographic ? std::max(1e-4f, state_.ortho_height) : std::max(1e-4f, state_.distance);
                        const float pan_speed     = base * 0.0015f * (key_shift_ ? 4.0f : 1.0f);
                        const float yaw_rad       = state_.yaw_deg * std::numbers::pi_v<float> / 180.0f;
                        const context::Vec3 right = context::Vec3{std::cos(yaw_rad), 0, std::sin(yaw_rad)}.cross(context::Vec3{0, 1, 0}).normalized();
                        state_.target -= right * (dx * pan_speed);
                        state_.target += context::Vec3{0, 1, 0} * (dy * pan_speed);
                        pan_x_vel_ = -dx * pan_speed * 10.0f;
                        pan_y_vel_ = dy * pan_speed * 10.0f;
                    } else if (rmb_) {
                        // Dolly/Zoom
                        const float factor = std::exp(dy * 0.01f * (key_shift_ ? 2.0f : 1.0f));
                        if (state_.projection == ProjectionMode::Perspective) {
                            state_.distance = std::clamp(state_.distance * factor, 1e-4f, 1e6f);
                        } else {
                            state_.ortho_height = std::clamp(state_.ortho_height * factor, 1e-4f, 1e6f);
                        }
                        zoom_vel_ = (factor - 1.0f) * 4.0f;
                    }
                    last_mx_ = mx;
                    last_my_ = my;
                }
            } else if (state_.mode == CameraMode::Fly) {
                if (rmb_ && fly_capturing_) {
                    constexpr float sens = 0.15f;
                    state_.fly_yaw_deg += dx * sens;
                    state_.fly_pitch_deg += dy * sens;
                    state_.fly_pitch_deg = std::clamp(state_.fly_pitch_deg, -89.0f, 89.0f);
                }
            }
        }
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        {
            const float scroll = static_cast<float>(event.wheel.y);
            if (state_.mode == CameraMode::Orbit) {
                const float z = std::exp(-scroll * 0.1f * (key_shift_ ? 2.0f : 1.0f));
                if (state_.projection == ProjectionMode::Perspective) {
                    state_.distance = std::clamp(state_.distance * z, 1e-4f, 1e6f);
                } else {
                    state_.ortho_height = std::clamp(state_.ortho_height * z, 1e-4f, 1e6f);
                }
                zoom_vel_ += -scroll * 0.25f;
            }
        }
        break;

    case SDL_EVENT_KEY_DOWN:
        {
            const SDL_Keycode k = event.key.key;
            if (k == SDLK_W) key_w_ = true;
            if (k == SDLK_A) key_a_ = true;
            if (k == SDLK_S) key_s_ = true;
            if (k == SDLK_D) key_d_ = true;
            if (k == SDLK_Q) key_q_ = true;
            if (k == SDLK_E) key_e_ = true;
            if (k == SDLK_LSHIFT || k == SDLK_RSHIFT) key_shift_ = true;
            if (k == SDLK_LCTRL || k == SDLK_RCTRL) key_ctrl_ = true;
            if (k == SDLK_SPACE) key_space_ = true;
            if (k == SDLK_LALT || k == SDLK_RALT) key_alt_ = true;
            if (k == SDLK_H) home_view();
        }
        break;

    case SDL_EVENT_KEY_UP:
        {
            const SDL_Keycode k = event.key.key;
            if (k == SDLK_W) key_w_ = false;
            if (k == SDLK_A) key_a_ = false;
            if (k == SDLK_S) key_s_ = false;
            if (k == SDLK_D) key_d_ = false;
            if (k == SDLK_Q) key_q_ = false;
            if (k == SDLK_E) key_e_ = false;
            if (k == SDLK_LSHIFT || k == SDLK_RSHIFT) key_shift_ = false;
            if (k == SDLK_LCTRL || k == SDLK_RCTRL) key_ctrl_ = false;
            if (k == SDLK_SPACE) key_space_ = false;
            if (k == SDLK_LALT || k == SDLK_RALT) key_alt_ = false;
        }
        break;

    default: break;
    }
}
vk::context::Vec3 vk::context::Camera::eye_position() const {
    if (state_.mode == CameraMode::Orbit) {
        const float yaw_rad   = state_.yaw_deg * std::numbers::pi_v<float> / 180.0f;
        const float pitch_rad = state_.pitch_deg * std::numbers::pi_v<float> / 180.0f;
        const float cp        = std::cos(pitch_rad);
        const float sp        = std::sin(pitch_rad);
        const float cy        = std::cos(yaw_rad);
        const float sy        = std::sin(yaw_rad);
        const context::Vec3 dir{cp * cy, -sp, cp * sy};
        return state_.target - dir * state_.distance;
    }
    return state_.eye;
}
void vk::context::Camera::set_state(const CameraState& s) {
    state_ = s;
    recompute_matrices();
}
void vk::context::Camera::set_mode(CameraMode m) {
    state_.mode = m;
    recompute_matrices();
}
void vk::context::Camera::set_projection(ProjectionMode p) {
    state_.projection = p;
    recompute_matrices();
}
void vk::context::Camera::home_view() {
    state_.mode      = CameraMode::Orbit;
    state_.target    = context::Vec3{0, 0, 0};
    state_.yaw_deg   = -45.0f;
    state_.pitch_deg = 25.0f;
    state_.distance  = 5.0f;
    recompute_matrices();
}
void vk::context::Camera::draw_imgui_panel() {
    ImGui::Begin("Camera Controls", &show_camera_panel_);

    auto state   = this->state();
    auto changed = false;

    const auto mode = static_cast<int>(state.mode);
    if (ImGui::RadioButton("Orbit Mode", mode == 0)) {
        state.mode = CameraMode::Orbit;
        changed    = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Fly Mode", mode == 1)) {
        state.mode = CameraMode::Fly;
        changed    = true;
    }

    ImGui::Separator();

    if (state.mode == CameraMode::Orbit) {
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
        home_view();
    }

    ImGui::Separator();
    ImGui::Text("Navigation:");
    ImGui::BulletText("Hold Space/Alt + LMB: Rotate");
    ImGui::BulletText("Hold Space/Alt + MMB: Pan");
    ImGui::BulletText("Hold Space/Alt + RMB: Zoom");
    ImGui::BulletText("Mouse Wheel: Zoom");
    ImGui::BulletText("Fly Mode: Hold RMB + WASDQE");

    if (changed) {
        set_state(state);
    }

    ImGui::End();
}
void vk::context::Camera::draw_mini_axis_gizmo() {
    auto* viewport = ImGui::GetMainViewport();
    if (!viewport) return;

    auto* draw_list = ImGui::GetForegroundDrawList(viewport);
    if (!draw_list) return;

    constexpr auto size   = 80.0f;
    constexpr auto margin = 16.0f;
    const ImVec2 center(viewport->Pos.x + viewport->Size.x - margin - size * 0.5f, viewport->Pos.y + margin + size * 0.5f);
    constexpr auto radius = size * 0.42f;

    draw_list->AddCircleFilled(center, size * 0.5f, IM_COL32(30, 32, 36, 180), 48);
    draw_list->AddCircle(center, size * 0.5f, IM_COL32(255, 255, 255, 60), 48, 1.5f);

    const auto& view = this->view_matrix();

    struct AxisInfo {
        context::Vec3 direction;
        ImU32 color;
        const char* label;
    };

    constexpr std::array axes{AxisInfo{{1, 0, 0}, IM_COL32(255, 80, 80, 255), "X"}, AxisInfo{{0, 1, 0}, IM_COL32(80, 255, 80, 255), "Y"}, AxisInfo{{0, 0, 1}, IM_COL32(100, 140, 255, 255), "Z"}};

    struct TransformedAxis {
        context::Vec3 view_dir;
        AxisInfo info;
    };

    std::array<TransformedAxis, 3> transformed{};
    for (size_t i = 0; i < 3; ++i) {
        const auto& dir = axes[i].direction;
        const context::Vec3 view_dir{view.m[0] * dir.x + view.m[4] * dir.y + view.m[8] * dir.z, view.m[1] * dir.x + view.m[5] * dir.y + view.m[9] * dir.z, view.m[2] * dir.x + view.m[6] * dir.y + view.m[10] * dir.z};
        transformed[i] = {view_dir, axes[i]};
    }

    const auto draw_axis = [&](const TransformedAxis& axis, const bool is_back) {
        const auto thickness  = is_back ? 2.0f : 3.0f;
        const auto base_color = axis.info.color;
        const auto color      = is_back ? IM_COL32((base_color >> IM_COL32_R_SHIFT) & 0xFF, (base_color >> IM_COL32_G_SHIFT) & 0xFF, (base_color >> IM_COL32_B_SHIFT) & 0xFF, 120) : base_color;

        const ImVec2 end_point(center.x + axis.view_dir.x * radius, center.y - axis.view_dir.y * radius);

        draw_list->AddLine(center, end_point, color, thickness);
        const auto circle_radius = is_back ? 3.0f : 4.5f;
        draw_list->AddCircleFilled(end_point, circle_radius, color, 12);

        if (!is_back) {
            const auto label_offset_x = axis.view_dir.x >= 0 ? 8.0f : -20.0f;
            const auto label_offset_y = axis.view_dir.y >= 0 ? -18.0f : 4.0f;
            const ImVec2 label_pos(end_point.x + label_offset_x, end_point.y + label_offset_y);
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
void vk::context::Camera::recompute_matrices() {
    // Compute view matrix
    if (state_.mode == CameraMode::Orbit) {
        const float yaw_rad   = state_.yaw_deg * std::numbers::pi_v<float> / 180.0f;
        const float pitch_rad = state_.pitch_deg * std::numbers::pi_v<float> / 180.0f;
        const float cp        = std::cos(pitch_rad);
        const float sp        = std::sin(pitch_rad);
        const float cy        = std::cos(yaw_rad);
        const float sy        = std::sin(yaw_rad);
        const context::Vec3 dir{cp * cy, -sp, cp * sy};
        const context::Vec3 eye = state_.target - dir * state_.distance;
        view_                   = context::Mat4::look_at(eye, state_.target, context::Vec3{0, 1, 0});
    } else {
        const float yaw_rad   = state_.fly_yaw_deg * std::numbers::pi_v<float> / 180.0f;
        const float pitch_rad = state_.fly_pitch_deg * std::numbers::pi_v<float> / 180.0f;
        const float cp        = std::cos(pitch_rad);
        const float sp        = std::sin(pitch_rad);
        const float cy        = std::cos(yaw_rad);
        const float sy        = std::sin(yaw_rad);
        const context::Vec3 fwd{cp * cy, sp, cp * sy};
        view_ = context::Mat4::look_at(state_.eye, state_.eye + fwd, context::Vec3{0, 1, 0});
    }

    // Compute projection matrix
    const float aspect = static_cast<float>(std::max(1, viewport_width_)) / static_cast<float>(std::max(1, viewport_height_));
    if (state_.projection == ProjectionMode::Perspective) {
        const float fov_rad = state_.fov_y_deg * std::numbers::pi_v<float> / 180.0f;
        proj_               = context::Mat4::perspective(fov_rad, aspect, state_.znear, state_.zfar);
    } else {
        // For orthographic, we'll need to implement make_ortho if needed
        const float fov_rad = state_.fov_y_deg * std::numbers::pi_v<float> / 180.0f;
        proj_               = context::Mat4::perspective(fov_rad, aspect, state_.znear, state_.zfar);
    }
}
void vk::context::Camera::apply_inertia(float dt) {
    const float damp = std::exp(-dt * 6.0f);
    if (!(lmb_ || rmb_ || mmb_) && state_.mode == CameraMode::Orbit) {
        state_.yaw_deg += yaw_vel_ * dt;
        state_.pitch_deg += pitch_vel_ * dt;
        state_.pitch_deg = std::clamp(state_.pitch_deg, -89.5f, 89.5f);
        state_.target.x += pan_x_vel_ * dt;
        state_.target.y += pan_y_vel_ * dt;

        if (state_.projection == ProjectionMode::Perspective) {
            state_.distance *= (1.0f + zoom_vel_ * dt);
        } else {
            state_.ortho_height *= (1.0f + zoom_vel_ * dt);
            state_.ortho_height = std::clamp(state_.ortho_height, 1e-4f, 1e6f);
        }

        yaw_vel_ *= damp;
        pitch_vel_ *= damp;
        pan_x_vel_ *= damp;
        pan_y_vel_ *= damp;
        zoom_vel_ *= damp;
    }
}
