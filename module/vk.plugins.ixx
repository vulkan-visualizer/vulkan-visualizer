module;
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
export module vk.plugins;
import vk.engine;
import vk.context;
import vk.camera;

namespace vk::plugins {
    export class Viewport3DPlugin {
    public:
        Viewport3DPlugin();
        ~Viewport3DPlugin() = default;
        Viewport3DPlugin(const Viewport3DPlugin&) = delete;
        Viewport3DPlugin& operator=(const Viewport3DPlugin&) = delete;
        Viewport3DPlugin(Viewport3DPlugin&&) noexcept = default;
        Viewport3DPlugin& operator=(Viewport3DPlugin&&) noexcept = default;

        [[nodiscard]] static constexpr const char* name() noexcept { return "Viewport3D"; }
        [[nodiscard]] static constexpr const char* description() noexcept { return "3D viewport with camera"; }
        [[nodiscard]] static constexpr uint32_t version() noexcept { return 1; }
        [[nodiscard]] static constexpr int32_t priority() noexcept { return 100; }
        [[nodiscard]] engine::PluginPhase phases() const noexcept;

        [[nodiscard]] bool is_enabled() const noexcept { return enabled_; }
        void set_enabled(const bool enabled) noexcept { enabled_ = enabled; }

        void on_setup(engine::PluginContext& ctx);
        void on_initialize(engine::PluginContext& ctx);
        void on_pre_render(engine::PluginContext& ctx);
        void on_render(engine::PluginContext& ctx);
        void on_post_render(engine::PluginContext& ctx);
        void on_present(engine::PluginContext& /*ctx*/) const noexcept {}
        void on_cleanup(engine::PluginContext& ctx);
        void on_event(const SDL_Event& event);
        void on_resize(uint32_t width, uint32_t height) noexcept;

        [[nodiscard]] camera::Camera& get_camera() noexcept { return camera_; }
        [[nodiscard]] const camera::Camera& get_camera() const noexcept { return camera_; }

    private:
        static void begin_rendering(VkCommandBuffer& cmd, const context::AttachmentView& target, VkExtent2D extent);
        static void end_rendering(VkCommandBuffer& cmd) noexcept;

        void create_imgui(context::EngineContext& eng, const context::FrameContext& frm);
        void destroy_imgui(const context::EngineContext& eng) const;
        void render_imgui(VkCommandBuffer& cmd, const context::FrameContext& frm);
        void draw_camera_panel();
        void draw_mini_axis_gizmo() const;

        bool enabled_{true};
        camera::Camera camera_;
        int viewport_width_{1920};
        int viewport_height_{1280};
        uint64_t last_time_ms_{0};
        bool show_camera_panel_{true};
        bool show_demo_window_{true};
    };

    export enum class ScreenshotFormat : uint8_t {
        PNG,
        JPG,
        BMP,
        TGA
    };

    export enum class ScreenshotSource : uint8_t {
        Swapchain,
        Offscreen,
        HighRes
    };

    export struct ScreenshotConfig {
        ScreenshotFormat format{ScreenshotFormat::PNG};
        ScreenshotSource source{ScreenshotSource::Swapchain};
        int jpeg_quality{95};
        bool include_alpha{true};
        bool auto_filename{true};
        std::string output_directory{"."};
        std::string filename_prefix{"screenshot"};
    };

    export class ScreenshotPlugin {
    public:
        ScreenshotPlugin() = default;
        ~ScreenshotPlugin() = default;
        ScreenshotPlugin(const ScreenshotPlugin&) = delete;
        ScreenshotPlugin& operator=(const ScreenshotPlugin&) = delete;
        ScreenshotPlugin(ScreenshotPlugin&&) noexcept = default;
        ScreenshotPlugin& operator=(ScreenshotPlugin&&) noexcept = default;

        [[nodiscard]] static constexpr const char* name() noexcept { return "Screenshot"; }
        [[nodiscard]] static constexpr const char* description() noexcept { return "Screenshot capture system"; }
        [[nodiscard]] static constexpr uint32_t version() noexcept { return 1; }
        [[nodiscard]] static constexpr int32_t priority() noexcept { return 50; }
        [[nodiscard]] engine::PluginPhase phases() const noexcept;

        [[nodiscard]] bool is_enabled() const noexcept { return enabled_; }
        void set_enabled(const bool enabled) noexcept { enabled_ = enabled; }

        void on_setup(engine::PluginContext& /*ctx*/) const noexcept {}
        void on_initialize(engine::PluginContext& ctx);
        void on_pre_render(engine::PluginContext& ctx);
        void on_render(engine::PluginContext& /*ctx*/) const noexcept {}
        void on_post_render(engine::PluginContext& /*ctx*/) const noexcept {}
        void on_present(engine::PluginContext& ctx);
        void on_cleanup(engine::PluginContext& ctx);
        void on_event(const SDL_Event& event);
        void on_resize(uint32_t /*width*/, uint32_t /*height*/) const noexcept {}

        void request_screenshot();
        void request_screenshot(const ScreenshotConfig& config);
        void set_config(const ScreenshotConfig& config) { config_ = config; }
        [[nodiscard]] const ScreenshotConfig& get_config() const noexcept { return config_; }

    private:
        void capture_swapchain(engine::PluginContext& ctx, uint32_t image_index);
        void save_screenshot(void* pixel_data, uint32_t width, uint32_t height, const std::string& path) const;
        [[nodiscard]] std::string generate_filename() const;

        bool enabled_{true};
        bool screenshot_requested_{false};
        ScreenshotConfig config_;

        struct CaptureData {
            VkBuffer buffer{VK_NULL_HANDLE};
            VmaAllocation allocation{};
            uint32_t width{0};
            uint32_t height{0};
            std::string output_path;
        };
        CaptureData pending_capture_;
    };

    // ============================================================================
    // Geometry Plugin - Built-in geometry rendering with instancing
    // ============================================================================

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

    export enum class RenderMode : uint8_t {
        Filled,
        Wireframe,
        Both
    };

    export struct GeometryInstance {
        camera::Vec3 position{0, 0, 0};
        camera::Vec3 rotation{0, 0, 0};  // Euler angles in degrees
        camera::Vec3 scale{1, 1, 1};
        camera::Vec3 color{1, 1, 1};
        float alpha{1.0f};
    };

    export struct GeometryBatch {
        GeometryType type{GeometryType::Box};
        RenderMode mode{RenderMode::Filled};
        std::vector<GeometryInstance> instances;
    };

    export class GeometryPlugin {
    public:
        GeometryPlugin();
        ~GeometryPlugin() = default;
        GeometryPlugin(const GeometryPlugin&) = delete;
        GeometryPlugin& operator=(const GeometryPlugin&) = delete;
        GeometryPlugin(GeometryPlugin&&) noexcept = default;
        GeometryPlugin& operator=(GeometryPlugin&&) noexcept = default;

        [[nodiscard]] static constexpr const char* name() noexcept { return "Geometry"; }
        [[nodiscard]] static constexpr const char* description() noexcept { return "Built-in geometry rendering with instancing"; }
        [[nodiscard]] static constexpr uint32_t version() noexcept { return 1; }
        [[nodiscard]] static constexpr int32_t priority() noexcept { return 75; }
        [[nodiscard]] engine::PluginPhase phases() const noexcept;

        [[nodiscard]] bool is_enabled() const noexcept { return enabled_; }
        void set_enabled(const bool enabled) noexcept { enabled_ = enabled; }

        void on_setup(engine::PluginContext& /*ctx*/) const noexcept {}
        void on_initialize(engine::PluginContext& ctx);
        void on_pre_render(engine::PluginContext& ctx);
        void on_render(engine::PluginContext& ctx);
        void on_post_render(engine::PluginContext& /*ctx*/) const noexcept {}
        void on_present(engine::PluginContext& /*ctx*/) const noexcept {}
        void on_cleanup(engine::PluginContext& ctx);
        void on_event(const SDL_Event& /*event*/) const noexcept {}
        void on_resize(uint32_t /*width*/, uint32_t /*height*/) const noexcept {}

        // Batch management
        void add_batch(const GeometryBatch& batch);
        void clear_batches();
        [[nodiscard]] size_t batch_count() const noexcept { return batches_.size(); }

        // Helper methods for quick geometry addition
        void add_sphere(const camera::Vec3& position, float radius, const camera::Vec3& color = {1, 1, 1}, RenderMode mode = RenderMode::Filled);
        void add_box(const camera::Vec3& position, const camera::Vec3& size, const camera::Vec3& color = {1, 1, 1}, RenderMode mode = RenderMode::Filled);
        void add_line(const camera::Vec3& start, const camera::Vec3& end, const camera::Vec3& color = {1, 1, 1});
        void add_ray(const camera::Vec3& origin, const camera::Vec3& direction, float length, const camera::Vec3& color = {1, 1, 1});
        void add_grid(const camera::Vec3& position, float size, int divisions, const camera::Vec3& color = {0.5f, 0.5f, 0.5f});

        // Advanced ray visualization methods for NeRF debugging
        void add_camera_frustum(const camera::Vec3& position, const camera::Vec3& forward, const camera::Vec3& up,
                                float fov_deg, float aspect, float near_dist, float far_dist, const camera::Vec3& color = {1, 1, 0});
        void add_image_plane(const camera::Vec3& camera_pos, const camera::Vec3& forward, const camera::Vec3& up,
                             float fov_deg, float aspect, float distance, int grid_divisions = 10, const camera::Vec3& color = {0.7f, 0.7f, 0.7f});
        void add_aabb(const camera::Vec3& min, const camera::Vec3& max, const camera::Vec3& color = {0, 1, 1}, RenderMode mode = RenderMode::Wireframe);
        void add_ray_with_aabb_intersection(const camera::Vec3& ray_origin, const camera::Vec3& ray_dir, float ray_length,
                                            const camera::Vec3& aabb_min, const camera::Vec3& aabb_max,
                                            const camera::Vec3& ray_color = {1, 0, 0}, const camera::Vec3& hit_color = {0, 1, 0});
        void add_coordinate_axes(const camera::Vec3& position, float size = 1.0f);

        // Batch ray visualization (for many rays at once)
        struct RayInfo {
            camera::Vec3 origin;
            camera::Vec3 direction;
            float length{1.0f};
            camera::Vec3 color{1, 1, 1};
        };
        void add_ray_batch(const std::vector<RayInfo>& rays, const camera::Vec3* aabb_min = nullptr, const camera::Vec3* aabb_max = nullptr);

        void set_viewport_plugin(Viewport3DPlugin* vp) noexcept { viewport_plugin_ = vp; }

    private:
        struct GeometryMesh {
            VkBuffer vertex_buffer{VK_NULL_HANDLE};
            VkBuffer index_buffer{VK_NULL_HANDLE};
            VmaAllocation vertex_allocation{};
            VmaAllocation index_allocation{};
            uint32_t vertex_count{0};
            uint32_t index_count{0};
            VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        };

        struct InstanceBuffer {
            VkBuffer buffer{VK_NULL_HANDLE};
            VmaAllocation allocation{};
            uint32_t capacity{0};
        };

        void create_pipelines(const context::EngineContext& eng);
        void destroy_pipelines(const context::EngineContext& eng);
        void create_geometry_meshes(const context::EngineContext& eng);
        void destroy_geometry_meshes(const context::EngineContext& eng);
        void update_instance_buffers(const context::EngineContext& eng);
        void render_batch(VkCommandBuffer cmd, const GeometryBatch& batch, const InstanceBuffer& instance_buffer, const camera::Mat4& view_proj);

        GeometryMesh create_sphere_mesh(const context::EngineContext& eng, uint32_t segments = 32);
        GeometryMesh create_box_mesh(const context::EngineContext& eng);
        GeometryMesh create_cylinder_mesh(const context::EngineContext& eng, uint32_t segments = 32);
        GeometryMesh create_cone_mesh(const context::EngineContext& eng, uint32_t segments = 32);
        GeometryMesh create_torus_mesh(const context::EngineContext& eng, uint32_t segments = 32, uint32_t tube_segments = 16);
        GeometryMesh create_capsule_mesh(const context::EngineContext& eng, uint32_t segments = 16);
        GeometryMesh create_plane_mesh(const context::EngineContext& eng);
        GeometryMesh create_circle_mesh(const context::EngineContext& eng, uint32_t segments = 32);
        GeometryMesh create_line_mesh(const context::EngineContext& eng);

        bool enabled_{true};
        Viewport3DPlugin* viewport_plugin_{nullptr};
        std::vector<GeometryBatch> batches_;

        VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
        VkPipeline filled_pipeline_{VK_NULL_HANDLE};
        VkPipeline wireframe_pipeline_{VK_NULL_HANDLE};
        VkPipeline line_pipeline_{VK_NULL_HANDLE};

        std::unordered_map<GeometryType, GeometryMesh> geometry_meshes_;
        std::vector<InstanceBuffer> instance_buffers_;
    };
} // namespace vk::plugins
