export module vk.camera;

import vk.math;
import std;

namespace vk::camera {

    export enum class Mode : std::uint8_t { Orbit, Fly };
    export enum class Projection : std::uint8_t { Perspective, Orthographic };

    export struct CameraConfig {
        Projection projection = Projection::Perspective;

        float fov_y_rad = std::numbers::pi_v<float> / 3.0f;
        float znear     = 0.01f;
        float zfar      = 1000.0f;

        float ortho_height = 5.0f;

        float orbit_rotate_sens = 0.0065f;
        float orbit_pan_sens    = 0.0015f;
        float orbit_zoom_sens   = 0.12f;

        float fly_look_sens = 0.0045f;
        float fly_speed     = 2.0f;
        float fly_shift_mul = 3.5f;
        float fly_ctrl_mul  = 0.25f;
    };

    export struct OrbitState {
        math::vec3 target{0.0f, 0.0f, 0.0f, 0.0f};
        float distance  = 5.0f;
        float yaw_rad   = -0.78539816339f;
        float pitch_rad = 0.43633231299f;
    };

    export struct FlyState {
        math::vec3 eye{0.0f, 0.0f, 5.0f, 0.0f};
        float yaw_rad   = -1.57079632679f;
        float pitch_rad = 0.0f;
    };

    export struct CameraState {
        Mode mode = Mode::Orbit;
        OrbitState orbit{};
        FlyState fly{};
    };

    export struct CameraInput {
        bool lmb = false;
        bool mmb = false;
        bool rmb = false;

        float mouse_dx = 0.0f;
        float mouse_dy = 0.0f;
        float scroll   = 0.0f;

        bool forward  = false;
        bool backward = false;
        bool left     = false;
        bool right    = false;
        bool up       = false;
        bool down     = false;

        bool shift = false;
        bool ctrl  = false;
        bool alt   = false;
        bool space = false;
    };

    export struct CameraMatrices {
        math::mat4 view{};
        math::mat4 proj{};
        math::mat4 view_proj{};
        math::vec3 eye{0.0f, 0.0f, 0.0f, 0.0f};
    };

    export class Camera {
    public:
        Camera() = default;

        void set_config(const CameraConfig& cfg) noexcept;
        void set_state(const CameraState& st) noexcept;

        void set_mode(Mode m) noexcept;
        void set_projection(Projection p) noexcept;
        void home() noexcept;

        void update(float dt_sec, std::uint32_t viewport_w, std::uint32_t viewport_h, const CameraInput& input) noexcept;

        [[nodiscard]] const CameraConfig& config() const noexcept;
        [[nodiscard]] const CameraState& state() const noexcept;
        [[nodiscard]] const CameraMatrices& matrices() const noexcept;

    private:
        void update_orbit(float dt, const CameraInput& in, std::uint32_t vw, std::uint32_t vh) noexcept;
        void update_fly(float dt, const CameraInput& in) noexcept;
        void update_projection(std::uint32_t w, std::uint32_t h) noexcept;
        void recompute_view() noexcept;
        void recompute_viewproj() noexcept;

        static float clamp_pitch(float x) noexcept;

        CameraConfig cfg_{};
        CameraState st_{};
        CameraMatrices m_{};

        std::uint32_t vw_ = 1;
        std::uint32_t vh_ = 1;
    };

} // namespace vk::camera
