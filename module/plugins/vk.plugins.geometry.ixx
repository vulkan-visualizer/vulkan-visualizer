module;
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <memory>
#include <numbers>
#include <unordered_map>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
export module vk.plugins.geometry;
import vk.context;
import vk.toolkit.math;
import vk.toolkit.camera;

namespace vk::plugins {
    // clang-format off
    template <float SX, float SY, float SZ>
    constexpr auto BOX_GEOMETRY() {
        constexpr float hx = SX * 0.5f;
        constexpr float hy = SY * 0.5f;
        constexpr float hz = SZ * 0.5f;

        // 24 vertices * 6 floats
        constexpr std::array<float, 24 * 6> vertices{
            // Front (+Z)
            -hx, -hy,  hz,  0.f,  0.f,  1.f,
             hx, -hy,  hz,  0.f,  0.f,  1.f,
             hx,  hy,  hz,  0.f,  0.f,  1.f,
            -hx,  hy,  hz,  0.f,  0.f,  1.f,

            // Back (-Z)
            -hx, -hy, -hz,  0.f,  0.f, -1.f,
            -hx,  hy, -hz,  0.f,  0.f, -1.f,
             hx,  hy, -hz,  0.f,  0.f, -1.f,
             hx, -hy, -hz,  0.f,  0.f, -1.f,

            // Top (+Y)
            -hx,  hy, -hz,  0.f,  1.f,  0.f,
            -hx,  hy,  hz,  0.f,  1.f,  0.f,
             hx,  hy,  hz,  0.f,  1.f,  0.f,
             hx,  hy, -hz,  0.f,  1.f,  0.f,

            // Bottom (-Y)
            -hx, -hy, -hz,  0.f, -1.f,  0.f,
             hx, -hy, -hz,  0.f, -1.f,  0.f,
             hx, -hy,  hz,  0.f, -1.f,  0.f,
            -hx, -hy,  hz,  0.f, -1.f,  0.f,

            // Right (+X)
             hx, -hy, -hz,  1.f,  0.f,  0.f,
             hx,  hy, -hz,  1.f,  0.f,  0.f,
             hx,  hy,  hz,  1.f,  0.f,  0.f,
             hx, -hy,  hz,  1.f,  0.f,  0.f,

            // Left (-X)
            -hx, -hy, -hz, -1.f,  0.f,  0.f,
            -hx, -hy,  hz, -1.f,  0.f,  0.f,
            -hx,  hy,  hz, -1.f,  0.f,  0.f,
            -hx,  hy, -hz, -1.f,  0.f,  0.f,
        };

        constexpr std::array<uint32_t, 36> indices{
            0, 1, 2, 2, 3, 0,
            4, 5, 6, 6, 7, 4,
            8, 9, 10, 10, 11, 8,
            12, 13, 14, 14, 15, 12,
            16, 17, 18, 18, 19, 16,
            20, 21, 22, 22, 23, 20
        };

        return std::pair{vertices, indices};
    }

    template <uint32_t SEGMENTS>
    constexpr auto SPHERE_GEOMETRY() {
        static_assert(SEGMENTS >= 3);

        constexpr uint32_t RINGS   = SEGMENTS;
        constexpr uint32_t SECTORS = SEGMENTS * 2;

        constexpr uint32_t VERTEX_COUNT = (RINGS + 1) * (SECTORS + 1);
        constexpr uint32_t INDEX_COUNT  = RINGS * SECTORS * 6;

        std::array<float, VERTEX_COUNT * 6> vertices{};   // pos + normal
        std::array<uint32_t, INDEX_COUNT> indices{};

        constexpr float PI = std::numbers::pi_v<float>;

        // -------------------------
        // Generate vertices
        // -------------------------
        uint32_t v = 0;
        for (uint32_t r = 0; r <= RINGS; ++r) {
            const float phi = (float(r) / float(RINGS)) * PI;

            const float sin_phi = std::sin(phi);
            const float cos_phi = std::cos(phi);

            for (uint32_t s = 0; s <= SECTORS; ++s) {
                const float theta = (float(s) / float(SECTORS)) * 2.0f * PI;

                const float x = sin_phi * std::cos(theta);
                const float y = cos_phi;
                const float z = sin_phi * std::sin(theta);

                // position
                vertices[v++] = x;
                vertices[v++] = y;
                vertices[v++] = z;

                // normal (same as position)
                vertices[v++] = x;
                vertices[v++] = y;
                vertices[v++] = z;
            }
        }

        // -------------------------
        // Generate indices (CCW outside)
        // -------------------------
        uint32_t i = 0;
        for (uint32_t r = 0; r < RINGS; ++r) {
            for (uint32_t s = 0; s < SECTORS; ++s) {
                const uint32_t current = r * (SECTORS + 1) + s;
                const uint32_t next    = current + SECTORS + 1;

                indices[i++] = current;
                indices[i++] = current + 1;
                indices[i++] = next;

                indices[i++] = current + 1;
                indices[i++] = next + 1;
                indices[i++] = next;
            }
        }

        return std::pair{vertices, indices};
    }

    template <uint32_t SEGMENTS>
    constexpr auto CYLINDER_GEOMETRY() {
        static_assert(SEGMENTS >= 3);

        constexpr uint32_t VERTEX_COUNT = 4 * SEGMENTS + 6;
        constexpr uint32_t INDEX_COUNT  = SEGMENTS * 12;

        std::array<float, VERTEX_COUNT * 6> vertices{};
        std::array<uint32_t, INDEX_COUNT> indices{};

        constexpr float PI = std::numbers::pi_v<float>;

        uint32_t v = 0;
        uint32_t i = 0;

        // -------------------------
        // Side vertices
        // -------------------------
        for (uint32_t s = 0; s <= SEGMENTS; ++s) {
            const float angle = (float(s) / float(SEGMENTS)) * 2.0f * PI;
            const float x = std::cos(angle);
            const float z = std::sin(angle);

            // bottom
            vertices[v++] = x;
            vertices[v++] = -0.5f;
            vertices[v++] = z;
            vertices[v++] = x;
            vertices[v++] = 0.0f;
            vertices[v++] = z;

            // top
            vertices[v++] = x;
            vertices[v++] = 0.5f;
            vertices[v++] = z;
            vertices[v++] = x;
            vertices[v++] = 0.0f;
            vertices[v++] = z;
        }

        // Side indices (CCW from outside)
        for (uint32_t s = 0; s < SEGMENTS; ++s) {
            const uint32_t base = s * 2;

            indices[i++] = base;
            indices[i++] = base + 1;
            indices[i++] = base + 2;

            indices[i++] = base + 2;
            indices[i++] = base + 1;
            indices[i++] = base + 3;
        }

        // -------------------------
        // Bottom cap
        // -------------------------
        const uint32_t bottom_center = v / 6;

        vertices[v++] = 0.0f;
        vertices[v++] = -0.5f;
        vertices[v++] = 0.0f;
        vertices[v++] = 0.0f;
        vertices[v++] = -1.0f;
        vertices[v++] = 0.0f;

        for (uint32_t s = 0; s <= SEGMENTS; ++s) {
            const float angle = (float(s) / float(SEGMENTS)) * 2.0f * PI;
            const float x = std::cos(angle);
            const float z = std::sin(angle);

            vertices[v++] = x;
            vertices[v++] = -0.5f;
            vertices[v++] = z;
            vertices[v++] = 0.0f;
            vertices[v++] = -1.0f;
            vertices[v++] = 0.0f;
        }

        for (uint32_t s = 0; s < SEGMENTS; ++s) {
            indices[i++] = bottom_center;
            indices[i++] = bottom_center + s + 1;
            indices[i++] = bottom_center + s + 2;
        }

        // -------------------------
        // Top cap
        // -------------------------
        const uint32_t top_center = v / 6;

        vertices[v++] = 0.0f;
        vertices[v++] = 0.5f;
        vertices[v++] = 0.0f;
        vertices[v++] = 0.0f;
        vertices[v++] = 1.0f;
        vertices[v++] = 0.0f;

        for (uint32_t s = 0; s <= SEGMENTS; ++s) {
            const float angle = (float(s) / float(SEGMENTS)) * 2.0f * PI;
            const float x = std::cos(angle);
            const float z = std::sin(angle);

            vertices[v++] = x;
            vertices[v++] = 0.5f;
            vertices[v++] = z;
            vertices[v++] = 0.0f;
            vertices[v++] = 1.0f;
            vertices[v++] = 0.0f;
        }

        for (uint32_t s = 0; s < SEGMENTS; ++s) {
            indices[i++] = top_center;
            indices[i++] = top_center + s + 2;
            indices[i++] = top_center + s + 1;
        }

        return std::pair{vertices, indices};
    }

    template <uint32_t SEGMENTS>
    constexpr auto CONE_GEOMETRY() {
        static_assert(SEGMENTS >= 3);

        constexpr uint32_t VERTEX_COUNT = 2 * SEGMENTS + 4;
        constexpr uint32_t INDEX_COUNT  = SEGMENTS * 6;

        std::array<float, VERTEX_COUNT * 6> vertices{};
        std::array<uint32_t, INDEX_COUNT> indices{};

        constexpr float PI = std::numbers::pi_v<float>;
        constexpr float NR = 0.70710678f; // 1 / sqrt(2)
        constexpr float NY = 0.70710678f;

        uint32_t v = 0;
        uint32_t i = 0;

        // -------------------------
        // Apex
        // -------------------------
        vertices[v++] = 0.0f;
        vertices[v++] = 0.5f;
        vertices[v++] = 0.0f;
        vertices[v++] = 0.0f;
        vertices[v++] = 1.0f;
        vertices[v++] = 0.0f;

        // -------------------------
        // Side ring
        // -------------------------
        for (uint32_t s = 0; s <= SEGMENTS; ++s) {
            const float angle = (float(s) / float(SEGMENTS)) * 2.0f * PI;
            const float x = std::cos(angle);
            const float z = std::sin(angle);

            vertices[v++] = x;
            vertices[v++] = -0.5f;
            vertices[v++] = z;

            vertices[v++] = x * NR;
            vertices[v++] = NY;
            vertices[v++] = z * NR;
        }

        // Side indices (CCW outside)
        for (uint32_t s = 0; s < SEGMENTS; ++s) {
            indices[i++] = 0;        // apex
            indices[i++] = s + 2;    // next
            indices[i++] = s + 1;    // current
        }

        // -------------------------
        // Base cap
        // -------------------------
        const uint32_t base_center = v / 6;

        // center
        vertices[v++] = 0.0f;
        vertices[v++] = -0.5f;
        vertices[v++] = 0.0f;
        vertices[v++] = 0.0f;
        vertices[v++] = -1.0f;
        vertices[v++] = 0.0f;

        // ring
        for (uint32_t s = 0; s <= SEGMENTS; ++s) {
            const float angle = (float(s) / float(SEGMENTS)) * 2.0f * PI;
            const float x = std::cos(angle);
            const float z = std::sin(angle);

            vertices[v++] = x;
            vertices[v++] = -0.5f;
            vertices[v++] = z;
            vertices[v++] = 0.0f;
            vertices[v++] = -1.0f;
            vertices[v++] = 0.0f;
        }

        for (uint32_t s = 0; s < SEGMENTS; ++s) {
            indices[i++] = base_center;
            indices[i++] = base_center + s + 1;
            indices[i++] = base_center + s + 2;
        }

        return std::pair{vertices, indices};
    }

    template <uint32_t SEGMENTS, uint32_t TUBE_SEGMENTS>
    constexpr auto TORUS_GEOMETRY(
        float major_radius = 0.4f,
        float minor_radius = 0.15f
    ) {
        static_assert(SEGMENTS >= 3);
        static_assert(TUBE_SEGMENTS >= 3);

        constexpr uint32_t VERTEX_COUNT =
            (SEGMENTS + 1) * (TUBE_SEGMENTS + 1);

        constexpr uint32_t INDEX_COUNT =
            SEGMENTS * TUBE_SEGMENTS * 6;

        std::array<float, VERTEX_COUNT * 6> vertices{};
        std::array<uint32_t, INDEX_COUNT> indices{};

        constexpr float PI = std::numbers::pi_v<float>;

        uint32_t v = 0;
        uint32_t i = 0;

        // -------------------------
        // Vertices
        // -------------------------
        for (uint32_t s = 0; s <= SEGMENTS; ++s) {
            const float u = (float(s) / float(SEGMENTS)) * 2.0f * PI;
            const float cu = std::cos(u);
            const float su = std::sin(u);

            for (uint32_t t = 0; t <= TUBE_SEGMENTS; ++t) {
                const float v_angle = (float(t) / float(TUBE_SEGMENTS)) * 2.0f * PI;

                const float cv = std::cos(v_angle);
                const float sv = std::sin(v_angle);

                const float x = (major_radius + minor_radius * cv) * cu;
                const float y = minor_radius * sv;
                const float z = (major_radius + minor_radius * cv) * su;

                const float nx = cv * cu;
                const float ny = sv;
                const float nz = cv * su;

                vertices[v++] = x;
                vertices[v++] = y;
                vertices[v++] = z;
                vertices[v++] = nx;
                vertices[v++] = ny;
                vertices[v++] = nz;
            }
        }

        // -------------------------
        // Indices (CCW outside)
        // -------------------------
        for (uint32_t s = 0; s < SEGMENTS; ++s) {
            for (uint32_t t = 0; t < TUBE_SEGMENTS; ++t) {
                const uint32_t a = s * (TUBE_SEGMENTS + 1) + t;
                const uint32_t b = a + TUBE_SEGMENTS + 1;

                indices[i++] = a;
                indices[i++] = a + 1;
                indices[i++] = b;

                indices[i++] = a + 1;
                indices[i++] = b + 1;
                indices[i++] = b;
            }
        }

        return std::pair{vertices, indices};
    }

    template <uint32_t SEGMENTS>
    constexpr auto CAPSULE_GEOMETRY(
        float half_height = 0.5f,
        float radius      = 0.25f
    ) {
        static_assert(SEGMENTS >= 6);
        static_assert(SEGMENTS % 2 == 0);

        constexpr uint32_t LAT = SEGMENTS / 2;

        constexpr uint32_t VERTEX_COUNT =
            2 * (LAT + 1) * (SEGMENTS + 1) +
            2 * (SEGMENTS + 1);

        constexpr uint32_t INDEX_COUNT =
            SEGMENTS * (2 * LAT + 1) * 6;

        std::array<float, VERTEX_COUNT * 6> vertices{};
        std::array<uint32_t, INDEX_COUNT> indices{};

        constexpr float PI = std::numbers::pi_v<float>;

        uint32_t v = 0;
        uint32_t i = 0;

        // ------------------------------------------------
        // Top hemisphere (phi: 0 → π/2)
        // ------------------------------------------------
        for (uint32_t r = 0; r <= LAT; ++r) {
            const float phi = (float(r) / float(LAT)) * (PI * 0.5f);
            const float sp = std::sin(phi);
            const float cp = std::cos(phi);

            for (uint32_t s = 0; s <= SEGMENTS; ++s) {
                const float t = (float(s) / float(SEGMENTS)) * 2.0f * PI;
                const float ct = std::cos(t);
                const float st = std::sin(t);

                vertices[v++] = radius * cp * ct;
                vertices[v++] = half_height + radius * sp;
                vertices[v++] = radius * cp * st;

                vertices[v++] = cp * ct;
                vertices[v++] = sp;
                vertices[v++] = cp * st;
            }
        }

        const uint32_t top_base = 0;

        for (uint32_t r = 0; r < LAT; ++r) {
            for (uint32_t s = 0; s < SEGMENTS; ++s) {
                const uint32_t c = top_base + r * (SEGMENTS + 1) + s;
                const uint32_t n = c + (SEGMENTS + 1);

                indices[i++] = c;
                indices[i++] = n;
                indices[i++] = c + 1;

                indices[i++] = c + 1;
                indices[i++] = n;
                indices[i++] = n + 1;
            }
        }

        // ------------------------------------------------
        // Cylinder
        // ------------------------------------------------
        const uint32_t cyl_base = v / 6;

        for (uint32_t s = 0; s <= SEGMENTS; ++s) {
            const float t = (float(s) / float(SEGMENTS)) * 2.0f * PI;
            const float ct = std::cos(t);
            const float st = std::sin(t);

            // top ring
            vertices[v++] = radius * ct;
            vertices[v++] = half_height;
            vertices[v++] = radius * st;

            vertices[v++] = ct;
            vertices[v++] = 0.0f;
            vertices[v++] = st;

            // bottom ring
            vertices[v++] = radius * ct;
            vertices[v++] = -half_height;
            vertices[v++] = radius * st;

            vertices[v++] = ct;
            vertices[v++] = 0.0f;
            vertices[v++] = st;
        }

        for (uint32_t s = 0; s < SEGMENTS; ++s) {
            const uint32_t b = cyl_base + s * 2;

            indices[i++] = b;
            indices[i++] = b + 2;
            indices[i++] = b + 1;

            indices[i++] = b + 2;
            indices[i++] = b + 3;
            indices[i++] = b + 1;
        }

        // ------------------------------------------------
        // Bottom hemisphere (phi: 0 → π/2)
        // ------------------------------------------------
        const uint32_t bot_base = v / 6;

        for (uint32_t r = 0; r <= LAT; ++r) {
            const float phi = (float(r) / float(LAT)) * (PI * 0.5f);
            const float sp = std::sin(phi);
            const float cp = std::cos(phi);

            for (uint32_t s = 0; s <= SEGMENTS; ++s) {
                const float t = (float(s) / float(SEGMENTS)) * 2.0f * PI;
                const float ct = std::cos(t);
                const float st = std::sin(t);

                vertices[v++] = radius * cp * ct;
                vertices[v++] = -half_height - radius * sp;
                vertices[v++] = radius * cp * st;

                vertices[v++] = cp * ct;
                vertices[v++] = -sp;
                vertices[v++] = cp * st;
            }
        }

        for (uint32_t r = 0; r < LAT; ++r) {
            for (uint32_t s = 0; s < SEGMENTS; ++s) {
                const uint32_t c = bot_base + r * (SEGMENTS + 1) + s;
                const uint32_t n = c + (SEGMENTS + 1);

                indices[i++] = c;
                indices[i++] = c + 1;
                indices[i++] = n;

                indices[i++] = c + 1;
                indices[i++] = n + 1;
                indices[i++] = n;
            }
        }

        return std::pair{vertices, indices};
    }

    constexpr auto PLANE_GEOMETRY() {
        // 4 vertices * (pos + normal)
        constexpr std::array<float, 4 * 6> vertices = {
            // position             // normal
            -0.5f, 0.0f, -0.5f,     0.0f, 1.0f, 0.0f,
             0.5f, 0.0f, -0.5f,     0.0f, 1.0f, 0.0f,
             0.5f, 0.0f,  0.5f,     0.0f, 1.0f, 0.0f,
            -0.5f, 0.0f,  0.5f,     0.0f, 1.0f, 0.0f,
        };

        // CCW winding (outside = +Y)
        constexpr std::array<uint32_t, 6> indices = {
            0, 2, 1,
            2, 0, 3
        };

        return std::pair{vertices, indices};
    }
    // clang-format on

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

        explicit Geometry(const std::shared_ptr<toolkit::camera::Camera>& camera) : camera_(camera) {}
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
        std::shared_ptr<toolkit::camera::Camera> camera_{nullptr};
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
