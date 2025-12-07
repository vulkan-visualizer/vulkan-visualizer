module;
#include <SDL3/SDL.h>
#include <cstdint>
#include <vulkan/vulkan.h>
export module vk.plugins;
import vk.engine;
import vk.context;
import vk.camera;

namespace vk::plugins {
    // ============================================================================
    // Built-in 3D Viewport Plugin
    // ============================================================================
    // This plugin provides a complete 3D viewport with:
    // - Camera system (orbit/fly modes)
    // - ImGui UI integration
    // - Axis gizmo overlay
    // - Pure viewport (no built-in 3D objects)
    // ============================================================================

    export class Viewport3DPlugin {
    public:
        Viewport3DPlugin();
        ~Viewport3DPlugin() = default;

        // Plugin metadata (required by CPlugin concept)
        const char* name() const { return "3D Viewport"; }
        const char* description() const { return "Built-in 3D viewport with camera"; }
        uint32_t version() const { return 1; }
        engine::PluginPhase phases() const;
        int32_t priority() const { return 100; }  // High priority for built-in plugin

        // Lifecycle (required by CPlugin concept)
        bool is_enabled() const { return enabled_; }
        void set_enabled(bool enabled) { enabled_ = enabled; }

        // Phase callbacks (required by CPlugin concept)
        void on_setup(engine::PluginContext& ctx);
        void on_initialize(engine::PluginContext& ctx);
        void on_pre_render(engine::PluginContext& ctx);
        void on_render(engine::PluginContext& ctx);
        void on_post_render(engine::PluginContext& ctx);
        void on_present(engine::PluginContext& ctx) {}
        void on_cleanup(engine::PluginContext& ctx);
        void on_event(const SDL_Event& event);
        void on_resize(uint32_t width, uint32_t height);

        // Plugin-specific API
        camera::Camera& get_camera() { return camera_; }
        const camera::Camera& get_camera() const { return camera_; }

    private:
        // Rendering subsystem
        static void begin_rendering(VkCommandBuffer& cmd, const context::AttachmentView& target, VkExtent2D extent);
        static void end_rendering(VkCommandBuffer& cmd);

        // UI subsystem
        void create_imgui(context::EngineContext& eng, const context::FrameContext& frm);
        void destroy_imgui(const context::EngineContext& eng);
        void render_imgui(VkCommandBuffer& cmd, const context::FrameContext& frm);
        void draw_camera_panel();
        void draw_mini_axis_gizmo() const;

    private:
        bool enabled_{true};

        // Camera
        camera::Camera camera_;
        int viewport_width_{1920};
        int viewport_height_{1280};
        uint64_t last_time_ms_{0};

        // UI
        bool show_camera_panel_{true};
        bool show_demo_window_{true};
    };
} // namespace vk::plugins
