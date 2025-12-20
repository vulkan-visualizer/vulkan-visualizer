#include <GLFW/glfw3.h>
#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

import vk.context;
import vk.swapchain;
import vk.frame;
import vk.pipeline;
import vk.memory;
import vk.geometry;
import vk.math;
import vk.imgui;
import vk.camera;

import std;

using namespace vk;
using namespace vk::context;
using namespace vk::swapchain;
using namespace vk::frame;
using namespace vk::pipeline;
using namespace vk::memory;
using namespace vk::geometry;
using namespace vk::math;

struct UiState {
    bool show_grid   = true;
    bool show_axes   = true;
    bool show_origin = true;
    bool show_ring   = true;
    bool fly_mode    = false;
    bool show_demo   = false;

    float grid_extent = 12.0f;
    float grid_step   = 1.0f;
    int major_every   = 5;

    float axis_length  = 4.0f;
    float origin_scale = 0.25f;

    float ring_radius   = 2.5f;
    int ring_segments   = 64;
};

struct InputState {
    bool lmb = false;
    bool mmb = false;
    bool rmb = false;

    bool keys[GLFW_KEY_LAST + 1]{};

    double last_x  = 0.0;
    double last_y  = 0.0;
    bool have_last = false;

    float dx     = 0.0f;
    float dy     = 0.0f;
    float scroll = 0.0f;
};

static void glfw_key_cb(GLFWwindow* w, int key, int, int action, int) {
    if (key < 0 || key > GLFW_KEY_LAST) return;
    auto* s = static_cast<InputState*>(glfwGetWindowUserPointer(w));
    if (!s) return;
    if (action == GLFW_PRESS) s->keys[key] = true;
    if (action == GLFW_RELEASE) s->keys[key] = false;
}

static void glfw_mouse_button_cb(GLFWwindow* w, int button, int action, int) {
    auto* s = static_cast<InputState*>(glfwGetWindowUserPointer(w));
    if (!s) return;

    const bool down = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_LEFT) s->lmb = down;
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) s->mmb = down;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) s->rmb = down;

    if (!down) s->have_last = false;
}

static void glfw_cursor_pos_cb(GLFWwindow* w, double x, double y) {
    auto* s = static_cast<InputState*>(glfwGetWindowUserPointer(w));
    if (!s) return;

    if (!s->have_last) {
        s->last_x    = x;
        s->last_y    = y;
        s->have_last = true;
        return;
    }

    s->dx += float(x - s->last_x);
    s->dy += float(y - s->last_y);

    s->last_x = x;
    s->last_y = y;
}

static void glfw_scroll_cb(GLFWwindow* w, double, double yoff) {
    auto* s = static_cast<InputState*>(glfwGetWindowUserPointer(w));
    if (!s) return;
    s->scroll += float(yoff);
}

struct LineMeshCPU {
    std::vector<VertexP3C4> vertices;
    std::vector<std::uint32_t> indices;
};

static void push_line(LineMeshCPU& mesh, const vec3& a, const vec3& b, const vec4& color) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({a, color});
    mesh.vertices.push_back({b, color});
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
}

static vec4 scale_rgb(vec4 c, float s) {
    return {c.x * s, c.y * s, c.z * s, c.w};
}

static LineMeshCPU build_workspace_lines(const UiState& ui) {
    LineMeshCPU mesh{};

    const float extent = std::max(0.1f, ui.grid_extent);
    const float step   = std::max(0.01f, ui.grid_step);
    const int half     = std::max(1, int(std::floor(extent / step)));
    const float half_extent = float(half) * step;

    const vec4 grid_minor{0.18f, 0.18f, 0.19f, 1.0f};
    const vec4 grid_major{0.32f, 0.32f, 0.34f, 1.0f};

    if (ui.show_grid) {
        for (int i = -half; i <= half; ++i) {
            const float pos = float(i) * step;
            const float t = float(std::abs(i)) / float(half);
            const float fade = std::clamp(1.0f - t * 0.6f, 0.35f, 1.0f);
            const bool major = (ui.major_every > 0) && (i % ui.major_every == 0);
            const vec4 col = scale_rgb(major ? grid_major : grid_minor, fade);

            push_line(mesh, vec3{pos, 0.0f, -half_extent, 0.0f}, vec3{pos, 0.0f, half_extent, 0.0f}, col);
            push_line(mesh, vec3{-half_extent, 0.0f, pos, 0.0f}, vec3{half_extent, 0.0f, pos, 0.0f}, col);
        }
    }

    if (ui.show_axes) {
        const float len = std::max(step * 2.0f, ui.axis_length);

        const vec4 x_pos{0.90f, 0.15f, 0.15f, 1.0f};
        const vec4 x_neg{0.35f, 0.08f, 0.08f, 1.0f};
        const vec4 y_pos{0.15f, 0.90f, 0.15f, 1.0f};
        const vec4 y_neg{0.08f, 0.35f, 0.08f, 1.0f};
        const vec4 z_pos{0.20f, 0.40f, 0.95f, 1.0f};
        const vec4 z_neg{0.08f, 0.18f, 0.40f, 1.0f};

        push_line(mesh, vec3{0.0f, 0.0f, 0.0f, 0.0f}, vec3{len, 0.0f, 0.0f, 0.0f}, x_pos);
        push_line(mesh, vec3{0.0f, 0.0f, 0.0f, 0.0f}, vec3{-len, 0.0f, 0.0f, 0.0f}, x_neg);

        push_line(mesh, vec3{0.0f, 0.0f, 0.0f, 0.0f}, vec3{0.0f, len, 0.0f, 0.0f}, y_pos);
        push_line(mesh, vec3{0.0f, 0.0f, 0.0f, 0.0f}, vec3{0.0f, -len, 0.0f, 0.0f}, y_neg);

        push_line(mesh, vec3{0.0f, 0.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, len, 0.0f}, z_pos);
        push_line(mesh, vec3{0.0f, 0.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, -len, 0.0f}, z_neg);
    }

    if (ui.show_origin) {
        const float o = std::max(0.02f, ui.origin_scale);
        const vec4 white{0.9f, 0.9f, 0.9f, 1.0f};

        push_line(mesh, vec3{-o, 0.0f, -o, 0.0f}, vec3{o, 0.0f, o, 0.0f}, white);
        push_line(mesh, vec3{-o, 0.0f, o, 0.0f}, vec3{o, 0.0f, -o, 0.0f}, white);
        push_line(mesh, vec3{-o, 0.0f, -o, 0.0f}, vec3{o, 0.0f, -o, 0.0f}, white);
        push_line(mesh, vec3{o, 0.0f, -o, 0.0f}, vec3{o, 0.0f, o, 0.0f}, white);
        push_line(mesh, vec3{o, 0.0f, o, 0.0f}, vec3{-o, 0.0f, o, 0.0f}, white);
        push_line(mesh, vec3{-o, 0.0f, o, 0.0f}, vec3{-o, 0.0f, -o, 0.0f}, white);
    }

    if (ui.show_ring) {
        const int segs = std::clamp(ui.ring_segments, 8, 256);
        const float r  = std::max(0.05f, ui.ring_radius);
        const vec4 ring_col{0.28f, 0.28f, 0.30f, 1.0f};

        for (int i = 0; i < segs; ++i) {
            const float a0 = (float(i) / float(segs)) * std::numbers::pi_v<float> * 2.0f;
            const float a1 = (float(i + 1) / float(segs)) * std::numbers::pi_v<float> * 2.0f;
            const vec3 p0{std::cos(a0) * r, 0.0f, std::sin(a0) * r, 0.0f};
            const vec3 p1{std::cos(a1) * r, 0.0f, std::sin(a1) * r, 0.0f};
            push_line(mesh, p0, p1, ring_col);
        }
    }

    return mesh;
}

static MeshGPU upload_lines(const VulkanContext& vkctx, const LineMeshCPU& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return {};

    MeshCPU<VertexP3C4> cpu{};
    cpu.vertices = mesh.vertices;
    cpu.indices  = mesh.indices;
    return upload_mesh(vkctx.physical_device, vkctx.device, vkctx.command_pool, vkctx.graphics_queue, cpu);
}

static GraphicsPipeline create_line_pipeline(const VulkanContext& vkctx, const Swapchain& sc) {
    const auto vin = make_vertex_input<VertexP3C4>();
    const auto spv = read_file_bytes("../shaders/workspace_lines.spv");
    auto shader    = load_shader_module(vkctx.device, spv);

    GraphicsPipelineDesc desc{};
    desc.color_format         = sc.format;
    desc.depth_format         = sc.depth_format;
    desc.use_depth            = true;
    desc.cull                 = CullModeFlagBits::eNone;
    desc.front_face           = FrontFace::eCounterClockwise;
    desc.polygon_mode         = PolygonMode::eLine;
    desc.topology             = PrimitiveTopology::eLineList;
    desc.push_constant_bytes  = sizeof(mat4);
    desc.push_constant_stages = ShaderStageFlagBits::eVertex;

    return create_graphics_pipeline(vkctx.device, vin, desc, shader, "vertMain", "fragMain");
}

static void record_commands(const VulkanContext& vkctx, const Swapchain& sc, const GraphicsPipeline& pipe, const MeshGPU& mesh, FrameSystem& frames, uint32_t frame_index, uint32_t image_index, const mat4& mvp, vk::imgui::ImGuiSystem& imgui_sys) {
    (void) vkctx;

    auto& cmd = frame::cmd(frames, frame_index);

    {
        const ImageMemoryBarrier2 barrier{
            .srcStageMask     = PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask     = PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask    = AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout        = frames.swapchain_image_layout[image_index],
            .newLayout        = ImageLayout::eColorAttachmentOptimal,
            .image            = sc.images[image_index],
            .subresourceRange = {ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };

        const DependencyInfo dep{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers    = &barrier,
        };

        cmd.pipelineBarrier2(dep);
        frames.swapchain_image_layout[image_index] = ImageLayout::eColorAttachmentOptimal;
    }

    {
        const ImageMemoryBarrier2 barrier{
            .srcStageMask     = PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask     = PipelineStageFlagBits2::eEarlyFragmentTests,
            .dstAccessMask    = AccessFlagBits2::eDepthStencilAttachmentWrite,
            .oldLayout        = sc.depth_layout,
            .newLayout        = ImageLayout::eDepthStencilAttachmentOptimal,
            .image            = *sc.depth_image,
            .subresourceRange = {ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
        };

        const DependencyInfo dep{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers    = &barrier,
        };

        cmd.pipelineBarrier2(dep);
        const_cast<Swapchain&>(sc).depth_layout = ImageLayout::eDepthStencilAttachmentOptimal;
    }

    ClearValue clear_color{};
    clear_color.color = ClearColorValue{std::array<float, 4>{0.06f, 0.06f, 0.08f, 1.0f}};

    ClearValue clear_depth{};
    clear_depth.depthStencil = ClearDepthStencilValue{1.0f, 0};

    const RenderingAttachmentInfo color{
        .imageView   = *sc.image_views[image_index],
        .imageLayout = ImageLayout::eColorAttachmentOptimal,
        .loadOp      = AttachmentLoadOp::eClear,
        .storeOp     = AttachmentStoreOp::eStore,
        .clearValue  = clear_color,
    };

    const RenderingAttachmentInfo depth{
        .imageView   = *sc.depth_view,
        .imageLayout = ImageLayout::eDepthStencilAttachmentOptimal,
        .loadOp      = AttachmentLoadOp::eClear,
        .storeOp     = AttachmentStoreOp::eStore,
        .clearValue  = clear_depth,
    };

    const RenderingInfo rendering{
        .renderArea           = {{0, 0}, sc.extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color,
        .pDepthAttachment     = &depth,
    };

    cmd.beginRendering(rendering);
    cmd.bindPipeline(PipelineBindPoint::eGraphics, *pipe.pipeline);
    cmd.pushConstants(*pipe.layout, ShaderStageFlagBits::eVertex, 0, ArrayProxy<const mat4>{mvp});

    const Viewport vp{
        .x        = 0.f,
        .y        = float(sc.extent.height),
        .width    = float(sc.extent.width),
        .height   = -float(sc.extent.height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };

    const Rect2D scissor{{0, 0}, sc.extent};

    cmd.setViewport(0, {vp});
    cmd.setScissor(0, {scissor});

    if (mesh.index_count > 0) {
        DeviceSize offset = 0;
        cmd.bindVertexBuffers(0, {*mesh.vertex_buffer.buffer}, {offset});
        cmd.bindIndexBuffer(*mesh.index_buffer.buffer, 0, IndexType::eUint32);
        cmd.drawIndexed(mesh.index_count, 1, 0, 0, 0);
    }

    cmd.endRendering();

    vk::imgui::render(imgui_sys, cmd, sc.extent, *sc.image_views[image_index], ImageLayout::eColorAttachmentOptimal);

    {
        const ImageMemoryBarrier2 barrier{
            .srcStageMask     = PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask    = AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask     = PipelineStageFlagBits2::eBottomOfPipe,
            .oldLayout        = ImageLayout::eColorAttachmentOptimal,
            .newLayout        = ImageLayout::ePresentSrcKHR,
            .image            = sc.images[image_index],
            .subresourceRange = {ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };

        const DependencyInfo dep{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers    = &barrier,
        };

        cmd.pipelineBarrier2(dep);
        frames.swapchain_image_layout[image_index] = ImageLayout::ePresentSrcKHR;
    }
}

int main() {
    auto [vkctx, surface] = setup_vk_context_glfw("Workspace", "Engine");
    Swapchain sc          = setup_swapchain(vkctx, surface);

    UiState ui{};
    InputState in_state{};

    glfwSetWindowUserPointer(surface.window.get(), &in_state);
    glfwSetKeyCallback(surface.window.get(), &glfw_key_cb);
    glfwSetMouseButtonCallback(surface.window.get(), &glfw_mouse_button_cb);
    glfwSetCursorPosCallback(surface.window.get(), &glfw_cursor_pos_cb);
    glfwSetScrollCallback(surface.window.get(), &glfw_scroll_cb);

    FrameSystem frames = create_frame_system(vkctx, sc, 2);
    auto imgui_sys = vk::imgui::create(vkctx, surface.window.get(), sc.format, 2, uint32_t(sc.images.size()), true, true);

    vk::camera::Camera cam{};
    vk::camera::CameraConfig cam_cfg{};
    cam.set_config(cam_cfg);
    cam.home();
    cam.set_mode(vk::camera::Mode::Orbit);

    {
        auto st = cam.state();
        st.orbit.distance = 14.0f;
        cam.set_state(st);
    }

    LineMeshCPU mesh_cpu = build_workspace_lines(ui);
    MeshGPU mesh_gpu = upload_lines(vkctx, mesh_cpu);
    GraphicsPipeline pipe = create_line_pipeline(vkctx, sc);

    using clock = std::chrono::steady_clock;
    auto t_prev = clock::now();

    uint32_t frame_index = 0;

    while (!glfwWindowShouldClose(surface.window.get())) {
        glfwPollEvents();

        auto t_now = clock::now();
        float dt   = std::chrono::duration<float>(t_now - t_prev).count();
        t_prev     = t_now;
        if (!(dt > 0.0f)) dt = 1.0f / 60.0f;
        dt = std::min(dt, 0.05f);

        const auto ar = begin_frame(vkctx, sc, frames, frame_index);
        if (!ar.ok || ar.need_recreate) {
            recreate_swapchain(vkctx, surface, sc);
            on_swapchain_recreated(vkctx, sc, frames);
            vk::imgui::set_min_image_count(imgui_sys, 2);
            vkctx.device.waitIdle();
            pipe = create_line_pipeline(vkctx, sc);
            continue;
        }

        begin_commands(frames, frame_index);
        vk::imgui::begin_frame();

        bool rebuild_mesh = false;

        ImGui::Begin("Workspace");
        rebuild_mesh |= ImGui::Checkbox("Show grid", &ui.show_grid);
        rebuild_mesh |= ImGui::Checkbox("Show axes", &ui.show_axes);
        rebuild_mesh |= ImGui::Checkbox("Show origin", &ui.show_origin);
        rebuild_mesh |= ImGui::Checkbox("Show ring", &ui.show_ring);
        ImGui::Separator();
        rebuild_mesh |= ImGui::SliderFloat("Grid extent", &ui.grid_extent, 2.0f, 100.0f);
        rebuild_mesh |= ImGui::SliderFloat("Grid step", &ui.grid_step, 0.1f, 5.0f);
        rebuild_mesh |= ImGui::SliderInt("Major every", &ui.major_every, 1, 20);
        rebuild_mesh |= ImGui::SliderFloat("Axis length", &ui.axis_length, 0.5f, 20.0f);
        rebuild_mesh |= ImGui::SliderFloat("Origin scale", &ui.origin_scale, 0.05f, 2.0f);
        rebuild_mesh |= ImGui::SliderFloat("Ring radius", &ui.ring_radius, 0.1f, 20.0f);
        rebuild_mesh |= ImGui::SliderInt("Ring segments", &ui.ring_segments, 8, 128);
        ImGui::Separator();
        ImGui::Checkbox("Fly mode", &ui.fly_mode);
        ImGui::TextUnformatted("Orbit: Alt/Space + LMB rotate, MMB pan, wheel zoom");
        ImGui::TextUnformatted("Fly: RMB look + WASD move, Q/E down/up");
        ImGui::Checkbox("ImGui demo", &ui.show_demo);
        ImGui::End();

        if (ui.show_demo) ImGui::ShowDemoWindow(&ui.show_demo);

        if (rebuild_mesh) {
            vkctx.device.waitIdle();
            mesh_cpu = build_workspace_lines(ui);
            mesh_gpu = upload_lines(vkctx, mesh_cpu);
        }

        cam.set_mode(ui.fly_mode ? vk::camera::Mode::Fly : vk::camera::Mode::Orbit);

        const ImGuiIO& io      = ImGui::GetIO();
        const bool block_mouse = io.WantCaptureMouse;
        const bool block_kbd   = io.WantCaptureKeyboard;

        vk::camera::CameraInput ci{};
        ci.lmb = (!block_mouse) && in_state.lmb;
        ci.mmb = (!block_mouse) && in_state.mmb;
        ci.rmb = (!block_mouse) && in_state.rmb;

        ci.mouse_dx = (!block_mouse) ? in_state.dx : 0.0f;
        ci.mouse_dy = (!block_mouse) ? in_state.dy : 0.0f;
        ci.scroll   = (!block_mouse) ? in_state.scroll : 0.0f;

        ci.shift = (!block_kbd) && (in_state.keys[GLFW_KEY_LEFT_SHIFT] || in_state.keys[GLFW_KEY_RIGHT_SHIFT]);
        ci.ctrl  = (!block_kbd) && (in_state.keys[GLFW_KEY_LEFT_CONTROL] || in_state.keys[GLFW_KEY_RIGHT_CONTROL]);
        ci.alt   = (!block_kbd) && (in_state.keys[GLFW_KEY_LEFT_ALT] || in_state.keys[GLFW_KEY_RIGHT_ALT]);
        ci.space = (!block_kbd) && (in_state.keys[GLFW_KEY_SPACE]);

        ci.forward  = (!block_kbd) && in_state.keys[GLFW_KEY_W];
        ci.backward = (!block_kbd) && in_state.keys[GLFW_KEY_S];
        ci.left     = (!block_kbd) && in_state.keys[GLFW_KEY_A];
        ci.right    = (!block_kbd) && in_state.keys[GLFW_KEY_D];
        ci.down     = (!block_kbd) && in_state.keys[GLFW_KEY_Q];
        ci.up       = (!block_kbd) && in_state.keys[GLFW_KEY_E];

        cam.update(dt, sc.extent.width, sc.extent.height, ci);

        vk::imgui::draw_mini_axis_gizmo(cam.matrices().c2w);

        in_state.dx     = 0.0f;
        in_state.dy     = 0.0f;
        in_state.scroll = 0.0f;

        const mat4 mvp = cam.matrices().view_proj;

        record_commands(vkctx, sc, pipe, mesh_gpu, frames, frame_index, ar.image_index, mvp, imgui_sys);

        const bool need_recreate = end_frame(vkctx, sc, frames, frame_index, ar.image_index);
        if (need_recreate) {
            recreate_swapchain(vkctx, surface, sc);
            on_swapchain_recreated(vkctx, sc, frames);
            vk::imgui::set_min_image_count(imgui_sys, 2);
            vkctx.device.waitIdle();
            pipe = create_line_pipeline(vkctx, sc);
        }

        frame_index = (frame_index + 1) % frames.frames_in_flight;
    }

    vkctx.device.waitIdle();
    vk::imgui::shutdown(imgui_sys);
    return 0;
}
