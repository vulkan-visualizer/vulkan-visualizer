module vk.camera;

import vk.math;
import std;

namespace vk::camera {

    static float clampf(const float x, const float a, const float b) noexcept {
        if (x < a) return a;
        if (x > b) return b;
        return x;
    }

    float Camera::clamp_pitch(const float x) noexcept {
        constexpr float lim = 1.55334303427f;
        return clampf(x, -lim, lim);
    }

    void Camera::set_config(const CameraConfig& cfg) noexcept {
        cfg_ = cfg;
        update_projection(vw_, vh_);
        recompute_view();
        recompute_viewproj();
    }

    void Camera::set_state(const CameraState& st) noexcept {
        st_                 = st;
        st_.orbit.pitch_rad = clamp_pitch(st_.orbit.pitch_rad);
        st_.fly.pitch_rad   = clamp_pitch(st_.fly.pitch_rad);
        recompute_view();
        recompute_viewproj();
    }

    void Camera::set_mode(const Mode m) noexcept {
        st_.mode = m;
        recompute_view();
        recompute_viewproj();
    }

    void Camera::set_projection(const Projection p) noexcept {
        cfg_.projection = p;
        update_projection(vw_, vh_);
        recompute_view();
        recompute_viewproj();
    }

    void Camera::home() noexcept {
        st_.mode            = Mode::Orbit;
        st_.orbit.target    = {0.0f, 0.0f, 0.0f, 0.0f};
        st_.orbit.distance  = 5.0f;
        st_.orbit.yaw_rad   = -0.78539816339f;
        st_.orbit.pitch_rad = 0.43633231299f;

        st_.fly.eye       = {0.0f, 0.0f, 5.0f, 0.0f};
        st_.fly.yaw_rad   = -1.57079632679f;
        st_.fly.pitch_rad = 0.0f;

        update_projection(vw_, vh_);
        recompute_view();
        recompute_viewproj();
    }

    const CameraConfig& Camera::config() const noexcept {
        return cfg_;
    }
    const CameraState& Camera::state() const noexcept {
        return st_;
    }
    const CameraMatrices& Camera::matrices() const noexcept {
        return m_;
    }

    void Camera::update(const float dt_sec, const std::uint32_t viewport_w, const std::uint32_t viewport_h, const CameraInput& input) noexcept {
        vw_ = viewport_w ? viewport_w : 1;
        vh_ = viewport_h ? viewport_h : 1;

        update_projection(vw_, vh_);

        if (st_.mode == Mode::Orbit) {
            update_orbit(dt_sec, input, vw_, vh_);
        } else {
            update_fly(dt_sec, input);
        }

        recompute_view();
        recompute_viewproj();
    }

    void Camera::update_projection(const std::uint32_t w, const std::uint32_t h) noexcept {
        const float aspect = static_cast<float>(w) / static_cast<float>(h ? h : 1);

        if (cfg_.projection == Projection::Perspective) {
            m_.proj = math::perspective_vk(cfg_.fov_y_rad, aspect, cfg_.znear, cfg_.zfar);
        } else {
            const float hh = cfg_.ortho_height * 0.5f;
            const float hw = hh * aspect;

            math::mat4 p{};
            p.c0    = {1.0f / hw, 0.0f, 0.0f, 0.0f};
            p.c1    = {0.0f, 1.0f / hh, 0.0f, 0.0f};
            p.c2    = {0.0f, 0.0f, 1.0f / (cfg_.zfar - cfg_.znear), 0.0f};
            p.c3    = {0.0f, 0.0f, -cfg_.znear / (cfg_.zfar - cfg_.znear), 1.0f};
            m_.proj = p;
        }
    }

    void Camera::update_orbit(float, const CameraInput& in, const std::uint32_t vw, const std::uint32_t vh) noexcept {
        const bool houdini = in.alt || in.space;

        if (houdini && in.lmb) {
            st_.orbit.yaw_rad += in.mouse_dx * cfg_.orbit_rotate_sens;
            st_.orbit.pitch_rad += in.mouse_dy * cfg_.orbit_rotate_sens;
            st_.orbit.pitch_rad = clamp_pitch(st_.orbit.pitch_rad);
        }

        if (houdini && in.mmb) {
            const float base = (cfg_.projection == Projection::Orthographic) ? std::max(1e-4f, cfg_.ortho_height) : std::max(1e-4f, st_.orbit.distance);

            const float pan = base * cfg_.orbit_pan_sens;

            const float yaw = st_.orbit.yaw_rad;
            math::vec3 forward{std::cos(yaw), 0.0f, std::sin(yaw), 0.0f};
            forward = vk::math::normalize(forward);

            constexpr math::vec3 up{0.0f, 1.0f, 0.0f, 0.0f};
            const math::vec3 right = vk::math::normalize(vk::math::cross(forward, up));

            const auto sx   = static_cast<float>(vw);
            const auto sy   = static_cast<float>(vh);
            const float ndx = (sx > 0.0f) ? (in.mouse_dx / sx) : 0.0f;
            const float ndy = (sy > 0.0f) ? (in.mouse_dy / sy) : 0.0f;

            st_.orbit.target = st_.orbit.target - right * (ndx * pan * 2.0f);
            st_.orbit.target = st_.orbit.target + up * (ndy * pan * 2.0f);
        }

        if ((houdini && in.rmb) || (in.scroll != 0.0f)) {
            if (const float s = in.scroll + (houdini && in.rmb ? (-in.mouse_dy * 0.01f) : 0.0f); s != 0.0f) {
                const float factor = std::exp(-s * cfg_.orbit_zoom_sens);
                if (cfg_.projection == Projection::Perspective) {
                    st_.orbit.distance = clampf(st_.orbit.distance * factor, 1e-4f, 1e6f);
                } else {
                    cfg_.ortho_height = clampf(cfg_.ortho_height * factor, 1e-4f, 1e6f);
                }
            }
        }
    }

    void Camera::update_fly(const float dt, const CameraInput& in) noexcept {
        if (in.rmb) {
            st_.fly.yaw_rad += in.mouse_dx * cfg_.fly_look_sens;
            st_.fly.pitch_rad += in.mouse_dy * cfg_.fly_look_sens;
            st_.fly.pitch_rad = clamp_pitch(st_.fly.pitch_rad);
        }

        float speed = cfg_.fly_speed;
        if (in.shift) speed *= cfg_.fly_shift_mul;
        if (in.ctrl) speed *= cfg_.fly_ctrl_mul;
        const float move = speed * dt;

        const float cy = std::cos(st_.fly.yaw_rad);
        const float sy = std::sin(st_.fly.yaw_rad);
        const float cp = std::cos(st_.fly.pitch_rad);
        const float sp = std::sin(st_.fly.pitch_rad);

        math::vec3 fwd{cp * cy, sp, cp * sy, 0.0f};
        fwd = vk::math::normalize(fwd);

        math::vec3 up{0.0f, 1.0f, 0.0f, 0.0f};
        const math::vec3 right = vk::math::normalize(vk::math::cross(fwd, up));
        up                     = vk::math::normalize(vk::math::cross(right, fwd));

        if (in.forward) st_.fly.eye = st_.fly.eye + fwd * move;
        if (in.backward) st_.fly.eye = st_.fly.eye - fwd * move;
        if (in.right) st_.fly.eye = st_.fly.eye + right * move;
        if (in.left) st_.fly.eye = st_.fly.eye - right * move;
        if (in.up) st_.fly.eye = st_.fly.eye + up * move;
        if (in.down) st_.fly.eye = st_.fly.eye - up * move;
    }

    void Camera::recompute_view() noexcept {
        if (st_.mode == Mode::Orbit) {
            const float cy = std::cos(st_.orbit.yaw_rad);
            const float sy = std::sin(st_.orbit.yaw_rad);
            const float cp = std::cos(st_.orbit.pitch_rad);
            const float sp = std::sin(st_.orbit.pitch_rad);

            math::vec3 dir{cp * cy, sp, cp * sy, 0.0f};
            dir = vk::math::normalize(dir);

            m_.eye  = st_.orbit.target - dir * st_.orbit.distance;
            m_.view = vk::math::look_at(m_.eye, st_.orbit.target, math::vec3{0.0f, 1.0f, 0.0f, 0.0f});
        } else {
            const float cy = std::cos(st_.fly.yaw_rad);
            const float sy = std::sin(st_.fly.yaw_rad);
            const float cp = std::cos(st_.fly.pitch_rad);
            const float sp = std::sin(st_.fly.pitch_rad);

            math::vec3 fwd{cp * cy, sp, cp * sy, 0.0f};
            fwd = vk::math::normalize(fwd);

            m_.eye                  = st_.fly.eye;
            const math::vec3 center = st_.fly.eye + fwd;
            m_.view                 = vk::math::look_at(st_.fly.eye, center, math::vec3{0.0f, 1.0f, 0.0f, 0.0f});
        }
    }

    void Camera::recompute_viewproj() noexcept {
        m_.view_proj = m_.proj * m_.view;
    }

} // namespace vk::camera
