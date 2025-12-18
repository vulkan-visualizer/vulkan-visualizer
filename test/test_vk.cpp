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

struct MeshState {
    MeshGPU gpu;
};

struct RenderState {
    raii::ShaderModule shader{nullptr};
    GraphicsPipeline pipeline;
};

struct UiState {
    bool wireframe    = true;
    float angle_speed = 0.25f;
    float radius      = 0.5f;
    int slices        = 32;
    int stacks        = 16;
    bool show_demo    = false;

    bool use_camera = true;
    bool fly_mode   = false;
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

static void glfw_key_cb(GLFWwindow* w, const int key, int, const int action, int) {
    if (key < 0 || key > GLFW_KEY_LAST) return;
    auto* s = static_cast<InputState*>(glfwGetWindowUserPointer(w));
    if (!s) return;
    if (action == GLFW_PRESS) s->keys[key] = true;
    if (action == GLFW_RELEASE) s->keys[key] = false;
}

static void glfw_mouse_button_cb(GLFWwindow* w, const int button, const int action, int) {
    auto* s = static_cast<InputState*>(glfwGetWindowUserPointer(w));
    if (!s) return;

    const bool down = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_LEFT) s->lmb = down;
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) s->mmb = down;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) s->rmb = down;

    if (!down) {
        s->have_last = false;
    }
}

static void glfw_cursor_pos_cb(GLFWwindow* w, const double x, const double y) {
    auto* s = static_cast<InputState*>(glfwGetWindowUserPointer(w));
    if (!s) return;

    if (!s->have_last) {
        s->last_x    = x;
        s->last_y    = y;
        s->have_last = true;
        return;
    }

    s->dx += static_cast<float>(x - s->last_x);
    s->dy += static_cast<float>(y - s->last_y);

    s->last_x = x;
    s->last_y = y;
}

static void glfw_scroll_cb(GLFWwindow* w, double, const double yoff) {
    auto* s = static_cast<InputState*>(glfwGetWindowUserPointer(w));
    if (!s) return;
    s->scroll += static_cast<float>(yoff);
}

static MeshState create_sphere_mesh(const VulkanContext& vkctx, const float radius, const uint32_t slices, const uint32_t stacks, const vec4& color) {
    const auto src = make_sphere<VertexP3C4>(radius, slices, stacks, color);

    MeshCPU<VertexP3C4> cpu{};
    cpu.vertices = src.vertices;
    cpu.indices  = src.indices;

    MeshState out{};
    out.gpu = upload_mesh(vkctx.physical_device, vkctx.device, vkctx.command_pool, vkctx.graphics_queue, cpu);
    return out;
}

static RenderState create_render_state(const VulkanContext& vkctx, const Swapchain& sc, const bool wireframe) {
    const auto vin = make_vertex_input<VertexP3C4>();

    const auto spv = read_file_bytes("../shaders/cube.spv");
    auto shader    = load_shader_module(vkctx.device, spv);

    GraphicsPipelineDesc desc{};
    desc.color_format = sc.format;
    desc.depth_format = sc.depth_format;
    desc.use_depth    = true;

    desc.cull       = CullModeFlagBits::eBack;
    desc.front_face = FrontFace::eCounterClockwise;

    desc.polygon_mode = wireframe ? PolygonMode::eLine : PolygonMode::eFill;

    desc.push_constant_bytes  = sizeof(mat4);
    desc.push_constant_stages = ShaderStageFlagBits::eVertex;

    RenderState out{};
    out.shader   = std::move(shader);
    out.pipeline = create_graphics_pipeline(vkctx.device, vin, desc, out.shader, "vertMain", "fragMain");
    return out;
}
void record_commands(const VulkanContext& vkctx, const Swapchain& sc, const RenderState& render, const MeshState& mesh, FrameSystem& frames, uint32_t frame_index, uint32_t image_index, const mat4& mvp, imgui::ImGuiSystem& imgui_sys) {
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
    clear_color.color = ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 1.0f}};

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

    cmd.bindPipeline(PipelineBindPoint::eGraphics, *render.pipeline.pipeline);
    cmd.pushConstants(*render.pipeline.layout, ShaderStageFlagBits::eVertex, 0, ArrayProxy<const mat4>{mvp});

    const Viewport vp{
        .x        = 0.f,
        .y        = static_cast<float>(sc.extent.height),
        .width    = static_cast<float>(sc.extent.width),
        .height   = -static_cast<float>(sc.extent.height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };

    const Rect2D scissor{{0, 0}, sc.extent};

    cmd.setViewport(0, {vp});
    cmd.setScissor(0, {scissor});

    DeviceSize offset = 0;
    cmd.bindVertexBuffers(0, {*mesh.gpu.vertex_buffer.buffer}, {offset});
    cmd.bindIndexBuffer(*mesh.gpu.index_buffer.buffer, 0, IndexType::eUint32);
    cmd.drawIndexed(mesh.gpu.index_count, 1, 0, 0, 0);

    cmd.endRendering();

    imgui::render(imgui_sys, cmd, sc.extent, *sc.image_views[image_index], ImageLayout::eColorAttachmentOptimal);

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
    auto [vkctx, surface] = setup_vk_context_glfw();
    Swapchain sc          = setup_swapchain(vkctx, surface);

    UiState ui{};
    InputState in_state{};

    glfwSetWindowUserPointer(surface.window.get(), &in_state);
    glfwSetKeyCallback(surface.window.get(), &glfw_key_cb);
    glfwSetMouseButtonCallback(surface.window.get(), &glfw_mouse_button_cb);
    glfwSetCursorPosCallback(surface.window.get(), &glfw_cursor_pos_cb);
    glfwSetScrollCallback(surface.window.get(), &glfw_scroll_cb);

    MeshState mesh   = create_sphere_mesh(vkctx, ui.radius, static_cast<uint32_t>(ui.slices), static_cast<uint32_t>(ui.stacks), vec4{0.9f, 0.9f, 1.0f, 1.0f});
    RenderState rend = create_render_state(vkctx, sc, ui.wireframe);

    FrameSystem frames = create_frame_system(vkctx, sc, 2);

    auto imgui_sys = imgui::create(vkctx, surface.window.get(), sc.format, 2, static_cast<uint32_t>(sc.images.size()), true, true);

    camera::Camera cam{};
    camera::CameraConfig cam_cfg{};
    cam.set_config(cam_cfg);
    cam.home();
    cam.set_mode(camera::Mode::Fly);

    using clock = std::chrono::steady_clock;
    auto t_prev = clock::now();

    uint32_t frame_index = 0;
    float angle          = 0.0f;

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
            rend = create_render_state(vkctx, sc, ui.wireframe);
            continue;
        }

        begin_commands(frames, frame_index);

        imgui::begin_frame();

        bool rebuild_pipeline = false;
        bool rebuild_mesh     = false;

        ImGui::Begin("Scene");
        rebuild_pipeline |= ImGui::Checkbox("Wireframe", &ui.wireframe);
        ImGui::SliderFloat("Angle speed", &ui.angle_speed, 0.0f, 3.0f);
        rebuild_mesh |= ImGui::SliderFloat("Radius", &ui.radius, 0.1f, 2.0f);
        rebuild_mesh |= ImGui::SliderInt("Slices", &ui.slices, 3, 128);
        rebuild_mesh |= ImGui::SliderInt("Stacks", &ui.stacks, 2, 128);
        ImGui::Checkbox("ImGui demo", &ui.show_demo);

        ImGui::Separator();
        ImGui::Checkbox("Enable camera", &ui.use_camera);
        ImGui::Checkbox("Fly mode", &ui.fly_mode);
        ImGui::TextUnformatted("Fly: RMB look + WASD move, Q/E down/up, Shift fast, Ctrl slow");
        ImGui::TextUnformatted("Orbit: Alt/Space + LMB rotate, MMB pan, wheel zoom");
        ImGui::End();

        if (ui.show_demo) ImGui::ShowDemoWindow(&ui.show_demo);

        if (rebuild_pipeline) {
            vkctx.device.waitIdle();
            rend = create_render_state(vkctx, sc, ui.wireframe);
        }

        if (rebuild_mesh) {
            vkctx.device.waitIdle();
            mesh = create_sphere_mesh(vkctx, ui.radius, static_cast<uint32_t>(ui.slices), static_cast<uint32_t>(ui.stacks), vec4{0.9f, 0.9f, 1.0f, 1.0f});
        }

        angle += ui.angle_speed * dt;
        const mat4 model = rotate_y(angle);

        if (ui.use_camera) {
            if (ui.fly_mode)
                cam.set_mode(camera::Mode::Fly);
            else
                cam.set_mode(camera::Mode::Orbit);

            const ImGuiIO& io      = ImGui::GetIO();
            const bool block_mouse = io.WantCaptureMouse;
            const bool block_kbd   = io.WantCaptureKeyboard;

            camera::CameraInput ci{};
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

            imgui::draw_mini_axis_gizmo(cam.matrices().view);

            in_state.dx     = 0.0f;
            in_state.dy     = 0.0f;
            in_state.scroll = 0.0f;

            const mat4 mvp = cam.matrices().view_proj * model;
            record_commands(vkctx, sc, rend, mesh, frames, frame_index, ar.image_index, mvp, imgui_sys);
        } else {
            in_state.dx     = 0.0f;
            in_state.dy     = 0.0f;
            in_state.scroll = 0.0f;

            const mat4 view = look_at(vec3{2.0f, 2.0f, 2.0f, 0.0f}, vec3{0.0f, 0.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f, 0.0f});
            const mat4 proj = perspective_vk(std::numbers::pi_v<float> / 3.0f, static_cast<float>(sc.extent.width) / static_cast<float>(sc.extent.height), 0.1f, 10.0f);
            const mat4 mvp  = proj * view * model;

            record_commands(vkctx, sc, rend, mesh, frames, frame_index, ar.image_index, mvp, imgui_sys);
        }

        if (end_frame(vkctx, sc, frames, frame_index, ar.image_index)) {
            recreate_swapchain(vkctx, surface, sc);
            on_swapchain_recreated(vkctx, sc, frames);
            vk::imgui::set_min_image_count(imgui_sys, 2);
            rend = create_render_state(vkctx, sc, ui.wireframe);
        }

        frame_index = (frame_index + 1) % frames.frames_in_flight;
    }

    vkctx.device.waitIdle();
    vk::imgui::shutdown(imgui_sys);
    return 0;
}
