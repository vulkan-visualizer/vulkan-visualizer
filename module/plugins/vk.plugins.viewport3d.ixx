module;
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
export module vk.plugins.viewport3d;
import vk.context;

namespace vk::plugins {
    enum class CameraMode : uint8_t { Orbit, Fly };
    enum class ProjectionMode : uint8_t { Perspective, Orthographic };

    export struct CameraState {
        CameraMode mode{CameraMode::Orbit};
        ProjectionMode projection{ProjectionMode::Perspective};

        // Orbit mode parameters
        context::Vec3 target{0, 0, 0};
        float distance{5.0f};
        float yaw_deg{-45.0f};
        float pitch_deg{25.0f};

        // Fly mode parameters
        context::Vec3 eye{0, 0, 5};
        float fly_yaw_deg{-90.0f};
        float fly_pitch_deg{0.0f};

        // Projection parameters
        float fov_y_deg{50.0f};
        float ortho_height{5.0f};
        float znear{0.01f};
        float zfar{1000.0f};
    };


    export class Camera {
    public:
        void update(float dt_sec, int viewport_w, int viewport_h);
        void handle_event(const SDL_Event& event);

        [[nodiscard]] const context::Mat4& view_matrix() const {
            return view_;
        }
        [[nodiscard]] const context::Mat4& proj_matrix() const {
            return proj_;
        }
        [[nodiscard]] const CameraState& state() const {
            return state_;
        }
        [[nodiscard]] context::Vec3 eye_position() const;

        void set_state(const CameraState& s);
        void set_mode(CameraMode m);
        void set_projection(ProjectionMode p);
        void home_view();
        void draw_imgui_panel();
        void draw_mini_axis_gizmo();

    private:
        void recompute_matrices();
        void apply_inertia(float dt);
        bool show_camera_panel_{true};

        CameraState state_{};
        context::Mat4 view_{};
        context::Mat4 proj_{};
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


    export class Viewport3D {
    public:
        [[nodiscard]] static constexpr context::PluginPhase phases() noexcept {
            return context::PluginPhase::Setup | context::PluginPhase::Initialize | context::PluginPhase::PreRender | context::PluginPhase::Render | context::PluginPhase::PostRender | context::PluginPhase::ImGUI | context::PluginPhase::Cleanup;
        }
        static void on_setup(const context::PluginContext& ctx);
        static void on_initialize(context::PluginContext&) {}
        void on_pre_render(context::PluginContext& ctx);
        static void on_render(const context::PluginContext& ctx);
        static void on_post_render(context::PluginContext&) {}
        void on_imgui(context::PluginContext& ctx);
        static void on_present(context::PluginContext&) {}
        static void on_cleanup(context::PluginContext&) {}
        void on_event(const SDL_Event& event);
        void on_resize(uint32_t width, uint32_t height);

        Viewport3D()                                 = default;
        ~Viewport3D()                                = default;
        Viewport3D(const Viewport3D&)                = delete;
        Viewport3D& operator=(const Viewport3D&)     = delete;
        Viewport3D(Viewport3D&&) noexcept            = default;
        Viewport3D& operator=(Viewport3D&&) noexcept = default;

        Camera camera_{};

    private:
        bool enabled_{true};
        int viewport_width_{1920};
        int viewport_height_{1280};
        uint64_t last_time_ms_{0};
    };
} // namespace vk::plugins
