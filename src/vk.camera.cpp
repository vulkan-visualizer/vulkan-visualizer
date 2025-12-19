module vk.camera;

import vk.math;
import std;

namespace vk::camera {

    static float clampf(float x, float a, float b) noexcept {
        if (x < a) return a;
        if (x > b) return b;
        return x;
    }

    float Camera::clamp_pitch(float x) noexcept {
        constexpr float lim = 1.55334303427f;
        return clampf(x, -lim, lim);
    }

    static math::vec3 axis_dir_to_vec3(const AxisDir d) noexcept {
        const float s = (d.sign == Sign::Positive) ? 1.0f : -1.0f;
        switch (d.axis) {
        case Axis::X: return {s, 0.0f, 0.0f, 0.0f};
        case Axis::Y: return {0.0f, s, 0.0f, 0.0f};
        case Axis::Z: return {0.0f, 0.0f, s, 0.0f};
        }
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }

    static math::vec3 add3(math::vec3 a, math::vec3 b) noexcept {
        return vk::math::add(a, b);
    }
    static math::vec3 sub3(math::vec3 a, math::vec3 b) noexcept {
        return vk::math::sub(a, b);
    }
    static math::vec3 mul3(math::vec3 v, float s) noexcept {
        return vk::math::mul(v, s);
    }

    static math::vec3 rotate_axis_angle(math::vec3 v, math::vec3 axis_unit, float rad) noexcept {
        const float c = std::cos(rad);
        const float s = std::sin(rad);

        const math::vec3 term1 = mul3(v, c);
        const math::vec3 term2 = mul3(vk::math::cross(axis_unit, v), s);
        const math::vec3 term3 = mul3(axis_unit, vk::math::dot(axis_unit, v) * (1.0f - c));

        return add3(add3(term1, term2), term3);
    }

    static math::vec3 normalized_or_fallback(math::vec3 v, math::vec3 fallback) noexcept {
        const float len = vk::math::length(v);
        if (len > 1e-8f) return vk::math::mul(v, 1.0f / len);
        return fallback;
    }

    struct Frame {
        math::vec3 right;
        math::vec3 up;
        math::vec3 forward;
        math::vec3 z_axis;
    };

    static Frame make_view_frame(const Convention& conv, math::vec3 forward_world) noexcept {
        forward_world = normalized_or_fallback(forward_world, {0.0f, 0.0f, -1.0f, 0.0f});

        math::vec3 up_hint = axis_dir_to_vec3(conv.world_up);

        if (conv.handedness == Handedness::Right) {
            math::vec3 right = vk::math::cross(forward_world, up_hint);
            right            = normalized_or_fallback(right, {1.0f, 0.0f, 0.0f, 0.0f});
            math::vec3 up    = vk::math::cross(right, forward_world);
            up               = normalized_or_fallback(up, up_hint);

            const math::vec3 z_axis = (conv.view_forward.sign == Sign::Negative) ? vk::math::mul(forward_world, -1.0f) : forward_world;

            return {right, up, forward_world, z_axis};
        } else {
            math::vec3 right = vk::math::cross(up_hint, forward_world);
            right            = normalized_or_fallback(right, {1.0f, 0.0f, 0.0f, 0.0f});
            math::vec3 up    = vk::math::cross(forward_world, right);
            up               = normalized_or_fallback(up, up_hint);

            const math::vec3 z_axis = (conv.view_forward.sign == Sign::Negative) ? vk::math::mul(forward_world, -1.0f) : forward_world;

            return {right, up, forward_world, z_axis};
        }
    }

    static math::mat4 make_w2c(math::vec3 eye, const Frame& f) noexcept {
        math::mat4 m{};

        m.c0 = {f.right.x, f.up.x, f.z_axis.x, 0.0f};
        m.c1 = {f.right.y, f.up.y, f.z_axis.y, 0.0f};
        m.c2 = {f.right.z, f.up.z, f.z_axis.z, 0.0f};

        const float tx = -vk::math::dot(f.right, eye);
        const float ty = -vk::math::dot(f.up, eye);
        const float tz = -vk::math::dot(f.z_axis, eye);

        m.c3 = {tx, ty, tz, 1.0f};

        return m;
    }

    static math::mat4 make_c2w(math::vec3 eye, const Frame& f) noexcept {
        math::mat4 m{};

        m.c0 = {f.right.x, f.right.y, f.right.z, 0.0f};
        m.c1 = {f.up.x, f.up.y, f.up.z, 0.0f};
        m.c2 = {f.z_axis.x, f.z_axis.y, f.z_axis.z, 0.0f};
        m.c3 = {eye.x, eye.y, eye.z, 1.0f};

        return m;
    }

    void Camera::set_config(const CameraConfig& cfg) noexcept {
        cfg_ = cfg;
        update_projection(vw_, vh_);
        recompute_matrices();
    }

    void Camera::set_state(const CameraState& st) noexcept {
        st_                 = st;
        st_.orbit.pitch_rad = clamp_pitch(st_.orbit.pitch_rad);
        st_.fly.pitch_rad   = clamp_pitch(st_.fly.pitch_rad);
        recompute_matrices();
    }

    void Camera::set_mode(Mode m) noexcept {
        st_.mode = m;
        recompute_matrices();
    }

    void Camera::set_projection(Projection p) noexcept {
        cfg_.projection = p;
        update_projection(vw_, vh_);
        recompute_matrices();
    }

    void Camera::set_convention(const Convention& c) noexcept {
        cfg_.convention = c;
        recompute_matrices();
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
        recompute_matrices();
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

    void Camera::update(float dt_sec, std::uint32_t viewport_w, std::uint32_t viewport_h, const CameraInput& input) noexcept {
        vw_ = viewport_w ? viewport_w : 1;
        vh_ = viewport_h ? viewport_h : 1;

        update_projection(vw_, vh_);

        if (st_.mode == Mode::Orbit) {
            update_orbit(dt_sec, input, vw_, vh_);
        } else {
            update_fly(dt_sec, input);
        }

        recompute_matrices();
    }

    void Camera::update_projection(std::uint32_t w, std::uint32_t h) noexcept {
        const float aspect = float(w) / float(h ? h : 1);

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

    void Camera::update_orbit(float, const CameraInput& in, std::uint32_t vw, std::uint32_t vh) noexcept {
        const bool houdini = in.alt || in.space;

        if (houdini && in.lmb) {
            st_.orbit.yaw_rad += in.mouse_dx * cfg_.orbit_rotate_sens;
            st_.orbit.pitch_rad += in.mouse_dy * cfg_.orbit_rotate_sens;
            st_.orbit.pitch_rad = clamp_pitch(st_.orbit.pitch_rad);
        }

        if (houdini && in.mmb) {
            const float base = (cfg_.projection == Projection::Orthographic) ? std::max(1e-4f, cfg_.ortho_height) : std::max(1e-4f, st_.orbit.distance);

            const float pan = base * cfg_.orbit_pan_sens;

            const float sx  = float(vw);
            const float sy  = float(vh);
            const float ndx = (sx > 0.0f) ? (in.mouse_dx / sx) : 0.0f;
            const float ndy = (sy > 0.0f) ? (in.mouse_dy / sy) : 0.0f;

            const math::vec3 up = axis_dir_to_vec3(cfg_.convention.world_up);

            const math::vec3 base_forward = axis_dir_to_vec3(cfg_.convention.view_forward);
            const math::vec3 base_look    = (cfg_.convention.view_forward.sign == Sign::Negative) ? vk::math::mul(base_forward, -1.0f) : base_forward;

            math::vec3 look  = rotate_axis_angle(base_look, up, st_.orbit.yaw_rad);
            math::vec3 right = (cfg_.convention.handedness == Handedness::Right) ? vk::math::cross(look, up) : vk::math::cross(up, look);
            right            = normalized_or_fallback(right, {1.0f, 0.0f, 0.0f, 0.0f});

            st_.orbit.target = sub3(st_.orbit.target, mul3(right, ndx * pan * 2.0f));
            st_.orbit.target = add3(st_.orbit.target, mul3(up, ndy * pan * 2.0f));
        }

        if ((houdini && in.rmb) || (in.scroll != 0.0f)) {
            const float s = in.scroll + (houdini && in.rmb ? (-in.mouse_dy * 0.01f) : 0.0f);
            if (s != 0.0f) {
                const float factor = std::exp(-s * cfg_.orbit_zoom_sens);
                if (cfg_.projection == Projection::Perspective) {
                    st_.orbit.distance = clampf(st_.orbit.distance * factor, 1e-4f, 1e6f);
                } else {
                    cfg_.ortho_height = clampf(cfg_.ortho_height * factor, 1e-4f, 1e6f);
                }
            }
        }
    }

    void Camera::update_fly(float dt, const CameraInput& in) noexcept {
        if (in.rmb) {
            st_.fly.yaw_rad += in.mouse_dx * cfg_.fly_look_sens;
            st_.fly.pitch_rad += in.mouse_dy * cfg_.fly_look_sens;
            st_.fly.pitch_rad = clamp_pitch(st_.fly.pitch_rad);
        }

        float speed = cfg_.fly_speed;
        if (in.shift) speed *= cfg_.fly_shift_mul;
        if (in.ctrl) speed *= cfg_.fly_ctrl_mul;

        const float move = speed * dt;

        const math::vec3 up = axis_dir_to_vec3(cfg_.convention.world_up);

        const math::vec3 base_forward = axis_dir_to_vec3(cfg_.convention.view_forward);
        const math::vec3 base_look    = (cfg_.convention.view_forward.sign == Sign::Negative) ? vk::math::mul(base_forward, -1.0f) : base_forward;

        math::vec3 look = rotate_axis_angle(base_look, up, st_.fly.yaw_rad);

        math::vec3 right = (cfg_.convention.handedness == Handedness::Right) ? vk::math::cross(look, up) : vk::math::cross(up, look);
        right            = normalized_or_fallback(right, {1.0f, 0.0f, 0.0f, 0.0f});

        look = rotate_axis_angle(look, right, st_.fly.pitch_rad);
        look = normalized_or_fallback(look, base_look);

        math::vec3 final_right = (cfg_.convention.handedness == Handedness::Right) ? vk::math::cross(look, up) : vk::math::cross(up, look);
        final_right            = normalized_or_fallback(final_right, right);

        math::vec3 final_up = (cfg_.convention.handedness == Handedness::Right) ? vk::math::cross(final_right, look) : vk::math::cross(look, final_right);
        final_up            = normalized_or_fallback(final_up, up);

        if (in.forward) st_.fly.eye = add3(st_.fly.eye, mul3(look, move));
        if (in.backward) st_.fly.eye = sub3(st_.fly.eye, mul3(look, move));
        if (in.right) st_.fly.eye = add3(st_.fly.eye, mul3(final_right, move));
        if (in.left) st_.fly.eye = sub3(st_.fly.eye, mul3(final_right, move));
        if (in.up) st_.fly.eye = add3(st_.fly.eye, mul3(final_up, move));
        if (in.down) st_.fly.eye = sub3(st_.fly.eye, mul3(final_up, move));
    }

    void Camera::recompute_matrices() noexcept {
        const math::vec3 up = axis_dir_to_vec3(cfg_.convention.world_up);

        math::vec3 eye{};
        math::vec3 forward_world{};

        if (st_.mode == Mode::Orbit) {
            const math::vec3 base_forward = axis_dir_to_vec3(cfg_.convention.view_forward);
            const math::vec3 base_look    = (cfg_.convention.view_forward.sign == Sign::Negative) ? vk::math::mul(base_forward, -1.0f) : base_forward;

            math::vec3 look = rotate_axis_angle(base_look, up, st_.orbit.yaw_rad);

            math::vec3 right = (cfg_.convention.handedness == Handedness::Right) ? vk::math::cross(look, up) : vk::math::cross(up, look);
            right            = normalized_or_fallback(right, {1.0f, 0.0f, 0.0f, 0.0f});

            look = rotate_axis_angle(look, right, st_.orbit.pitch_rad);
            look = normalized_or_fallback(look, base_look);

            forward_world = look;
            eye           = sub3(st_.orbit.target, mul3(forward_world, st_.orbit.distance));

            m_.eye = eye;
        } else {
            const math::vec3 base_forward = axis_dir_to_vec3(cfg_.convention.view_forward);
            const math::vec3 base_look    = (cfg_.convention.view_forward.sign == Sign::Negative) ? vk::math::mul(base_forward, -1.0f) : base_forward;

            math::vec3 look = rotate_axis_angle(base_look, up, st_.fly.yaw_rad);

            math::vec3 right = (cfg_.convention.handedness == Handedness::Right) ? vk::math::cross(look, up) : vk::math::cross(up, look);
            right            = normalized_or_fallback(right, {1.0f, 0.0f, 0.0f, 0.0f});

            look = rotate_axis_angle(look, right, st_.fly.pitch_rad);
            look = normalized_or_fallback(look, base_look);

            forward_world = look;
            eye           = st_.fly.eye;

            m_.eye = eye;
        }

        const Frame f = make_view_frame(cfg_.convention, forward_world);

        m_.right   = f.right;
        m_.up      = f.up;
        m_.forward = f.forward;

        m_.w2c = make_w2c(eye, f);
        m_.c2w = make_c2w(eye, f);

        m_.view_proj = vk::math::mul(m_.proj, m_.w2c);
    }

} // namespace vk::camera
