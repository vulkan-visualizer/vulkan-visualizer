module;
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <numbers>
module vk.camera;

namespace vk::camera {
    // ============================================================================
    // Vec3 Implementation
    // ============================================================================

    float Vec3::length() const {
        return std::sqrt(dot(*this));
    }

    Vec3 Vec3::normalized() const {
        const float len = length();
        return len > 0.0f ? *this / len : Vec3{0, 0, 0};
    }

    // ============================================================================
    // Mat4 Implementation
    // ============================================================================

    Mat4 Mat4::look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
        const Vec3 f = (center - eye).normalized();
        const Vec3 s = f.cross(up).normalized();
        const Vec3 u = s.cross(f);

        Mat4 result;
        result.m = {
            s.x, u.x, -f.x, 0,
            s.y, u.y, -f.y, 0,
            s.z, u.z, -f.z, 0,
            -s.dot(eye), -u.dot(eye), f.dot(eye), 1
        };
        return result;
    }

    Mat4 Mat4::perspective(float fov_y_rad, float aspect, float znear, float zfar) {
        const float tan_half_fov = std::tan(fov_y_rad * 0.5f);
        Mat4 result{};
        result.m = {
            1.0f / (aspect * tan_half_fov), 0, 0, 0,
            0, -1.0f / tan_half_fov, 0, 0,
            0, 0, (zfar + znear) / (znear - zfar), -1,
            0, 0, (2.0f * zfar * znear) / (znear - zfar), 0
        };
        return result;
    }

    Mat4 Mat4::operator*(const Mat4& o) const {
        Mat4 result{};
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                result.m[c * 4 + r] = m[0 * 4 + r] * o.m[c * 4 + 0] +
                                      m[1 * 4 + r] * o.m[c * 4 + 1] +
                                      m[2 * 4 + r] * o.m[c * 4 + 2] +
                                      m[3 * 4 + r] * o.m[c * 4 + 3];
            }
        }
        return result;
    }

    // ============================================================================
    // Camera Implementation
    // ============================================================================

    void Camera::update(float dt_sec, int viewport_w, int viewport_h) {
        viewport_width_ = std::max(1, viewport_w);
        viewport_height_ = std::max(1, viewport_h);
        apply_inertia(dt_sec);

        // Update fly mode movement
        if (state_.mode == CameraMode::Fly) {
            const float speed = 2.0f * (key_shift_ ? 3.5f : 1.0f) * (key_ctrl_ ? 0.25f : 1.0f);
            const float move = speed * dt_sec;
            const float yaw_rad = state_.fly_yaw_deg * std::numbers::pi_v<float> / 180.0f;
            const float pitch_rad = state_.fly_pitch_deg * std::numbers::pi_v<float> / 180.0f;

            const Vec3 fwd{
                std::cos(pitch_rad) * std::cos(yaw_rad),
                std::sin(pitch_rad),
                std::cos(pitch_rad) * std::sin(yaw_rad)
            };
            const Vec3 right = fwd.cross(Vec3{0, 1, 0}).normalized();
            const Vec3 up = right.cross(fwd).normalized();

            if (key_w_) state_.eye += fwd * move;
            if (key_s_) state_.eye -= fwd * move;
            if (key_a_) state_.eye -= right * move;
            if (key_d_) state_.eye += right * move;
            if (key_q_) state_.eye -= up * move;
            if (key_e_) state_.eye += up * move;
        }

        recompute_matrices();
    }

    void Camera::handle_event(const SDL_Event& event) {
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
                rmb_ = false;
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
                            yaw_vel_ = dx * sens * 10.0f;
                            pitch_vel_ = dy * sens * 10.0f;
                        } else if (mmb_) {
                            // Pan
                            const float base = state_.projection == ProjectionMode::Orthographic
                                ? std::max(1e-4f, state_.ortho_height)
                                : std::max(1e-4f, state_.distance);
                            const float pan_speed = base * 0.0015f * (key_shift_ ? 4.0f : 1.0f);
                            const float yaw_rad = state_.yaw_deg * std::numbers::pi_v<float> / 180.0f;
                            const Vec3 right = Vec3{std::cos(yaw_rad), 0, std::sin(yaw_rad)}.cross(Vec3{0, 1, 0}).normalized();
                            state_.target -= right * (dx * pan_speed);
                            state_.target += Vec3{0, 1, 0} * (dy * pan_speed);
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

    Vec3 Camera::eye_position() const {
        if (state_.mode == CameraMode::Orbit) {
            const float yaw_rad = state_.yaw_deg * std::numbers::pi_v<float> / 180.0f;
            const float pitch_rad = state_.pitch_deg * std::numbers::pi_v<float> / 180.0f;
            const float cp = std::cos(pitch_rad);
            const float sp = std::sin(pitch_rad);
            const float cy = std::cos(yaw_rad);
            const float sy = std::sin(yaw_rad);
            const Vec3 dir{cp * cy, -sp, cp * sy};
            return state_.target - dir * state_.distance;
        }
        return state_.eye;
    }

    void Camera::set_state(const CameraState& s) {
        state_ = s;
        recompute_matrices();
    }

    void Camera::set_mode(CameraMode m) {
        state_.mode = m;
        recompute_matrices();
    }

    void Camera::set_projection(ProjectionMode p) {
        state_.projection = p;
        recompute_matrices();
    }

    void Camera::home_view() {
        state_.mode = CameraMode::Orbit;
        state_.target = Vec3{0, 0, 0};
        state_.yaw_deg = -45.0f;
        state_.pitch_deg = 25.0f;
        state_.distance = 5.0f;
        recompute_matrices();
    }

    void Camera::recompute_matrices() {
        // Compute view matrix
        if (state_.mode == CameraMode::Orbit) {
            const float yaw_rad = state_.yaw_deg * std::numbers::pi_v<float> / 180.0f;
            const float pitch_rad = state_.pitch_deg * std::numbers::pi_v<float> / 180.0f;
            const float cp = std::cos(pitch_rad);
            const float sp = std::sin(pitch_rad);
            const float cy = std::cos(yaw_rad);
            const float sy = std::sin(yaw_rad);
            const Vec3 dir{cp * cy, -sp, cp * sy};
            const Vec3 eye = state_.target - dir * state_.distance;
            view_ = Mat4::look_at(eye, state_.target, Vec3{0, 1, 0});
        } else {
            const float yaw_rad = state_.fly_yaw_deg * std::numbers::pi_v<float> / 180.0f;
            const float pitch_rad = state_.fly_pitch_deg * std::numbers::pi_v<float> / 180.0f;
            const float cp = std::cos(pitch_rad);
            const float sp = std::sin(pitch_rad);
            const float cy = std::cos(yaw_rad);
            const float sy = std::sin(yaw_rad);
            const Vec3 fwd{cp * cy, sp, cp * sy};
            view_ = Mat4::look_at(state_.eye, state_.eye + fwd, Vec3{0, 1, 0});
        }

        // Compute projection matrix
        const float aspect = static_cast<float>(std::max(1, viewport_width_)) /
                             static_cast<float>(std::max(1, viewport_height_));
        if (state_.projection == ProjectionMode::Perspective) {
            const float fov_rad = state_.fov_y_deg * std::numbers::pi_v<float> / 180.0f;
            proj_ = Mat4::perspective(fov_rad, aspect, state_.znear, state_.zfar);
        } else {
            // For orthographic, we'll need to implement make_ortho if needed
            const float fov_rad = state_.fov_y_deg * std::numbers::pi_v<float> / 180.0f;
            proj_ = Mat4::perspective(fov_rad, aspect, state_.znear, state_.zfar);
        }
    }

    void Camera::apply_inertia(float dt) {
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

} // namespace vk::camera

