module;
#include <SDL3/SDL.h>
#include <memory>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
export module vk.plugins.bitmap;
import vk.context;
import vk.toolkit.geometry;
import vk.toolkit.camera;
import vk.toolkit.math;

namespace vk::plugins {
    // Visualization modes
    enum class VisualizationMode {
        WireframeGrid, // Grid showing all cells with different colors (DEFAULT)
        SolidCubes, // Filled cubes for occupied voxels
        Points, // Point cloud of occupied voxels
        TransparentShell, // Semi-transparent occupied voxels with edges
        DensityColored // Color based on local density
    };
    export class BitmapViewer {
    public:
        [[nodiscard]] static constexpr context::PluginPhase phases() noexcept {
            return context::PluginPhase::Setup | context::PluginPhase::Initialize | context::PluginPhase::PreRender | context::PluginPhase::Render | context::PluginPhase::ImGUI | context::PluginPhase::Cleanup;
        }
        void on_setup(const context::PluginContext& ctx);
        void on_initialize(context::PluginContext& ctx);
        void on_pre_render(context::PluginContext& ctx);
        void on_render(context::PluginContext& ctx);
        void on_post_render(context::PluginContext& ctx);
        void on_imgui(context::PluginContext& ctx);
        void on_present(context::PluginContext&);
        void on_cleanup(context::PluginContext& ctx);
        void on_event(const SDL_Event& event);
        void on_resize(uint32_t width, uint32_t height);

        explicit BitmapViewer(std::shared_ptr<toolkit::camera::Camera> camera, const toolkit::geometry::BitmapView& view) noexcept;
        ~BitmapViewer()                                  = default;
        BitmapViewer(const BitmapViewer&)                = delete;
        BitmapViewer& operator=(const BitmapViewer&)     = delete;
        BitmapViewer(BitmapViewer&&) noexcept            = default;
        BitmapViewer& operator=(BitmapViewer&&) noexcept = default;

    protected:
        void create_pipeline(const context::EngineContext& eng, VkFormat color_format);
        void destroy_pipeline(const context::EngineContext& eng);
        void create_geometry_buffers(const context::EngineContext& eng);
        void destroy_geometry_buffers(const context::EngineContext& eng);

        void render_solid_cubes(VkCommandBuffer cmd, const toolkit::math::Mat4& view_proj);
        void render_wireframe_grid(VkCommandBuffer cmd, const toolkit::math::Mat4& view_proj);
        void render_points(VkCommandBuffer cmd, const toolkit::math::Mat4& view_proj);
        void render_transparent_shell(VkCommandBuffer cmd, const toolkit::math::Mat4& view_proj);
        void render_density_colored(VkCommandBuffer cmd, const toolkit::math::Mat4& view_proj);

    private:
        std::shared_ptr<toolkit::camera::Camera> camera_{};
        toolkit::geometry::BitmapView view_{};
        std::vector<toolkit::math::Vec3> voxel_positions_{};
        std::vector<toolkit::math::Vec3> density_colors_{};

        VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
        VkPipeline solid_pipeline_{VK_NULL_HANDLE};
        VkPipeline wireframe_pipeline_{VK_NULL_HANDLE};
        VkPipeline point_pipeline_{VK_NULL_HANDLE};
        VkPipeline transparent_pipeline_{VK_NULL_HANDLE};

        VkBuffer vertex_buffer_{VK_NULL_HANDLE};
        VkBuffer index_buffer_{VK_NULL_HANDLE};
        VkBuffer grid_vertex_buffer_{VK_NULL_HANDLE};
        VkBuffer grid_index_buffer_{VK_NULL_HANDLE};
        VkBuffer instance_buffer_{VK_NULL_HANDLE};
        VkBuffer all_grid_instance_buffer_{VK_NULL_HANDLE};
        VkBuffer density_color_buffer_{VK_NULL_HANDLE};

        VmaAllocation vertex_allocation_{};
        VmaAllocation index_allocation_{};
        VmaAllocation grid_vertex_allocation_{};
        VmaAllocation grid_index_allocation_{};
        VmaAllocation instance_allocation_{};
        VmaAllocation all_grid_instance_allocation_{};
        VmaAllocation density_color_allocation_{};

        VisualizationMode viz_mode_{VisualizationMode::SolidCubes};
        bool show_panel_{true};

        uint32_t vertex_count_{0};
        uint32_t index_count_{0};
        uint32_t grid_index_count_{0};
    };
} // namespace vk::plugins
