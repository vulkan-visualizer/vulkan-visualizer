module;
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
export module vk.plugins.geometry;
import vk.context;
import vk.toolkit.math;

namespace vk::plugins {
    export enum class GeometryType : uint8_t {
        // 3D Primitives
        Sphere,
        Box,
        Cylinder,
        Cone,
        Torus,
        Capsule,

        // 2D Primitives
        Plane,
        Circle,
        Line,
        Ray,
        Grid
    };

    struct GeometryMesh {
        VkBuffer vertex_buffer{VK_NULL_HANDLE};
        VkBuffer index_buffer{VK_NULL_HANDLE};
        VmaAllocation vertex_allocation{};
        VmaAllocation index_allocation{};
        uint32_t vertex_count{0};
        uint32_t index_count{0};
        VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};

        static GeometryMesh create_sphere_mesh(const context::EngineContext& eng, uint32_t segments = 32, std::vector<float>* out_vertices = nullptr, std::vector<uint32_t>* out_indices = nullptr);
        static GeometryMesh create_box_mesh(const context::EngineContext& eng, std::vector<float>* out_vertices = nullptr, std::vector<uint32_t>* out_indices = nullptr);
        static GeometryMesh create_cylinder_mesh(const context::EngineContext& eng, uint32_t segments = 32, std::vector<float>* out_vertices = nullptr, std::vector<uint32_t>* out_indices = nullptr);
        static GeometryMesh create_cone_mesh(const context::EngineContext& eng, uint32_t segments = 32, std::vector<float>* out_vertices = nullptr, std::vector<uint32_t>* out_indices = nullptr);
        static GeometryMesh create_torus_mesh(const context::EngineContext& eng, uint32_t segments = 32, uint32_t tube_segments = 16, std::vector<float>* out_vertices = nullptr, std::vector<uint32_t>* out_indices = nullptr);
        static GeometryMesh create_capsule_mesh(const context::EngineContext& eng, uint32_t segments = 16, std::vector<float>* out_vertices = nullptr, std::vector<uint32_t>* out_indices = nullptr);
        static GeometryMesh create_plane_mesh(const context::EngineContext& eng, std::vector<float>* out_vertices = nullptr, std::vector<uint32_t>* out_indices = nullptr);
        static GeometryMesh create_circle_mesh(const context::EngineContext& eng, uint32_t segments = 32, std::vector<float>* out_vertices = nullptr, std::vector<uint32_t>* out_indices = nullptr);
        static GeometryMesh create_line_mesh(const context::EngineContext& eng);
        static GeometryMesh create_face_normal_mesh(const context::EngineContext& eng, const std::vector<float>& vertices, const std::vector<uint32_t>& indices, float normal_length = 0.1f);
    };

    struct InstanceBuffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VmaAllocation allocation{};
        uint32_t capacity{0};
    };
    export struct GeometryInstance {
        toolkit::math::Vec3 position{0, 0, 0};
        toolkit::math::Vec3 rotation{0, 0, 0}; // Euler angles in degrees
        toolkit::math::Vec3 scale{1, 1, 1};
        toolkit::math::Vec3 color{1, 1, 1};
        float alpha{1.0f};
    };
    export enum class RenderMode : uint8_t { Filled, Wireframe, Both };
    export struct GeometryBatch {
        GeometryType type{GeometryType::Box};
        RenderMode mode{RenderMode::Filled};
        std::vector<GeometryInstance> instances{};
    };

    export class Geometry {
    public:
        [[nodiscard]] static constexpr context::PluginPhase phases() noexcept {
            return context::PluginPhase::Setup | context::PluginPhase::Initialize | context::PluginPhase::PreRender | context::PluginPhase::Render | context::PluginPhase::ImGUI | context::PluginPhase::Cleanup;
        }
        static void on_setup(const context::PluginContext& ctx);
        void on_initialize(context::PluginContext& ctx);
        void on_pre_render(context::PluginContext& ctx);
        void on_render(context::PluginContext& ctx);
        static void on_post_render(context::PluginContext&) {}
        static void on_imgui(context::PluginContext& ctx) {}
        static void on_present(context::PluginContext&) {}
        void on_cleanup(context::PluginContext& ctx);
        static void on_event(const SDL_Event&) {}
        void on_resize(uint32_t width, uint32_t height);

        explicit Geometry(const std::shared_ptr<context::Camera>& camera) : camera_(camera) {}
        ~Geometry()                              = default;
        Geometry(const Geometry&)                = delete;
        Geometry& operator=(const Geometry&)     = delete;
        Geometry(Geometry&&) noexcept            = default;
        Geometry& operator=(Geometry&&) noexcept = default;

        void add_batch(const GeometryBatch& batch) {
            batches_.push_back(batch);
        }
        void visualize_normals(const bool enabled) {
            show_face_normals_ = enabled;
        }

    protected:
        void create_pipelines(const context::EngineContext& eng, VkFormat color_format, VkFormat depth_format);
        void destroy_pipelines(const context::EngineContext& eng);
        void create_geometry_meshes(const context::EngineContext& eng);
        void destroy_geometry_meshes(const context::EngineContext& eng);
        void update_instance_buffers(const context::EngineContext& eng);
        void render_batch(VkCommandBuffer& cmd, const GeometryBatch& batch, const InstanceBuffer& instance_buffer, const toolkit::math::Mat4& view_proj);

    private:
        std::shared_ptr<context::Camera> camera_{nullptr};
        std::vector<GeometryBatch> batches_{};

        std::unordered_map<GeometryType, GeometryMesh> geometry_meshes_{};
        std::unordered_map<GeometryType, GeometryMesh> normal_meshes_{};
        std::vector<InstanceBuffer> instance_buffers_{};

        VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
        VkPipeline filled_pipeline_{VK_NULL_HANDLE};
        VkPipeline wireframe_pipeline_{VK_NULL_HANDLE};
        VkPipeline line_pipeline_{VK_NULL_HANDLE};

        VkFormat color_format_{VK_FORMAT_UNDEFINED};
        VkFormat depth_format_{VK_FORMAT_UNDEFINED};
        VkImageLayout depth_layout_{VK_IMAGE_LAYOUT_UNDEFINED};

        bool show_face_normals_{false};
    };
} // namespace vk::plugins
