module;
#include <SDL3/SDL.h>
#include <print>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>
export module vk.plugins.transform;
import vk.context;
import vk.toolkit.geometry;
import vk.toolkit.math;
import vk.toolkit.camera;

namespace vk::plugins {
    void append_lines(std::vector<toolkit::geometry::Vertex>& out, const std::vector<toolkit::geometry::ColoredLine>& lines) {
        out.reserve(out.size() + lines.size() * 2);
        for (const auto& l : lines) {
            out.push_back(toolkit::geometry::Vertex{l.a, l.color});
            out.push_back(toolkit::geometry::Vertex{l.b, l.color});
        }
    }

    struct MeshBuffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        uint32_t vertex_count{0};
    };

    MeshBuffer create_vertex_buffer(const context::EngineContext& eng, std::span<const toolkit::geometry::Vertex> vertices);

    export class TransformViewer {
    public:
        [[nodiscard]] static constexpr context::PluginPhase phases() noexcept {
            return context::PluginPhase::Setup | context::PluginPhase::Initialize | context::PluginPhase::PreRender | context::PluginPhase::Render | context::PluginPhase::ImGUI | context::PluginPhase::Cleanup;
        }
        static void on_setup(const context::PluginContext& ctx);
        void on_initialize(context::PluginContext& ctx);
        void on_pre_render(context::PluginContext& ctx) const;
        void on_render(context::PluginContext& ctx) const;
        static void on_post_render(context::PluginContext& ctx) {}
        void on_imgui(context::PluginContext& ctx) const;
        static void on_present(context::PluginContext&) {}
        void on_cleanup(context::PluginContext& ctx);
        void on_event(const SDL_Event& event) const;
        static void on_resize(uint32_t width, uint32_t height) {}

        explicit TransformViewer(std::shared_ptr<toolkit::camera::Camera> camera, std::vector<toolkit::math::Mat4> poses) : camera_(std::move(camera)), poses_(std::move(poses)) {}
        ~TransformViewer()                                     = default;
        TransformViewer(const TransformViewer&)                = delete;
        TransformViewer& operator=(const TransformViewer&)     = delete;
        TransformViewer(TransformViewer&&) noexcept            = default;
        TransformViewer& operator=(TransformViewer&&) noexcept = default;

    protected:
        void create_pipeline(const context::EngineContext& eng, VkFormat color_format);
        void destroy_pipeline(const context::EngineContext& eng);

    private:
        std::shared_ptr<toolkit::camera::Camera> camera_{};
        std::vector<toolkit::math::Mat4> poses_{};

        MeshBuffer mesh_buffer_{};
        VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
        VkPipeline pipeline_{VK_NULL_HANDLE};
    };
} // namespace vk::plugins
