module;
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
export module vk.plugins.screenshot;
import vk.context;


namespace vk::plugins {
    enum class ScreenshotFormat : uint8_t { PNG, JPG, BMP, TGA };
    enum class ScreenshotSource : uint8_t { Swapchain, Offscreen, HighRes };

    export class Screenshot {
    public:
        [[nodiscard]] static constexpr context::PluginPhase phases() noexcept {
            return context::PluginPhase::Initialize | context::PluginPhase::PreRender | context::PluginPhase::Present | context::PluginPhase::Cleanup;
        }
        static void on_setup(context::PluginContext&) {}
        static void on_initialize(context::PluginContext&) {}
        void on_pre_render(const context::PluginContext&);
        static void on_render(context::PluginContext&) {}
        static void on_post_render(context::PluginContext&) {}
        static void on_imgui(context::PluginContext&) {}
        void on_present(context::PluginContext& ctx);
        void on_cleanup(const context::PluginContext& ctx);
        void on_event(const SDL_Event& event);
        static void on_resize(uint32_t, uint32_t) {}


        Screenshot()                                 = default;
        ~Screenshot()                                = default;
        Screenshot(const Screenshot&)                = delete;
        Screenshot& operator=(const Screenshot&)     = delete;
        Screenshot(Screenshot&&) noexcept            = default;
        Screenshot& operator=(Screenshot&&) noexcept = default;

    private:
        [[nodiscard]] std::string generate_filename() const;

        bool screenshot_requested_{false};
        struct ScreenshotConfig {
            ScreenshotFormat format{ScreenshotFormat::PNG};
            ScreenshotSource source{ScreenshotSource::Swapchain};
            int jpeg_quality{95};
            bool include_alpha{true};
            bool auto_filename{true};
            std::string output_directory{"."};
            std::string filename_prefix{"screenshot"};
        } config_;

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
