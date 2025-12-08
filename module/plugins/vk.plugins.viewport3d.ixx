module;
#include <SDL3/SDL.h>
#include <memory>
#include <numbers>
#include <vulkan/vulkan.h>
export module vk.plugins.viewport3d;
import vk.context;
import vk.toolkit.camera;

namespace vk::plugins {
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
        void on_imgui(context::PluginContext& ctx) const;
        static void on_present(context::PluginContext&) {}
        static void on_cleanup(context::PluginContext&) {}
        void on_event(const SDL_Event& event) const;
        void on_resize(uint32_t width, uint32_t height);

        explicit Viewport3D(const std::shared_ptr<toolkit::camera::Camera>& camera) : camera_(camera) {}
        ~Viewport3D()                                = default;
        Viewport3D(const Viewport3D&)                = delete;
        Viewport3D& operator=(const Viewport3D&)     = delete;
        Viewport3D(Viewport3D&&) noexcept            = default;
        Viewport3D& operator=(Viewport3D&&) noexcept = default;

    private:
        std::shared_ptr<toolkit::camera::Camera> camera_{nullptr};
        bool enabled_{true};
        int viewport_width_{1920};
        int viewport_height_{1280};
        uint64_t last_time_ms_{0};
    };
} // namespace vk::plugins
