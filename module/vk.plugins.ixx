module;
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <vk_mem_alloc.h>
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

    // ============================================================================
    // Screenshot Plugin - Built-in
    // ============================================================================
    // Modern screenshot capture system with multiple formats and quality options
    // ============================================================================

    export enum class ScreenshotFormat {
        PNG,        // Lossless, compressed
        JPG,        // Lossy, smaller size
        BMP,        // Uncompressed
        TGA         // Uncompressed with alpha
    };

    export enum class ScreenshotSource {
        Swapchain,      // Capture final swapchain image
        Offscreen,      // Capture offscreen render target
        HighRes         // Render at higher resolution (future)
    };

    export struct ScreenshotConfig {
        ScreenshotFormat format{ScreenshotFormat::PNG};
        ScreenshotSource source{ScreenshotSource::Swapchain};
        int jpeg_quality{95};           // 1-100 for JPEG
        bool include_alpha{true};        // For formats that support it
        bool auto_filename{true};        // Generate timestamp-based filename
        std::string output_directory{"."}; // Output directory
        std::string filename_prefix{"screenshot"};
    };

    export class ScreenshotPlugin {
    public:
        ScreenshotPlugin() = default;
        ~ScreenshotPlugin() = default;

        // Plugin metadata (required by CPlugin concept)
        const char* name() const { return "Screenshot"; }
        const char* description() const { return "Screenshot capture system"; }
        uint32_t version() const { return 1; }
        engine::PluginPhase phases() const;
        int32_t priority() const { return 50; }

        // Lifecycle (required by CPlugin concept)
        bool is_enabled() const { return enabled_; }
        void set_enabled(bool enabled) { enabled_ = enabled; }

        // Phase callbacks (required by CPlugin concept)
        void on_setup(engine::PluginContext& ctx) {}
        void on_initialize(engine::PluginContext& ctx);
        void on_pre_render(engine::PluginContext& ctx);
        void on_render(engine::PluginContext& ctx) {}
        void on_post_render(engine::PluginContext& ctx) {}
        void on_present(engine::PluginContext& ctx);
        void on_cleanup(engine::PluginContext& ctx);
        void on_event(const SDL_Event& event);
        void on_resize(uint32_t width, uint32_t height) {}

        // Plugin-specific API
        void request_screenshot();
        void request_screenshot(const ScreenshotConfig& config);
        void set_config(const ScreenshotConfig& config) { config_ = config; }
        const ScreenshotConfig& get_config() const { return config_; }

    private:
        void capture_swapchain(engine::PluginContext& ctx, uint32_t image_index);
        void save_screenshot(void* pixel_data, uint32_t width, uint32_t height, const std::string& path);
        std::string generate_filename() const;

    private:
        bool enabled_{true};
        bool screenshot_requested_{false};
        ScreenshotConfig config_;

        // Capture state
        struct CaptureData {
            VkBuffer buffer{VK_NULL_HANDLE};
            VmaAllocation allocation{};
            uint32_t width{0};
            uint32_t height{0};
            std::string output_path;
        };
        CaptureData pending_capture_;
    };
} // namespace vk::plugins
