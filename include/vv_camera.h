#ifndef VULKAN_VISUALIZER_VV_CAMERA_H
#define VULKAN_VISUALIZER_VV_CAMERA_H

#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <array>

struct EngineContext;
struct FrameContext;

namespace vv {

struct float3 { float x{}, y{}, z{}; };
struct float2 { float x{}, y{}; };
struct float4x4 { std::array<float, 16> m{}; };

float3 make_float3(float x, float y, float z);
float3 operator+(const float3& a, const float3& b);
float3 operator-(const float3& a, const float3& b);
float3 operator*(const float3& a, float s);
float3 operator/(const float3& a, float s);
float  dot(const float3& a, const float3& b);
float3 cross(const float3& a, const float3& b);
float  length(const float3& a);
float3 normalize(const float3& a);

float4x4 make_identity();
float4x4 mul(const float4x4& a, const float4x4& b);
float4x4 make_look_at(const float3& eye, const float3& center, const float3& up);
float4x4 make_perspective(float fovy_rad, float aspect, float znear, float zfar);
float4x4 make_ortho(float left, float right, float bottom, float top, float znear, float zfar);

bool project_to_screen(const float3& p_world, const float4x4& view, const float4x4& proj, int screen_w, int screen_h, float& out_x, float& out_y);

enum class CameraProjection : uint8_t { Perspective, Orthographic };
enum class CameraMode : uint8_t { Orbit, Fly };

struct BoundingBox { float3 min{0,0,0}; float3 max{0,0,0}; bool valid{false}; };

struct CameraState {
    CameraMode mode{CameraMode::Orbit};
    CameraProjection projection{CameraProjection::Perspective};
    float units_per_meter{1.0f};
    float3 target{0,0,0};
    float  distance{5.0f};
    float  yaw_deg{0.0f};
    float  pitch_deg{20.0f};
    float3 eye{0,0,5};
    float  fly_yaw_deg{-90.0f};
    float  fly_pitch_deg{0.0f};
    float  fov_y_deg{50.0f};
    float  ortho_height{5.0f};
    float  znear{0.01f};
    float  zfar{1000.0f};
};

struct CameraBookmarksEntry { std::string name; CameraState state; };

class CameraService {
public:
    CameraService();
    void update(double dt_sec, int viewport_w, int viewport_h);
    void handle_event(const SDL_Event& e, const EngineContext* eng = nullptr, const FrameContext* frm = nullptr);
    [[nodiscard]] float4x4 view_matrix() const;
    [[nodiscard]] float4x4 proj_matrix() const;
    [[nodiscard]] float3   eye_position() const;
    void set_state(const CameraState& s);
    [[nodiscard]] const CameraState& state() const { return state_; }
    void set_mode(CameraMode m);
    void set_projection(CameraProjection p);
    void fit(const BoundingBox& bbox, float frame_padding = 1.05f);
    void set_units_per_meter(float upm);
    void add_bookmark(std::string name);
    bool remove_bookmark(std::string_view name);
    [[nodiscard]] const std::vector<CameraBookmarksEntry>& bookmarks() const { return bookmarks_; }
    bool recall_bookmark(std::string_view name, bool animate = true, float duration_sec = 0.6f);
    bool is_animating() const { return anim_active_; }
    bool save_to_file(const std::string& path) const;
    bool load_from_file(const std::string& path);
    void imgui_panel(bool* p_open = nullptr);
    void imgui_panel_contents();
    void imgui_draw_mini_axis_gizmo(int margin_px = 12, int size_px = 96, float thickness = 2.0f) const;
    void imgui_draw_nav_overlay_space_tint(uint32_t rgba = 0) const;
    void set_scene_bounds(const BoundingBox& bbox) { scene_bounds_ = bbox; }
    void home_view();
    void frame_scene(float padding = 1.1f);

private:
    void recompute_cached_();
    void begin_orbit_(int mx, int my);
    void update_orbit_drag_(int mx, int my, bool pan);
    void end_orbit_();
    void begin_fly_(int mx, int my, const EngineContext* eng);
    void update_fly_look_(int dx, int dy);
    void end_fly_(const EngineContext* eng);
    void apply_inertia_(double dt);
    void start_animation_to_(const CameraState& dst, float duration_sec);

private:
    CameraState state_{};
    float4x4 view_{};
    float4x4 proj_{};
    int vp_w_{1}, vp_h_{1};
    bool rmb_{false}, mmb_{false}, lmb_{false};
    bool key_w_{false}, key_a_{false}, key_s_{false}, key_d_{false}, key_q_{false}, key_e_{false}, key_shift_{false}, key_ctrl_{false};
    bool key_space_{false}, key_alt_{false};
    bool nav_orbiting_{false}, nav_panning_{false}, nav_dollying_{false};
    int last_mx_{0}, last_my_{0};
    bool fly_capturing_{false};
    float yaw_vel_{0}, pitch_vel_{0};
    float pan_x_vel_{0}, pan_y_vel_{0};
    float zoom_vel_{0};
    std::vector<CameraBookmarksEntry> bookmarks_{};
    bool  anim_active_{false};
    float anim_t_{0.0f};
    float anim_dur_{0.0f};
    CameraState anim_from_{};
    CameraState anim_to_{};
    BoundingBox scene_bounds_{};
};

}

#endif
