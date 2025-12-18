export module vk.geometry;
import vk.math;
import std;


namespace vk::geometry {
    export struct alignas(16) VertexP2C4 {
        math::vec2 position; // 8B
        math::vec2 _pad; // pad to 16B
        math::vec4 color; // 16B
    };

    static_assert(std::is_standard_layout_v<VertexP2C4>);
    static_assert(std::is_trivially_copyable_v<VertexP2C4>);
    static_assert(sizeof(VertexP2C4) == 32);
    static_assert(alignof(VertexP2C4) == 16);

    export struct alignas(16) VertexP3C4 {
        math::vec3 position; // 16B
        math::vec4 color; // 16B
    };

    static_assert(std::is_standard_layout_v<VertexP3C4>);
    static_assert(std::is_trivially_copyable_v<VertexP3C4>);
    static_assert(sizeof(VertexP3C4) == 32);
    static_assert(alignof(VertexP3C4) == 16);

    export struct alignas(16) Vertex {
        math::vec3 position; // 16B
        math::vec3 normal; // 16B
        math::vec2 uv; // 8B
        math::vec2 _pad0; // pad to 48B
        math::vec4 color; // 16B
    };

    static_assert(std::is_standard_layout_v<Vertex>);
    static_assert(std::is_trivially_copyable_v<Vertex>);
    static_assert(sizeof(Vertex) == 64);
    static_assert(alignof(Vertex) == 16);


    export struct MeshP3C4 {
        std::vector<VertexP3C4> vertices{};
        std::vector<std::uint32_t> indices{};
    };


    export [[nodiscard]] MeshP3C4 make_sphere_p3c4(float radius, std::uint32_t slices, std::uint32_t stacks, const math::vec4& color);
    export [[nodiscard]] MeshP3C4 make_cube_p3c4(float half_extent, const math::vec4& color);
} // namespace vk::geometry
