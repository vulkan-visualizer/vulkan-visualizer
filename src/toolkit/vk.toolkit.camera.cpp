module;
#include "VkBootstrap.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <numbers>
#include <vector>
#include <vulkan/vulkan.h>
module vk.toolkit.camera;


void vk::toolkit::camera::Camera::update(float dt_sec, int viewport_w, int viewport_h) {
    this->viewport_width_  = std::max(1, viewport_w);
    this->viewport_height_ = std::max(1, viewport_h);
    apply_inertia(dt_sec);

    // Update fly mode movement
    if (this->state_.mode == CameraMode::Fly) {
        const float speed     = 2.0f * (this->key_shift_ ? 3.5f : 1.0f) * (this->key_ctrl_ ? 0.25f : 1.0f);
        const float move      = speed * dt_sec;
        const float yaw_rad   = this->state_.fly_yaw_deg * std::numbers::pi_v<float> / 180.0f;
        const float pitch_rad = this->state_.fly_pitch_deg * std::numbers::pi_v<float> / 180.0f;

        const math::Vec3 fwd{std::cos(pitch_rad) * std::cos(yaw_rad), std::sin(pitch_rad), std::cos(pitch_rad) * std::sin(yaw_rad)};
        const math::Vec3 right = fwd.cross(math::Vec3{0, 1, 0}).normalized();
        const math::Vec3 up    = right.cross(fwd).normalized();

        if (this->key_w_) this->state_.eye += fwd * move;
        if (this->key_s_) this->state_.eye -= fwd * move;
        if (this->key_a_) this->state_.eye -= right * move;
        if (this->key_d_) this->state_.eye += right * move;
        if (this->key_q_) this->state_.eye -= up * move;
        if (this->key_e_) this->state_.eye += up * move;
    }

    recompute_matrices();
}
void vk::toolkit::camera::Camera::handle_event(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT) this->lmb_ = true;
        if (event.button.button == SDL_BUTTON_MIDDLE) this->mmb_ = true;
        if (event.button.button == SDL_BUTTON_RIGHT) this->rmb_ = true;
        this->last_mx_ = static_cast<int>(event.button.x);
        this->last_my_ = static_cast<int>(event.button.y);
        if (this->state_.mode == CameraMode::Fly && this->rmb_) {
            this->fly_capturing_ = true;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT) this->lmb_ = false;
        if (event.button.button == SDL_BUTTON_MIDDLE) this->mmb_ = false;
        if (event.button.button == SDL_BUTTON_RIGHT) {
            this->rmb_           = false;
            this->fly_capturing_ = false;
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        {
            const int mx = static_cast<int>(event.motion.x);
            const int my = static_cast<int>(event.motion.y);
            const int dx = static_cast<int>(event.motion.xrel);
            const int dy = static_cast<int>(event.motion.yrel);

            if (this->state_.mode == CameraMode::Orbit) {
                const bool houdini_nav = this->key_space_ || this->key_alt_;
                if (houdini_nav) {
                    if (this->lmb_) {
                        // Rotate
                        constexpr float sens = 0.25f;
                        this->state_.yaw_deg += dx * sens;
                        this->state_.pitch_deg += dy * sens;
                        this->state_.pitch_deg = std::clamp(this->state_.pitch_deg, -89.5f, 89.5f);
                        this->yaw_vel_         = dx * sens * 10.0f;
                        this->pitch_vel_       = dy * sens * 10.0f;
                    } else if (this->mmb_) {
                        // Pan
                        const float base                = this->state_.projection == ProjectionMode::Orthographic ? std::max(1e-4f, this->state_.ortho_height) : std::max(1e-4f, this->state_.distance);
                        const float pan_speed           = base * 0.0015f * (this->key_shift_ ? 4.0f : 1.0f);
                        const float yaw_rad             = this->state_.yaw_deg * std::numbers::pi_v<float> / 180.0f;
                        const math::Vec3 right = math::Vec3{std::cos(yaw_rad), 0, std::sin(yaw_rad)}.cross(math::Vec3{0, 1, 0}).normalized();
                        this->state_.target -= right * (dx * pan_speed);
                        this->state_.target += math::Vec3{0, 1, 0} * (dy * pan_speed);
                        this->pan_x_vel_ = -dx * pan_speed * 10.0f;
                        this->pan_y_vel_ = dy * pan_speed * 10.0f;
                    } else if (this->rmb_) {
                        // Dolly/Zoom
                        const float factor = std::exp(dy * 0.01f * (this->key_shift_ ? 2.0f : 1.0f));
                        if (this->state_.projection == ProjectionMode::Perspective) {
                            this->state_.distance = std::clamp(this->state_.distance * factor, 1e-4f, 1e6f);
                        } else {
                            this->state_.ortho_height = std::clamp(this->state_.ortho_height * factor, 1e-4f, 1e6f);
                        }
                        this->zoom_vel_ = (factor - 1.0f) * 4.0f;
                    }
                    this->last_mx_ = mx;
                    this->last_my_ = my;
                }
            } else if (this->state_.mode == CameraMode::Fly) {
                if (this->rmb_ && this->fly_capturing_) {
                    constexpr float sens = 0.15f;
                    this->state_.fly_yaw_deg += dx * sens;
                    this->state_.fly_pitch_deg += dy * sens;
                    this->state_.fly_pitch_deg = std::clamp(this->state_.fly_pitch_deg, -89.0f, 89.0f);
                }
            }
        }
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        {
            const float scroll = static_cast<float>(event.wheel.y);
            if (this->state_.mode == CameraMode::Orbit) {
                const float z = std::exp(-scroll * 0.1f * (this->key_shift_ ? 2.0f : 1.0f));
                if (this->state_.projection == ProjectionMode::Perspective) {
                    this->state_.distance = std::clamp(this->state_.distance * z, 1e-4f, 1e6f);
                } else {
                    this->state_.ortho_height = std::clamp(this->state_.ortho_height * z, 1e-4f, 1e6f);
                }
                this->zoom_vel_ += -scroll * 0.25f;
            }
        }
        break;

    case SDL_EVENT_KEY_DOWN:
        {
            const SDL_Keycode k = event.key.key;
            if (k == SDLK_W) this->key_w_ = true;
            if (k == SDLK_A) this->key_a_ = true;
            if (k == SDLK_S) this->key_s_ = true;
            if (k == SDLK_D) this->key_d_ = true;
            if (k == SDLK_Q) this->key_q_ = true;
            if (k == SDLK_E) this->key_e_ = true;
            if (k == SDLK_LSHIFT || k == SDLK_RSHIFT) this->key_shift_ = true;
            if (k == SDLK_LCTRL || k == SDLK_RCTRL) this->key_ctrl_ = true;
            if (k == SDLK_SPACE) this->key_space_ = true;
            if (k == SDLK_LALT || k == SDLK_RALT) this->key_alt_ = true;
            if (k == SDLK_H) home_view();
        }
        break;

    case SDL_EVENT_KEY_UP:
        {
            const SDL_Keycode k = event.key.key;
            if (k == SDLK_W) this->key_w_ = false;
            if (k == SDLK_A) this->key_a_ = false;
            if (k == SDLK_S) this->key_s_ = false;
            if (k == SDLK_D) this->key_d_ = false;
            if (k == SDLK_Q) this->key_q_ = false;
            if (k == SDLK_E) this->key_e_ = false;
            if (k == SDLK_LSHIFT || k == SDLK_RSHIFT) this->key_shift_ = false;
            if (k == SDLK_LCTRL || k == SDLK_RCTRL) this->key_ctrl_ = false;
            if (k == SDLK_SPACE) this->key_space_ = false;
            if (k == SDLK_LALT || k == SDLK_RALT) this->key_alt_ = false;
        }
        break;

    default: break;
    }
}
vk::toolkit::math::Vec3 vk::toolkit::camera::Camera::eye_position() const {
    if (state_.mode == CameraMode::Orbit) {
        const float yaw_rad   = state_.yaw_deg * std::numbers::pi_v<float> / 180.0f;
        const float pitch_rad = state_.pitch_deg * std::numbers::pi_v<float> / 180.0f;
        const float cp        = std::cos(pitch_rad);
        const float sp        = std::sin(pitch_rad);
        const float cy        = std::cos(yaw_rad);
        const float sy        = std::sin(yaw_rad);
        const math::Vec3 dir{cp * cy, -sp, cp * sy};
        return state_.target - dir * state_.distance;
    }
    return state_.eye;
}
void vk::toolkit::camera::Camera::set_state(const CameraState& s) {
    state_ = s;
    recompute_matrices();
}
void vk::toolkit::camera::Camera::set_mode(CameraMode m) {
    state_.mode = m;
    recompute_matrices();
}
void vk::toolkit::camera::Camera::set_projection(ProjectionMode p) {
    state_.projection = p;
    recompute_matrices();
}
void vk::toolkit::camera::Camera::home_view() {
    state_.mode      = CameraMode::Orbit;
    state_.target    = math::Vec3{0, 0, 0};
    state_.yaw_deg   = -45.0f;
    state_.pitch_deg = 25.0f;
    state_.distance  = 5.0f;
    recompute_matrices();
}
void vk::toolkit::camera::Camera::draw_imgui_panel() {
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
        this->set_state(state);
    }

    ImGui::End();
}
void vk::toolkit::camera::Camera::draw_mini_axis_gizmo() {
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
        math::Vec3 direction;
        ImU32 color;
        const char* label;
    };

    constexpr std::array axes{AxisInfo{{1, 0, 0}, IM_COL32(255, 80, 80, 255), "X"}, AxisInfo{{0, 1, 0}, IM_COL32(80, 255, 80, 255), "Y"}, AxisInfo{{0, 0, 1}, IM_COL32(100, 140, 255, 255), "Z"}};

    struct TransformedAxis {
        math::Vec3 view_dir;
        AxisInfo info;
    };

    std::array<TransformedAxis, 3> transformed{};
    for (size_t i = 0; i < 3; ++i) {
        const auto& dir = axes[i].direction;
        const math::Vec3 view_dir{view.m[0] * dir.x + view.m[4] * dir.y + view.m[8] * dir.z, view.m[1] * dir.x + view.m[5] * dir.y + view.m[9] * dir.z, view.m[2] * dir.x + view.m[6] * dir.y + view.m[10] * dir.z};
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
void vk::toolkit::camera::Camera::recompute_matrices() {
    // Compute view matrix
    if (state_.mode == CameraMode::Orbit) {
        const float yaw_rad   = state_.yaw_deg * std::numbers::pi_v<float> / 180.0f;
        const float pitch_rad = state_.pitch_deg * std::numbers::pi_v<float> / 180.0f;
        const float cp        = std::cos(pitch_rad);
        const float sp        = std::sin(pitch_rad);
        const float cy        = std::cos(yaw_rad);
        const float sy        = std::sin(yaw_rad);
        const math::Vec3 dir{cp * cy, -sp, cp * sy};
        const math::Vec3 eye = state_.target - dir * state_.distance;
        view_                         = math::Mat4::look_at(eye, state_.target, math::Vec3{0, 1, 0});
    } else {
        const float yaw_rad   = state_.fly_yaw_deg * std::numbers::pi_v<float> / 180.0f;
        const float pitch_rad = state_.fly_pitch_deg * std::numbers::pi_v<float> / 180.0f;
        const float cp        = std::cos(pitch_rad);
        const float sp        = std::sin(pitch_rad);
        const float cy        = std::cos(yaw_rad);
        const float sy        = std::sin(yaw_rad);
        const math::Vec3 fwd{cp * cy, sp, cp * sy};
        view_ = math::Mat4::look_at(state_.eye, state_.eye + fwd, math::Vec3{0, 1, 0});
    }

    // Compute projection matrix
    const float aspect = static_cast<float>(std::max(1, viewport_width_)) / static_cast<float>(std::max(1, viewport_height_));
    if (state_.projection == ProjectionMode::Perspective) {
        const float fov_rad = state_.fov_y_deg * std::numbers::pi_v<float> / 180.0f;
        proj_               = math::Mat4::perspective(fov_rad, aspect, state_.znear, state_.zfar);
    } else {
        // For orthographic, we'll need to implement make_ortho if needed
        const float fov_rad = state_.fov_y_deg * std::numbers::pi_v<float> / 180.0f;
        proj_               = math::Mat4::perspective(fov_rad, aspect, state_.znear, state_.zfar);
    }
}
void vk::toolkit::camera::Camera::apply_inertia(float dt) {
    const float damp = std::exp(-dt * 6.0f);
    if (!(this->lmb_ || this->rmb_ || this->mmb_) && this->state_.mode == CameraMode::Orbit) {
        this->state_.yaw_deg += this->yaw_vel_ * dt;
        this->state_.pitch_deg += this->pitch_vel_ * dt;
        this->state_.pitch_deg = std::clamp(this->state_.pitch_deg, -89.5f, 89.5f);
        this->state_.target.x += this->pan_x_vel_ * dt;
        this->state_.target.y += this->pan_y_vel_ * dt;

        if (this->state_.projection == ProjectionMode::Perspective) {
            this->state_.distance *= (1.0f + this->zoom_vel_ * dt);
        } else {
            this->state_.ortho_height *= (1.0f + this->zoom_vel_ * dt);
            this->state_.ortho_height = std::clamp(this->state_.ortho_height, 1e-4f, 1e6f);
        }

        this->yaw_vel_ *= damp;
        this->pitch_vel_ *= damp;
        this->pan_x_vel_ *= damp;
        this->pan_y_vel_ *= damp;
        this->zoom_vel_ *= damp;
    }
}
