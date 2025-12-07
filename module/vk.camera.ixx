module;
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
export module vk.camera;

namespace vk::camera {
    // ============================================================================
    // Math Types
    // ============================================================================

    export struct Vec3 {
        float x{}, y{}, z{};

        constexpr Vec3() = default;
        constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

        constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
        constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
        constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
        constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
        constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
        constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
        constexpr Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

        [[nodiscard]] constexpr float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
        [[nodiscard]] constexpr Vec3 cross(const Vec3& o) const {
            return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
        }
        [[nodiscard]] float length() const;
        [[nodiscard]] Vec3 normalized() const;
    };

    export struct Mat4 {
        std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

        [[nodiscard]] static constexpr Mat4 identity() {
            Mat4 result;
            return result;
        }

        [[nodiscard]] static Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up);
        [[nodiscard]] static Mat4 perspective(float fov_y_rad, float aspect, float znear, float zfar);
        [[nodiscard]] Mat4 operator*(const Mat4& o) const;
    };

    // ============================================================================
    // Camera Types
    // ============================================================================

    export enum class CameraMode : uint8_t { Orbit, Fly };
    export enum class ProjectionMode : uint8_t { Perspective, Orthographic };

    export struct CameraState {
        CameraMode mode{CameraMode::Orbit};
        ProjectionMode projection{ProjectionMode::Perspective};

        // Orbit mode parameters
        Vec3 target{0, 0, 0};
        float distance{5.0f};
        float yaw_deg{-45.0f};
        float pitch_deg{25.0f};

        // Fly mode parameters
        Vec3 eye{0, 0, 5};
        float fly_yaw_deg{-90.0f};
        float fly_pitch_deg{0.0f};

        // Projection parameters
        float fov_y_deg{50.0f};
        float ortho_height{5.0f};
        float znear{0.01f};
        float zfar{1000.0f};
    };

    // ============================================================================
    // Camera Controller
    // ============================================================================

    export class Camera {
    public:
        Camera() = default;

        void update(float dt_sec, int viewport_w, int viewport_h);
        void handle_event(const SDL_Event& event);

        [[nodiscard]] const Mat4& view_matrix() const { return view_; }
        [[nodiscard]] const Mat4& proj_matrix() const { return proj_; }
        [[nodiscard]] const CameraState& state() const { return state_; }
        [[nodiscard]] Vec3 eye_position() const;

        void set_state(const CameraState& s);
        void set_mode(CameraMode m);
        void set_projection(ProjectionMode p);
        void home_view();

    private:
        void recompute_matrices();
        void apply_inertia(float dt);

        CameraState state_{};
        Mat4 view_{};
        Mat4 proj_{};
        int viewport_width_{1};
        int viewport_height_{1};

        // Mouse state
        bool lmb_{false}, mmb_{false}, rmb_{false};
        int last_mx_{0}, last_my_{0};
        bool fly_capturing_{false};

        // Keyboard state
        bool key_w_{false}, key_a_{false}, key_s_{false}, key_d_{false};
        bool key_q_{false}, key_e_{false};
        bool key_shift_{false}, key_ctrl_{false}, key_space_{false}, key_alt_{false};

        // Inertia
        float yaw_vel_{0}, pitch_vel_{0};
        float pan_x_vel_{0}, pan_y_vel_{0};
        float zoom_vel_{0};
    };
} // namespace vk::camera

