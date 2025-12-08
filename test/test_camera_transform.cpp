#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <imgui.h>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <vector>
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
import vk.engine;
import vk.context;

namespace {
struct ColoredLine {
    vk::context::Vec3 a{};
    vk::context::Vec3 b{};
    ImU32 color{IM_COL32_WHITE};
};

struct Projector {
    vk::context::Mat4 view_proj{};
    float width{1.0f};
    float height{1.0f};

    [[nodiscard]] std::optional<ImVec2> project(const vk::context::Vec3& p) const {
        const auto& m = view_proj.m;
        const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
        const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
        const float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
        const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];

        if (w <= 0.0f) return std::nullopt;
        const float inv_w = 1.0f / w;
        const float ndc_x = x * inv_w;
        const float ndc_y = y * inv_w;
        const float ndc_z = z * inv_w;

        // Simple clip rejection to avoid extreme lines; keep z in front of the camera.
        if (ndc_z < 0.0f || ndc_z > 1.2f) return std::nullopt;
        if (std::abs(ndc_x) > 1.5f || std::abs(ndc_y) > 1.5f) return std::nullopt;

        const float sx = (ndc_x * 0.5f + 0.5f) * width;
        const float sy = (-ndc_y * 0.5f + 0.5f) * height;
        return ImVec2{sx, sy};
    }
};

vk::context::Mat4 build_pose(const vk::context::Vec3& position, const vk::context::Vec3& target, const vk::context::Vec3& world_up) {
    const vk::context::Vec3 forward = (target - position).normalized();
    vk::context::Vec3 right         = forward.cross(world_up).normalized();
    vk::context::Vec3 up            = right.cross(forward).normalized();

    vk::context::Mat4 m{};
    m.m = {right.x,
        right.y,
        right.z,
        0.0f,
        up.x,
        up.y,
        up.z,
        0.0f,
        forward.x,
        forward.y,
        forward.z,
        0.0f,
        position.x,
        position.y,
        position.z,
        1.0f};
    return m;
}

std::vector<vk::context::Mat4> generate_nerf_like_camera_path(const std::size_t count, const float radius, const float height_variation) {
    std::vector<vk::context::Mat4> poses;
    poses.reserve(count);

    constexpr vk::context::Vec3 target{0.0f, 0.0f, 0.0f};
    const float two_pi = 2.0f * std::numbers::pi_v<float>;

    for (std::size_t i = 0; i < count; ++i) {
        const float t        = static_cast<float>(i) / static_cast<float>(count);
        const float theta    = t * two_pi;
        const float wobble   = std::sin(theta * 3.0f) * 0.25f;
        const float distance = radius * (0.8f + 0.15f * std::cos(theta * 0.5f));
        const float height   = height_variation * std::cos(theta * 1.5f) + wobble * 0.35f;

        const vk::context::Vec3 position{distance * std::cos(theta), height, distance * std::sin(theta)};
        poses.push_back(build_pose(position, target, vk::context::Vec3{0.0f, 1.0f, 0.0f}));
    }

    return poses;
}

vk::context::Vec3 extract_position(const vk::context::Mat4& m) {
    return vk::context::Vec3{m.m[12], m.m[13], m.m[14]};
}

std::tuple<vk::context::Vec3, float> compute_center_and_radius(const std::vector<vk::context::Mat4>& poses) {
    vk::context::Vec3 center{0.0f, 0.0f, 0.0f};
    for (const auto& p : poses) {
        center += extract_position(p);
    }
    if (!poses.empty()) {
        center = center / static_cast<float>(poses.size());
    }

    float average_radius = 0.0f;
    for (const auto& p : poses) {
        average_radius += (extract_position(p) - center).length();
    }
    if (!poses.empty()) average_radius /= static_cast<float>(poses.size());

    return {center, average_radius};
}

std::vector<ColoredLine> make_frustum_lines(const std::vector<vk::context::Mat4>& poses, const float near_d, const float far_d, const float fov_deg) {
    std::vector<ColoredLine> lines;
    lines.reserve(poses.size() * 12);

    const float fov_rad = fov_deg * std::numbers::pi_v<float> / 180.0f;
    const float near_h  = std::tan(fov_rad * 0.5f) * near_d;
    const float near_w  = near_h;
    const float far_h   = std::tan(fov_rad * 0.5f) * far_d;
    const float far_w   = far_h;
    const ImU32 edge    = ImColor(0.95f, 0.76f, 0.32f, 1.0f);

    auto add_line = [&](const vk::context::Vec3& a, const vk::context::Vec3& b) {
        lines.push_back(ColoredLine{a, b, edge});
    };

    for (const auto& pose : poses) {
        const vk::context::Vec3 origin  = extract_position(pose);
        const vk::context::Vec3 right   = {pose.m[0], pose.m[1], pose.m[2]};
        const vk::context::Vec3 up      = {pose.m[4], pose.m[5], pose.m[6]};
        const vk::context::Vec3 forward = {pose.m[8], pose.m[9], pose.m[10]};

        const auto corner = [&](const float w, const float h, const float d) {
            return origin + forward * d + right * w + up * h;
        };

        const vk::context::Vec3 nlt = corner(-near_w, near_h, near_d);
        const vk::context::Vec3 nrt = corner(near_w, near_h, near_d);
        const vk::context::Vec3 nlb = corner(-near_w, -near_h, near_d);
        const vk::context::Vec3 nrb = corner(near_w, -near_h, near_d);

        const vk::context::Vec3 flt = corner(-far_w, far_h, far_d);
        const vk::context::Vec3 frt = corner(far_w, far_h, far_d);
        const vk::context::Vec3 flb = corner(-far_w, -far_h, far_d);
        const vk::context::Vec3 frb = corner(far_w, -far_h, far_d);

        add_line(origin, nlt);
        add_line(origin, nrt);
        add_line(origin, nlb);
        add_line(origin, nrb);

        add_line(nlt, nrt);
        add_line(nrt, nrb);
        add_line(nrb, nlb);
        add_line(nlb, nlt);

        add_line(flt, frt);
        add_line(frt, frb);
        add_line(frb, flb);
        add_line(flb, flt);

        add_line(nlt, flt);
        add_line(nrt, frt);
        add_line(nlb, flb);
        add_line(nrb, frb);
    }

    return lines;
}

std::vector<ColoredLine> make_axis_lines(const std::vector<vk::context::Mat4>& poses, const float axis_length) {
    std::vector<ColoredLine> lines;
    lines.reserve(poses.size() * 3);

    for (const auto& pose : poses) {
        const vk::context::Vec3 origin  = extract_position(pose);
        const vk::context::Vec3 right   = {pose.m[0], pose.m[1], pose.m[2]};
        const vk::context::Vec3 up      = {pose.m[4], pose.m[5], pose.m[6]};
        const vk::context::Vec3 forward = {pose.m[8], pose.m[9], pose.m[10]};

        lines.push_back({origin, origin + right * axis_length, ImColor(0.94f, 0.33f, 0.31f, 1.0f)});
        lines.push_back({origin, origin + up * axis_length, ImColor(0.37f, 0.82f, 0.36f, 1.0f)});
        lines.push_back({origin, origin + forward * axis_length, ImColor(0.32f, 0.60f, 1.0f, 1.0f)});
    }

    return lines;
}

std::vector<ColoredLine> make_path_lines(const std::vector<vk::context::Mat4>& poses) {
    std::vector<ColoredLine> lines;
    lines.reserve(poses.size());
    if (poses.size() < 2) return lines;

    const ImU32 color = ImColor(0.7f, 0.72f, 0.78f, 0.8f);
    vk::context::Vec3 prev = extract_position(poses.front());
    for (std::size_t i = 1; i < poses.size(); ++i) {
        const vk::context::Vec3 curr = extract_position(poses[i]);
        lines.push_back(ColoredLine{prev, curr, color});
        prev = curr;
    }
    // Close the loop to emphasize the orbit.
    lines.push_back(ColoredLine{prev, extract_position(poses.front()), color});
    return lines;
}

void draw_lines(const Projector& projector, const std::vector<ColoredLine>& lines, const float thickness) {
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    for (const auto& l : lines) {
        const auto a = projector.project(l.a);
        const auto b = projector.project(l.b);
        if (a && b) {
            draw_list->AddLine(*a, *b, l.color, thickness);
        }
    }
}

void draw_matrix(const vk::context::Mat4& m, const char* label) {
    if (ImGui::TreeNode(label)) {
        for (int row = 0; row < 4; ++row) {
            ImGui::Text("%.3f  %.3f  %.3f  %.3f", m.m[0 + row], m.m[4 + row], m.m[8 + row], m.m[12 + row]);
        }
        ImGui::TreePop();
    }
}
} // namespace

class CameraTransformPlugin {
public:
    explicit CameraTransformPlugin(std::shared_ptr<vk::context::Camera> camera) : camera_(std::move(camera)) {}

    [[nodiscard]] static constexpr vk::context::PluginPhase phases() noexcept {
        using vk::context::PluginPhase;
        return PluginPhase::Setup | PluginPhase::Initialize | PluginPhase::PreRender | PluginPhase::Render | PluginPhase::ImGUI;
    }

    void on_setup(const vk::context::PluginContext& ctx) {
        if (!ctx.caps) return;
        ctx.caps->allow_async_compute        = false;
        ctx.caps->presentation_mode          = vk::context::PresentationMode::EngineBlit;
        ctx.caps->preferred_swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
        ctx.caps->color_samples              = VK_SAMPLE_COUNT_1_BIT;
        ctx.caps->uses_depth                 = VK_FALSE;
        ctx.caps->color_attachments          = {vk::context::AttachmentRequest{.name = "color", .format = VK_FORMAT_B8G8R8A8_UNORM, .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, .samples = VK_SAMPLE_COUNT_1_BIT, .aspect = VK_IMAGE_ASPECT_COLOR_BIT, .initial_layout = VK_IMAGE_LAYOUT_GENERAL}};
        ctx.caps->presentation_attachment    = "color";
    }

    void on_initialize(vk::context::PluginContext&) {
        poses_ = generate_nerf_like_camera_path(24, 4.0f, 0.6f);
        std::tie(center_, average_radius_) = compute_center_and_radius(poses_);

        const float frustum_near = std::max(0.12f, average_radius_ * 0.06f);
        const float frustum_far  = std::max(0.32f, average_radius_ * 0.12f);
        frustum_lines_           = make_frustum_lines(poses_, frustum_near, frustum_far, 45.0f);
        axis_lines_              = make_axis_lines(poses_, std::max(0.2f, average_radius_ * 0.08f));
        path_lines_              = make_path_lines(poses_);

        if (camera_) {
            auto s      = camera_->state();
            s.target    = center_;
            s.distance  = std::max(average_radius_ * 1.8f, 3.5f);
            s.yaw_deg   = -120.0f;
            s.pitch_deg = 22.0f;
            s.fov_y_deg = 55.0f;
            s.znear     = 0.05f;
            s.zfar      = std::max(50.0f, average_radius_ * 6.0f);
            camera_->set_state(s);
        }
    }

    void on_pre_render(vk::context::PluginContext& ctx) {
        if (!camera_) return;
        if (ctx.frame) {
            viewport_width_  = ctx.frame->extent.width;
            viewport_height_ = ctx.frame->extent.height;
        }
        camera_->update(ctx.delta_time, static_cast<int>(viewport_width_), static_cast<int>(viewport_height_));
    }

    void on_render(vk::context::PluginContext& ctx) {
        if (!ctx.cmd || !ctx.frame || ctx.frame->color_attachments.empty()) return;
        const auto& target = ctx.frame->color_attachments.front();

        vk::context::transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        constexpr VkClearValue clear_value{.color = {{0.05f, 0.06f, 0.08f, 1.0f}}};
        const VkRenderingAttachmentInfo color_attachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = target.view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = clear_value};
        const VkRenderingInfo render_info{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, ctx.frame->extent}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color_attachment};
        vkCmdBeginRendering(*ctx.cmd, &render_info);
        vkCmdEndRendering(*ctx.cmd);

        vk::context::transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    static void on_post_render(vk::context::PluginContext&) {}

    void on_imgui(vk::context::PluginContext& ctx) {
        if (!camera_ || !ctx.frame) return;
        camera_->draw_mini_axis_gizmo();
        overlay_scale_ = std::clamp(std::min(ctx.frame->extent.width, ctx.frame->extent.height) / 900.0f, 0.75f, 1.8f);

        const auto view_proj = camera_->proj_matrix() * camera_->view_matrix();
        const Projector projector{view_proj, static_cast<float>(ctx.frame->extent.width), static_cast<float>(ctx.frame->extent.height)};

        if (show_frustums_) draw_lines(projector, frustum_lines_, 1.5f * overlay_scale_);
        if (show_axes_) draw_lines(projector, axis_lines_, 2.2f * overlay_scale_);
        if (show_path_) draw_lines(projector, path_lines_, 1.1f * overlay_scale_);

        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("Camera Transform Debug")) {
            ImGui::TextUnformatted("Nerf-style synthetic poses");
            ImGui::Separator();
            ImGui::Text("Poses: %zu", poses_.size());
            ImGui::Text("Centroid: [%.2f, %.2f, %.2f]", center_.x, center_.y, center_.z);
            ImGui::Text("Average radius: %.2f", average_radius_);
            ImGui::Checkbox("Show frustums", &show_frustums_);
            ImGui::Checkbox("Show axis triads", &show_axes_);
            ImGui::Checkbox("Show path", &show_path_);
            ImGui::SliderFloat("Overlay scale", &overlay_scale_, 0.5f, 2.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

            if (ImGui::CollapsingHeader("Sample transforms", ImGuiTreeNodeFlags_DefaultOpen)) {
                const std::size_t preview_count = std::min<std::size_t>(poses_.size(), 3);
                for (std::size_t i = 0; i < preview_count; ++i) {
                    const std::string label = std::format("pose [{}]", i);
                    draw_matrix(poses_[i], label.c_str());
                }
            }
        }
        ImGui::End();
    }

    static void on_present(vk::context::PluginContext&) {}

    void on_cleanup(vk::context::PluginContext&) {
        frustum_lines_.clear();
        axis_lines_.clear();
        path_lines_.clear();
        poses_.clear();
    }

    void on_event(const SDL_Event& event) {
        if (!camera_) return;
        const auto& io = ImGui::GetIO();
        if (io.WantCaptureMouse || io.WantCaptureKeyboard) return;
        camera_->handle_event(event);
    }

    void on_resize(const uint32_t width, const uint32_t height) {
        viewport_width_  = width;
        viewport_height_ = height;
    }

private:
    std::shared_ptr<vk::context::Camera> camera_{};
    std::vector<vk::context::Mat4> poses_{};
    std::vector<ColoredLine> frustum_lines_{};
    std::vector<ColoredLine> axis_lines_{};
    std::vector<ColoredLine> path_lines_{};
    vk::context::Vec3 center_{0.0f, 0.0f, 0.0f};
    float average_radius_{4.0f};
    float overlay_scale_{1.0f};
    uint32_t viewport_width_{1280};
    uint32_t viewport_height_{720};
    bool show_frustums_{true};
    bool show_axes_{true};
    bool show_path_{true};
};

int main() {
    vk::engine::VulkanEngine engine;
    const auto camera = std::make_shared<vk::context::Camera>();
    CameraTransformPlugin transform_visualizer(camera);

    engine.init(transform_visualizer);
    engine.run(transform_visualizer);
    engine.cleanup();
    return 0;
}
