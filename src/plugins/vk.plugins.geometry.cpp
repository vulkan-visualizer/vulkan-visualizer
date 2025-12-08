module;
#include <array>
#include <backends/imgui_impl_vulkan.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <imgui.h>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
module vk.plugins.geometry;
import vk.context;

namespace {
    void create_buffer_with_data(const vk::context::EngineContext& eng, const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VmaAllocation& allocation) {
        const VkBufferCreateInfo buffer_ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

        constexpr VmaAllocationCreateInfo alloc_ci{.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO, .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};

        VmaAllocationInfo alloc_info{};
        vk::context::vk_check(vmaCreateBuffer(eng.allocator, &buffer_ci, &alloc_ci, &buffer, &allocation, &alloc_info), "Failed to create geometry buffer");

        void* mapped = nullptr;
        vmaMapMemory(eng.allocator, allocation, &mapped);
        std::memcpy(mapped, data, size);
        vmaUnmapMemory(eng.allocator, allocation);
    }
} // namespace

vk::plugins::GeometryMesh vk::plugins::GeometryMesh::create_sphere_mesh(const context::EngineContext& eng, uint32_t segments, std::vector<float>* out_vertices, std::vector<uint32_t>* out_indices) {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    const auto rings   = segments;
    const auto sectors = segments * 2;

    // Generate vertices
    for (uint32_t r = 0; r <= rings; ++r) {
        const auto phi = static_cast<float>(r) / static_cast<float>(rings) * 3.14159265359f;
        for (uint32_t s = 0; s <= sectors; ++s) {
            const auto theta = static_cast<float>(s) / static_cast<float>(sectors) * 2.0f * 3.14159265359f;

            const auto x = std::sin(phi) * std::cos(theta);
            const auto y = std::cos(phi);
            const auto z = std::sin(phi) * std::sin(theta);

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z); // position
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z); // normal
        }
    }

    // Generate indices (CCW from outside)
    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < sectors; ++s) {
            const auto current = r * (sectors + 1) + s;
            const auto next    = current + sectors + 1;

            // Reversed winding
            indices.push_back(current);
            indices.push_back(current + 1);
            indices.push_back(next);

            indices.push_back(current + 1);
            indices.push_back(next + 1);
            indices.push_back(next);
        }
    }

    GeometryMesh mesh;
    mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
    mesh.index_count  = static_cast<uint32_t>(indices.size());
    mesh.topology     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    if (out_vertices) *out_vertices = vertices;
    if (out_indices) *out_indices = indices;

    create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
    create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

    return mesh;
}
vk::plugins::GeometryMesh vk::plugins::GeometryMesh::create_box_mesh(const context::EngineContext& eng, std::vector<float>* out_vertices, std::vector<uint32_t>* out_indices) {
    // clang-format off
    constexpr float vertices[] = {
        // positions          // normals
        // Front
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        // Back
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        // Top
        -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
        // Bottom
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
        // Right
         0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
         0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
         0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
        // Left
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
    };
    // clang-format on

    constexpr uint32_t indices[] = {
        0, 1, 2, 2, 3, 0, // front
        4, 5, 6, 6, 7, 4, // back
        8, 9, 10, 10, 11, 8, // top
        12, 13, 14, 14, 15, 12, // bottom
        16, 17, 18, 18, 19, 16, // right
        20, 21, 22, 22, 23, 20 // left
    };

    GeometryMesh mesh;
    mesh.vertex_count = 24;
    mesh.index_count  = 36;
    mesh.topology     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    if (out_vertices) {
        out_vertices->assign(std::begin(vertices), std::end(vertices));
    }
    if (out_indices) {
        out_indices->assign(std::begin(indices), std::end(indices));
    }

    create_buffer_with_data(eng, vertices, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
    create_buffer_with_data(eng, indices, sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

    return mesh;
}
vk::plugins::GeometryMesh vk::plugins::GeometryMesh::create_cylinder_mesh(const context::EngineContext& eng, uint32_t segments, std::vector<float>* out_vertices, std::vector<uint32_t>* out_indices) {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    // Generate side vertices (with radial normals)
    for (uint32_t i = 0; i <= segments; ++i) {
        const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
        const auto x     = std::cos(angle);
        const auto z     = std::sin(angle);

        // Bottom vertex
        vertices.push_back(x);
        vertices.push_back(-0.5f);
        vertices.push_back(z);
        vertices.push_back(x);
        vertices.push_back(0.0f);
        vertices.push_back(z); // radial normal

        // Top vertex
        vertices.push_back(x);
        vertices.push_back(0.5f);
        vertices.push_back(z);
        vertices.push_back(x);
        vertices.push_back(0.0f);
        vertices.push_back(z); // radial normal
    }

    const auto side_vertex_count = static_cast<uint32_t>(vertices.size() / 6);

    // Generate side indices (CCW from outside)
    for (uint32_t i = 0; i < segments; ++i) {
        const auto base = i * 2;
        // Reversed winding for correct CCW from outside
        indices.push_back(base); // bottom current
        indices.push_back(base + 1); // top current
        indices.push_back(base + 2); // bottom next

        indices.push_back(base + 2); // bottom next
        indices.push_back(base + 1); // top current
        indices.push_back(base + 3); // top next
    }

    // Add bottom cap center vertex
    const auto bottom_center_idx = side_vertex_count;
    vertices.push_back(0.0f);
    vertices.push_back(-0.5f);
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);
    vertices.push_back(-1.0f);
    vertices.push_back(0.0f); // down normal

    // Add bottom cap ring vertices
    for (uint32_t i = 0; i <= segments; ++i) {
        const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
        const auto x     = std::cos(angle);
        const auto z     = std::sin(angle);
        vertices.push_back(x);
        vertices.push_back(-0.5f);
        vertices.push_back(z);
        vertices.push_back(0.0f);
        vertices.push_back(-1.0f);
        vertices.push_back(0.0f); // down normal
    }

    // Generate bottom cap indices (CCW from outside = CW when viewed from below)
    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(bottom_center_idx);
        indices.push_back(bottom_center_idx + i + 1);
        indices.push_back(bottom_center_idx + i + 2);
    }

    // Add top cap center vertex
    const auto top_center_idx = static_cast<uint32_t>(vertices.size() / 6);
    vertices.push_back(0.0f);
    vertices.push_back(0.5f);
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);
    vertices.push_back(1.0f);
    vertices.push_back(0.0f); // up normal

    // Add top cap ring vertices
    for (uint32_t i = 0; i <= segments; ++i) {
        const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
        const auto x     = std::cos(angle);
        const auto z     = std::sin(angle);
        vertices.push_back(x);
        vertices.push_back(0.5f);
        vertices.push_back(z);
        vertices.push_back(0.0f);
        vertices.push_back(1.0f);
        vertices.push_back(0.0f); // up normal
    }

    // Generate top cap indices (CCW from outside)
    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(top_center_idx);
        indices.push_back(top_center_idx + i + 2);
        indices.push_back(top_center_idx + i + 1);
    }

    GeometryMesh mesh;
    mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
    mesh.index_count  = static_cast<uint32_t>(indices.size());
    mesh.topology     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    if (out_vertices) *out_vertices = vertices;
    if (out_indices) *out_indices = indices;

    create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
    create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

    return mesh;
}
vk::plugins::GeometryMesh vk::plugins::GeometryMesh::create_cone_mesh(const context::EngineContext& eng, uint32_t segments, std::vector<float>* out_vertices, std::vector<uint32_t>* out_indices) {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    // Apex
    vertices.push_back(0.0f);
    vertices.push_back(0.5f);
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);
    vertices.push_back(1.0f);
    vertices.push_back(0.0f);

    // Side vertices (base circle with radial normals)
    for (uint32_t i = 0; i <= segments; ++i) {
        const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
        const auto x     = std::cos(angle);
        const auto z     = std::sin(angle);

        // Calculate cone side normal (pointing outward and upward)
        const auto ny = 0.707f; // 45 degree slope
        const auto nr = 0.707f;
        vertices.push_back(x);
        vertices.push_back(-0.5f);
        vertices.push_back(z);
        vertices.push_back(x * nr);
        vertices.push_back(ny);
        vertices.push_back(z * nr);
    }

    // Generate side indices (CCW from outside)
    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(0); // apex
        indices.push_back(i + 2); // next base vertex
        indices.push_back(i + 1); // current base vertex
    }

    // Add base cap center
    const auto base_center_idx = static_cast<uint32_t>(vertices.size() / 6);
    vertices.push_back(0.0f);
    vertices.push_back(-0.5f);
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);
    vertices.push_back(-1.0f);
    vertices.push_back(0.0f);

    // Add base cap ring (with downward normals)
    for (uint32_t i = 0; i <= segments; ++i) {
        const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
        const auto x     = std::cos(angle);
        const auto z     = std::sin(angle);
        vertices.push_back(x);
        vertices.push_back(-0.5f);
        vertices.push_back(z);
        vertices.push_back(0.0f);
        vertices.push_back(-1.0f);
        vertices.push_back(0.0f);
    }

    // Generate base cap indices (CCW from outside)
    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(base_center_idx);
        indices.push_back(base_center_idx + i + 1);
        indices.push_back(base_center_idx + i + 2);
    }

    GeometryMesh mesh;
    mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
    mesh.index_count  = static_cast<uint32_t>(indices.size());
    mesh.topology     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    if (out_vertices) *out_vertices = vertices;
    if (out_indices) *out_indices = indices;

    create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
    create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

    return mesh;
}
vk::plugins::GeometryMesh vk::plugins::GeometryMesh::create_torus_mesh(const context::EngineContext& eng, uint32_t segments, uint32_t tube_segments, std::vector<float>* out_vertices, std::vector<uint32_t>* out_indices) {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    constexpr auto major_radius = 0.4f;
    constexpr auto minor_radius = 0.15f;

    for (uint32_t i = 0; i <= segments; ++i) {
        const auto u = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
        for (uint32_t j = 0; j <= tube_segments; ++j) {
            const auto v = static_cast<float>(j) / static_cast<float>(tube_segments) * 2.0f * 3.14159265359f;

            const auto x = (major_radius + minor_radius * std::cos(v)) * std::cos(u);
            const auto y = minor_radius * std::sin(v);
            const auto z = (major_radius + minor_radius * std::cos(v)) * std::sin(u);

            const auto nx = std::cos(v) * std::cos(u);
            const auto ny = std::sin(v);
            const auto nz = std::cos(v) * std::sin(u);

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            vertices.push_back(nx);
            vertices.push_back(ny);
            vertices.push_back(nz);
        }
    }

    for (uint32_t i = 0; i < segments; ++i) {
        for (uint32_t j = 0; j < tube_segments; ++j) {
            const auto a = i * (tube_segments + 1) + j;
            const auto b = a + tube_segments + 1;

            // Reversed winding
            indices.push_back(a);
            indices.push_back(a + 1);
            indices.push_back(b);

            indices.push_back(a + 1);
            indices.push_back(b + 1);
            indices.push_back(b);
        }
    }

    GeometryMesh mesh;
    mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
    mesh.index_count  = static_cast<uint32_t>(indices.size());
    mesh.topology     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    if (out_vertices) *out_vertices = vertices;
    if (out_indices) *out_indices = indices;

    create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
    create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

    return mesh;
}
vk::plugins::GeometryMesh vk::plugins::GeometryMesh::create_capsule_mesh(const context::EngineContext& eng, uint32_t segments, std::vector<float>* out_vertices, std::vector<uint32_t>* out_indices) {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    constexpr auto height = 0.5f; // Half-height of cylinder section
    constexpr auto radius = 0.25f;

    // Top hemisphere
    for (uint32_t r = 0; r <= segments / 2; ++r) {
        const auto phi = static_cast<float>(r) / static_cast<float>(segments / 2) * 3.14159265359f * 0.5f;
        for (uint32_t s = 0; s <= segments; ++s) {
            const auto theta = static_cast<float>(s) / static_cast<float>(segments) * 2.0f * 3.14159265359f;

            const auto x = radius * std::cos(phi) * std::cos(theta);
            const auto y = height + radius * std::sin(phi);
            const auto z = radius * std::cos(phi) * std::sin(theta);

            const auto nx = std::cos(phi) * std::cos(theta);
            const auto ny = std::sin(phi);
            const auto nz = std::cos(phi) * std::sin(theta);

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            vertices.push_back(nx);
            vertices.push_back(ny);
            vertices.push_back(nz);
        }
    }

    const auto top_hemisphere_count = static_cast<uint32_t>(vertices.size() / 6);

    // Generate top hemisphere indices
    for (uint32_t r = 0; r < segments / 2; ++r) {
        for (uint32_t s = 0; s < segments; ++s) {
            const auto current = r * (segments + 1) + s;
            const auto next    = current + segments + 1;

            // Reversed winding
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);

            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }

    // Cylinder middle section
    const auto cylinder_start_idx = static_cast<uint32_t>(vertices.size() / 6);
    for (uint32_t i = 0; i <= segments; ++i) {
        const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
        const auto x     = radius * std::cos(angle);
        const auto z     = radius * std::sin(angle);
        const auto nx    = std::cos(angle);
        const auto nz    = std::sin(angle);

        // Bottom of cylinder (at +height)
        vertices.push_back(x);
        vertices.push_back(height);
        vertices.push_back(z);
        vertices.push_back(nx);
        vertices.push_back(0.0f);
        vertices.push_back(nz);

        // Top of cylinder (at -height)
        vertices.push_back(x);
        vertices.push_back(-height);
        vertices.push_back(z);
        vertices.push_back(nx);
        vertices.push_back(0.0f);
        vertices.push_back(nz);
    }

    // Generate cylinder indices
    for (uint32_t i = 0; i < segments; ++i) {
        const auto base = cylinder_start_idx + i * 2;
        // Reversed winding
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 1);

        indices.push_back(base + 2);
        indices.push_back(base + 3);
        indices.push_back(base + 1);
    }

    // Bottom hemisphere
    const auto bottom_hemisphere_start = static_cast<uint32_t>(vertices.size() / 6);
    for (uint32_t r = 0; r <= segments / 2; ++r) {
        const auto phi = static_cast<float>(r) / static_cast<float>(segments / 2) * 3.14159265359f * 0.5f;
        for (uint32_t s = 0; s <= segments; ++s) {
            const auto theta = static_cast<float>(s) / static_cast<float>(segments) * 2.0f * 3.14159265359f;

            const auto x = radius * std::cos(phi) * std::cos(theta);
            const auto y = -height - radius * std::sin(phi);
            const auto z = radius * std::cos(phi) * std::sin(theta);

            const auto nx = std::cos(phi) * std::cos(theta);
            const auto ny = -std::sin(phi);
            const auto nz = std::cos(phi) * std::sin(theta);

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            vertices.push_back(nx);
            vertices.push_back(ny);
            vertices.push_back(nz);
        }
    }

    // Generate bottom hemisphere indices
    for (uint32_t r = 0; r < segments / 2; ++r) {
        for (uint32_t s = 0; s < segments; ++s) {
            const auto current = bottom_hemisphere_start + r * (segments + 1) + s;
            const auto next    = current + segments + 1;

            // Reversed winding
            indices.push_back(current);
            indices.push_back(current + 1);
            indices.push_back(next);

            indices.push_back(current + 1);
            indices.push_back(next + 1);
            indices.push_back(next);
        }
    }

    GeometryMesh mesh;
    mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
    mesh.index_count  = static_cast<uint32_t>(indices.size());
    mesh.topology     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    if (out_vertices) *out_vertices = vertices;
    if (out_indices) *out_indices = indices;

    create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
    create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

    return mesh;
}
vk::plugins::GeometryMesh vk::plugins::GeometryMesh::create_plane_mesh(const context::EngineContext& eng, std::vector<float>* out_vertices, std::vector<uint32_t>* out_indices) {
    // clang-format off
    constexpr float vertices[] = {
        // positions           // normals
        -0.5f, 0.0f, -0.5f,    0.0f, 1.0f, 0.0f, // vertex 0
         0.5f, 0.0f, -0.5f,    0.0f, 1.0f, 0.0f, // vertex 1
         0.5f, 0.0f,  0.5f,    0.0f, 1.0f, 0.0f, // vertex 2
        -0.5f, 0.0f,  0.5f,    0.0f, 1.0f, 0.0f, // vertex 3
    };
    // clang-format on

    constexpr uint32_t indices[] = {0, 2, 1, 2, 0, 3};

    GeometryMesh mesh;
    mesh.vertex_count = 4;
    mesh.index_count  = 6;
    mesh.topology     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    if (out_vertices) {
        out_vertices->assign(std::begin(vertices), std::end(vertices));
    }
    if (out_indices) {
        out_indices->assign(std::begin(indices), std::end(indices));
    }

    create_buffer_with_data(eng, vertices, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
    create_buffer_with_data(eng, indices, sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

    return mesh;
}
vk::plugins::GeometryMesh vk::plugins::GeometryMesh::create_circle_mesh(const context::EngineContext& eng, uint32_t segments, std::vector<float>* out_vertices, std::vector<uint32_t>* out_indices) {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;

    // Center
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);
    vertices.push_back(1.0f);
    vertices.push_back(0.0f);

    // Circle points
    for (uint32_t i = 0; i <= segments; ++i) {
        const auto angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * 3.14159265359f;
        const auto x     = std::cos(angle) * 0.5f;
        const auto z     = std::sin(angle) * 0.5f;

        vertices.push_back(x);
        vertices.push_back(0.0f);
        vertices.push_back(z);
        vertices.push_back(0.0f);
        vertices.push_back(1.0f);
        vertices.push_back(0.0f);
    }

    // Generate indices
    for (uint32_t i = 1; i <= segments; ++i) {
        indices.push_back(0);
        indices.push_back(i + 1);
        indices.push_back(i);
    }

    GeometryMesh mesh;
    mesh.vertex_count = static_cast<uint32_t>(vertices.size() / 6);
    mesh.index_count  = static_cast<uint32_t>(indices.size());
    mesh.topology     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    if (out_vertices) *out_vertices = vertices;
    if (out_indices) *out_indices = indices;

    create_buffer_with_data(eng, vertices.data(), vertices.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
    create_buffer_with_data(eng, indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

    return mesh;
}
vk::plugins::GeometryMesh vk::plugins::GeometryMesh::create_line_mesh(const context::EngineContext& eng) {
    // clang-format off
    constexpr float vertices[] = {
        // positions          // normals (unused for lines)
        -0.5f, 0.0f, 0.0f,    0.0f, 1.0f, 0.0f,
         0.5f, 0.0f, 0.0f,    0.0f, 1.0f, 0.0f,
    };
    // clang-format on

    constexpr uint32_t indices[] = {0, 1};

    GeometryMesh mesh;
    mesh.vertex_count = 2;
    mesh.index_count  = 2;
    mesh.topology     = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    create_buffer_with_data(eng, vertices, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
    create_buffer_with_data(eng, indices, sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

    return mesh;
}
vk::plugins::GeometryMesh vk::plugins::GeometryMesh::create_face_normal_mesh(const context::EngineContext& eng, const std::vector<float>& vertices, const std::vector<uint32_t>& indices, float normal_length) {

    constexpr uint32_t stride = 6; // position + normal
    if (vertices.size() < stride || indices.size() < 3) {
        return {};
    }

    std::vector<float> line_vertices;
    std::vector<uint32_t> line_indices;
    line_vertices.reserve(indices.size() * 4); // rough guess
    line_indices.reserve(indices.size() * 2);

    const auto normalize = [](const context::Vec3& v) {
        const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        return len > 0.0f ? v / len : context::Vec3{0, 1, 0};
    };

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const auto i0 = static_cast<size_t>(indices[i]);
        const auto i1 = static_cast<size_t>(indices[i + 1]);
        const auto i2 = static_cast<size_t>(indices[i + 2]);
        if ((i0 + 1) * stride > vertices.size() || (i1 + 1) * stride > vertices.size() || (i2 + 1) * stride > vertices.size()) {
            continue;
        }

        const context::Vec3 p0{vertices[i0 * stride + 0], vertices[i0 * stride + 1], vertices[i0 * stride + 2]};
        const context::Vec3 p1{vertices[i1 * stride + 0], vertices[i1 * stride + 1], vertices[i1 * stride + 2]};
        const context::Vec3 p2{vertices[i2 * stride + 0], vertices[i2 * stride + 1], vertices[i2 * stride + 2]};

        const auto edge1  = p1 - p0;
        const auto edge2  = p2 - p0;
        const auto normal = normalize(edge1.cross(edge2));
        const auto center = (p0 + p1 + p2) / 3.0f;

        const auto start = center;
        const auto end   = center + normal * normal_length;

        const uint32_t base_index = static_cast<uint32_t>(line_vertices.size() / stride);

        // start
        line_vertices.push_back(start.x);
        line_vertices.push_back(start.y);
        line_vertices.push_back(start.z);
        line_vertices.push_back(normal.x);
        line_vertices.push_back(normal.y);
        line_vertices.push_back(normal.z);
        // end
        line_vertices.push_back(end.x);
        line_vertices.push_back(end.y);
        line_vertices.push_back(end.z);
        line_vertices.push_back(normal.x);
        line_vertices.push_back(normal.y);
        line_vertices.push_back(normal.z);

        line_indices.push_back(base_index);
        line_indices.push_back(base_index + 1);
    }

    if (line_vertices.empty()) {
        return {};
    }

    GeometryMesh mesh;
    mesh.vertex_count = static_cast<uint32_t>(line_vertices.size() / stride);
    mesh.index_count  = static_cast<uint32_t>(line_indices.size());
    mesh.topology     = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    create_buffer_with_data(eng, line_vertices.data(), line_vertices.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertex_buffer, mesh.vertex_allocation);
    create_buffer_with_data(eng, line_indices.data(), line_indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.index_buffer, mesh.index_allocation);

    return mesh;
}

void vk::plugins::Geometry::on_setup(const context::PluginContext& ctx) {
    if (!ctx.caps) return;

    ctx.caps->uses_depth = VK_TRUE;
    if (!ctx.caps->depth_attachment) {
        ctx.caps->depth_attachment = context::AttachmentRequest{
            .name           = "depth",
            .format         = ctx.caps->preferred_depth_format,
            .usage          = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .samples        = ctx.caps->color_samples,
            .aspect         = VK_IMAGE_ASPECT_DEPTH_BIT,
            .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
    } else {
        ctx.caps->depth_attachment->usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (ctx.caps->depth_attachment->aspect == 0) {
            ctx.caps->depth_attachment->aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        }
    }
}
void vk::plugins::Geometry::on_initialize(context::PluginContext& ctx) {
    if (!ctx.frame || ctx.frame->color_attachments.empty()) {
        throw std::runtime_error("GeometryPlugin requires at least one color attachment");
    }

    color_format_ = ctx.frame->color_attachments.front().format;
    depth_format_ = ctx.frame->depth_attachment ? ctx.frame->depth_attachment->format : VK_FORMAT_UNDEFINED;
    depth_layout_ = ctx.frame->depth_attachment ? ctx.frame->depth_attachment->current_layout : VK_IMAGE_LAYOUT_UNDEFINED;

    create_pipelines(*ctx.engine, color_format_, depth_format_);
    create_geometry_meshes(*ctx.engine);
}
void vk::plugins::Geometry::on_pre_render(context::PluginContext& ctx) {
    if (batches_.empty()) return;
    this->update_instance_buffers(*ctx.engine);
}
void vk::plugins::Geometry::on_render(context::PluginContext& ctx) {
    if (batches_.empty()) return;
    if (!ctx.frame || ctx.frame->color_attachments.empty() || !ctx.cmd) return;

    auto& cmd          = *ctx.cmd;
    const auto& target = ctx.frame->color_attachments.front();

    context::transition_image_layout(cmd, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    constexpr VkClearValue clear_value{.color = {{0.f, 0.f, 0.f, 1.0f}}};
    VkRenderingAttachmentInfo color_attachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = target.view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = clear_value};

    VkRenderingAttachmentInfo depth_attachment{};
    bool has_depth = ctx.frame->depth_attachment && ctx.frame->depth_attachment->view != VK_NULL_HANDLE;
    if (has_depth) {
        const auto* depth = ctx.frame->depth_attachment;
        const VkImageMemoryBarrier2 depth_barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask                                = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask                               = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask                                = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask                               = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout                                   = depth_layout_,
            .newLayout                                   = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .image                                       = depth->image,
            .subresourceRange                            = {depth->aspect, 0, 1, 0, 1}};

        const VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &depth_barrier};

        vkCmdPipelineBarrier2(cmd, &dep);
        depth_layout_ = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

        depth_attachment = VkRenderingAttachmentInfo{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = depth->view, .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = {.depthStencil = {1.0f, 0}}};
    }

    const VkRenderingInfo rendering_info{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, ctx.frame->extent}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color_attachment, .pDepthAttachment = has_depth ? &depth_attachment : nullptr};

    vkCmdBeginRendering(cmd, &rendering_info);

    const VkViewport viewport{.x = 0.0f, .y = 0.0f, .width = static_cast<float>(ctx.frame->extent.width), .height = static_cast<float>(ctx.frame->extent.height), .minDepth = 0.0f, .maxDepth = 1.0f};
    const VkRect2D scissor{{0, 0}, ctx.frame->extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const auto view_proj = camera_->proj_matrix() * camera_->view_matrix();

    // Render all batches with instancing
    for (size_t i = 0; i < batches_.size(); ++i) {
        if (!batches_[i].instances.empty() && i < instance_buffers_.size()) {
            render_batch(cmd, batches_[i], instance_buffers_[i], view_proj);
        }
    }

    vkCmdEndRendering(cmd);

    context::transition_image_layout(cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
}
void vk::plugins::Geometry::on_cleanup(context::PluginContext& ctx) {
    if (ctx.engine) {
        vkDeviceWaitIdle(ctx.engine->device);
        destroy_geometry_meshes(*ctx.engine);
        destroy_pipelines(*ctx.engine);
    }
}
void vk::plugins::Geometry::on_resize(uint32_t, uint32_t) {
    depth_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    color_format_ = VK_FORMAT_UNDEFINED;
    depth_format_ = VK_FORMAT_UNDEFINED;
}
void vk::plugins::Geometry::create_pipelines(const context::EngineContext& eng, VkFormat color_format, VkFormat depth_format) {
    // Load shaders
    auto load_shader = [&](const char* filename) -> VkShaderModule {
        std::ifstream file(std::string("shader/") + filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error(std::format("Failed to open shader file: {}", filename));
        }

        const size_t file_size = file.tellg();
        std::vector<char> code(file_size);
        file.seekg(0);
        file.read(code.data(), static_cast<std::streamsize>(file_size));

        const VkShaderModuleCreateInfo create_info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = code.size(), .pCode = reinterpret_cast<const uint32_t*>(code.data())};

        VkShaderModule module = VK_NULL_HANDLE;
        context::vk_check(vkCreateShaderModule(eng.device, &create_info, nullptr, &module), "Failed to create shader module");
        return module;
    };

    const auto vert_module = load_shader("geometry.vert.spv");
    const auto frag_module = load_shader("geometry.frag.spv");

    // Push constant for MVP matrix
    const VkPushConstantRange push_constant{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(float) * 16};

    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1, .pPushConstantRanges = &push_constant};

    context::vk_check(vkCreatePipelineLayout(eng.device, &layout_info, nullptr, &pipeline_layout_), "Failed to create geometry pipeline layout");

    const VkPipelineShaderStageCreateInfo vert_stage{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main"};

    const VkPipelineShaderStageCreateInfo frag_stage{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main"};

    const VkPipelineShaderStageCreateInfo filled_stages[] = {vert_stage, frag_stage};
    const VkPipelineShaderStageCreateInfo wire_stages[]   = {vert_stage, frag_stage};
    const VkPipelineShaderStageCreateInfo line_stages[]   = {vert_stage, frag_stage};

    // Vertex input bindings (per-vertex and per-instance)
    const VkVertexInputBindingDescription bindings[] = {
        {0, sizeof(float) * 6, VK_VERTEX_INPUT_RATE_VERTEX}, // position + normal
        {1, sizeof(float) * 13, VK_VERTEX_INPUT_RATE_INSTANCE} // instance data
    };

    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, // position
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3}, // normal
        {2, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, // instance position
        {3, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3}, // instance rotation
        {4, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 6}, // instance scale
        {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 9} // instance color + alpha
    };

    const VkPipelineVertexInputStateCreateInfo vertex_input{.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = bindings, .vertexAttributeDescriptionCount = 6, .pVertexAttributeDescriptions = attributes};

    const VkPipelineInputAssemblyStateCreateInfo input_assembly{.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};

    const VkPipelineViewportStateCreateInfo viewport_state{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};

    const VkPipelineRasterizationStateCreateInfo rasterizer{.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode                                               = VK_POLYGON_MODE_FILL,
        .cullMode                                                  = VK_CULL_MODE_NONE, // Disable culling to see both front and back faces
        .frontFace                                                 = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth                                                 = 1.0f};

    const VkPipelineMultisampleStateCreateInfo multisampling{.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

    const bool has_depth = depth_format != VK_FORMAT_UNDEFINED;
    const VkPipelineDepthStencilStateCreateInfo depth_stencil{.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = has_depth ? VK_TRUE : VK_FALSE, .depthWriteEnable = has_depth ? VK_TRUE : VK_FALSE, .depthCompareOp = VK_COMPARE_OP_LESS};

    const VkPipelineColorBlendAttachmentState color_blend_attachment{.blendEnable = VK_TRUE,
        .srcColorBlendFactor                                                      = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor                                                      = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp                                                             = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor                                                      = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor                                                      = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp                                                             = VK_BLEND_OP_ADD,
        .colorWriteMask                                                           = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};

    const VkPipelineColorBlendStateCreateInfo color_blending{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &color_blend_attachment};

    constexpr VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic_state{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamic_states};

    const VkPipelineRenderingCreateInfo rendering_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1, .pColorAttachmentFormats = &color_format, .depthAttachmentFormat = depth_format};

    const VkGraphicsPipelineCreateInfo pipeline_info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext                                              = &rendering_info,
        .stageCount                                         = 2,
        .pStages                                            = filled_stages,
        .pVertexInputState                                  = &vertex_input,
        .pInputAssemblyState                                = &input_assembly,
        .pViewportState                                     = &viewport_state,
        .pRasterizationState                                = &rasterizer,
        .pMultisampleState                                  = &multisampling,
        .pDepthStencilState                                 = &depth_stencil,
        .pColorBlendState                                   = &color_blending,
        .pDynamicState                                      = &dynamic_state,
        .layout                                             = pipeline_layout_};

    context::vk_check(vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &filled_pipeline_), "Failed to create filled geometry pipeline");

    // Create wireframe pipeline
    auto wireframe_rasterizer        = rasterizer;
    wireframe_rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    wireframe_rasterizer.cullMode    = VK_CULL_MODE_NONE;

    auto wireframe_pipeline_info                = pipeline_info;
    wireframe_pipeline_info.pRasterizationState = &wireframe_rasterizer;
    wireframe_pipeline_info.pStages             = wire_stages;

    context::vk_check(vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &wireframe_pipeline_info, nullptr, &wireframe_pipeline_), "Failed to create wireframe geometry pipeline");

    // Create line pipeline for LINE_LIST topology
    const VkPipelineInputAssemblyStateCreateInfo line_input_assembly{.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST};

    auto line_rasterizer        = rasterizer;
    line_rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    line_rasterizer.cullMode    = VK_CULL_MODE_NONE;
    line_rasterizer.lineWidth   = 2.0f;

    auto line_pipeline_info                = pipeline_info;
    line_pipeline_info.pInputAssemblyState = &line_input_assembly;
    line_pipeline_info.pRasterizationState = &line_rasterizer;
    line_pipeline_info.pStages             = line_stages;

    context::vk_check(vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &line_pipeline_info, nullptr, &line_pipeline_), "Failed to create line geometry pipeline");

    vkDestroyShaderModule(eng.device, vert_module, nullptr);
    vkDestroyShaderModule(eng.device, frag_module, nullptr);
}
void vk::plugins::Geometry::destroy_pipelines(const context::EngineContext& eng) {
    if (filled_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(eng.device, filled_pipeline_, nullptr);
        filled_pipeline_ = VK_NULL_HANDLE;
    }
    if (wireframe_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(eng.device, wireframe_pipeline_, nullptr);
        wireframe_pipeline_ = VK_NULL_HANDLE;
    }
    if (line_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(eng.device, line_pipeline_, nullptr);
        line_pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(eng.device, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }
}
void vk::plugins::Geometry::create_geometry_meshes(const context::EngineContext& eng) {
    {
        std::vector<float> v;
        std::vector<uint32_t> i;
        geometry_meshes_[GeometryType::Sphere] = GeometryMesh::create_sphere_mesh(eng, 32, &v, &i);
        normal_meshes_[GeometryType::Sphere]   = GeometryMesh::create_face_normal_mesh(eng, v, i);
    }
    {
        std::vector<float> v;
        std::vector<uint32_t> i;
        geometry_meshes_[GeometryType::Box] = GeometryMesh::create_box_mesh(eng, &v, &i);
        normal_meshes_[GeometryType::Box]   = GeometryMesh::create_face_normal_mesh(eng, v, i);
    }
    {
        std::vector<float> v;
        std::vector<uint32_t> i;
        geometry_meshes_[GeometryType::Cylinder] = GeometryMesh::create_cylinder_mesh(eng, 32, &v, &i);
        normal_meshes_[GeometryType::Cylinder]   = GeometryMesh::create_face_normal_mesh(eng, v, i);
    }
    {
        std::vector<float> v;
        std::vector<uint32_t> i;
        geometry_meshes_[GeometryType::Cone] = GeometryMesh::create_cone_mesh(eng, 32, &v, &i);
        normal_meshes_[GeometryType::Cone]   = GeometryMesh::create_face_normal_mesh(eng, v, i);
    }
    {
        std::vector<float> v;
        std::vector<uint32_t> i;
        geometry_meshes_[GeometryType::Torus] = GeometryMesh::create_torus_mesh(eng, 32, 16, &v, &i);
        normal_meshes_[GeometryType::Torus]   = GeometryMesh::create_face_normal_mesh(eng, v, i);
    }
    {
        std::vector<float> v;
        std::vector<uint32_t> i;
        geometry_meshes_[GeometryType::Capsule] = GeometryMesh::create_capsule_mesh(eng, 16, &v, &i);
        normal_meshes_[GeometryType::Capsule]   = GeometryMesh::create_face_normal_mesh(eng, v, i);
    }
    {
        std::vector<float> v;
        std::vector<uint32_t> i;
        geometry_meshes_[GeometryType::Plane] = GeometryMesh::create_plane_mesh(eng, &v, &i);
        normal_meshes_[GeometryType::Plane]   = GeometryMesh::create_face_normal_mesh(eng, v, i);
    }
    {
        std::vector<float> v;
        std::vector<uint32_t> i;
        geometry_meshes_[GeometryType::Circle] = GeometryMesh::create_circle_mesh(eng, 32, &v, &i);
        normal_meshes_[GeometryType::Circle]   = GeometryMesh::create_face_normal_mesh(eng, v, i);
    }
    geometry_meshes_[GeometryType::Line] = GeometryMesh::create_line_mesh(eng);
    geometry_meshes_[GeometryType::Grid] = GeometryMesh::create_line_mesh(eng);
    geometry_meshes_[GeometryType::Ray]  = GeometryMesh::create_line_mesh(eng);
}
void vk::plugins::Geometry::destroy_geometry_meshes(const context::EngineContext& eng) {
    for (auto& mesh : geometry_meshes_ | std::views::values) {
        if (mesh.vertex_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(eng.allocator, mesh.vertex_buffer, mesh.vertex_allocation);
            mesh.vertex_buffer = VK_NULL_HANDLE;
        }
        if (mesh.index_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(eng.allocator, mesh.index_buffer, mesh.index_allocation);
            mesh.index_buffer = VK_NULL_HANDLE;
        }
    }
    geometry_meshes_.clear();

    for (auto& mesh : normal_meshes_ | std::views::values) {
        if (mesh.vertex_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(eng.allocator, mesh.vertex_buffer, mesh.vertex_allocation);
            mesh.vertex_buffer = VK_NULL_HANDLE;
        }
        if (mesh.index_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(eng.allocator, mesh.index_buffer, mesh.index_allocation);
            mesh.index_buffer = VK_NULL_HANDLE;
        }
    }
    normal_meshes_.clear();

    for (size_t i = 0; i < instance_buffers_.size(); ++i) {
        if (const auto& inst_buf = instance_buffers_[i]; inst_buf.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(eng.allocator, inst_buf.buffer, inst_buf.allocation);
        }
    }
    instance_buffers_.clear();
}
void vk::plugins::Geometry::update_instance_buffers(const context::EngineContext& eng) {
    // Resize instance buffers vector if needed
    if (instance_buffers_.size() < batches_.size()) {
        instance_buffers_.resize(batches_.size());
    }

    for (size_t i = 0; i < batches_.size(); ++i) {
        const auto& batch = batches_[i];
        auto& [buffer, allocation, capacity]    = instance_buffers_[i];

        if (batch.instances.empty()) continue;

        const uint32_t required_capacity = static_cast<uint32_t>(batch.instances.size());
        const VkDeviceSize buffer_size   = static_cast<VkDeviceSize>(required_capacity) * sizeof(GeometryInstance);

        // Reallocate if needed
        if (capacity < required_capacity) {
            if (buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(eng.allocator, buffer, allocation);
            }

            const VkBufferCreateInfo buffer_ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = buffer_size, .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

            constexpr VmaAllocationCreateInfo alloc_ci{.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO};

            VmaAllocationInfo alloc_info{};
            context::vk_check(vmaCreateBuffer(eng.allocator, &buffer_ci, &alloc_ci, &buffer, &allocation, &alloc_info), "Failed to create instance buffer");

            capacity = required_capacity;
        }

        // Update buffer data
        void* data = nullptr;
        vmaMapMemory(eng.allocator, allocation, &data);
        std::memcpy(data, batch.instances.data(), buffer_size);
        vmaUnmapMemory(eng.allocator, allocation);
    }
}
void vk::plugins::Geometry::render_batch(VkCommandBuffer& cmd, const GeometryBatch& batch, const InstanceBuffer& instance_buffer, const context::Mat4& view_proj) {
    if (batch.instances.empty()) return;

    const auto mesh_it = geometry_meshes_.find(batch.type);
    if (mesh_it == geometry_meshes_.end()) return;

    const auto& mesh = mesh_it->second;

    // Choose pipeline based on geometry type and render mode
    VkPipeline pipeline = filled_pipeline_;
    if (batch.type == GeometryType::Line || batch.type == GeometryType::Ray || batch.type == GeometryType::Grid) {
        pipeline = line_pipeline_;
    } else if (batch.mode == RenderMode::Wireframe) {
        pipeline = wireframe_pipeline_;
    }

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Push constants (view-proj matrix)
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, view_proj.m.data());

    // Bind vertex buffers
    const VkBuffer vertex_buffers[] = {mesh.vertex_buffer, instance_buffer.buffer};
    const VkDeviceSize offsets[]    = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);

    // Bind index buffer and draw
    if (mesh.index_buffer != VK_NULL_HANDLE) {
        vkCmdBindIndexBuffer(cmd, mesh.index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh.index_count, static_cast<uint32_t>(batch.instances.size()), 0, 0, 0);
    } else {
        vkCmdDraw(cmd, mesh.vertex_count, static_cast<uint32_t>(batch.instances.size()), 0, 0);
    }

    // If RenderMode::Both, render wireframe on top (only for non-line geometries)
    if (batch.mode == RenderMode::Both && batch.type != GeometryType::Line && batch.type != GeometryType::Ray && batch.type != GeometryType::Grid) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe_pipeline_);
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, view_proj.m.data());
        vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);

        if (mesh.index_buffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(cmd, mesh.index_buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh.index_count, static_cast<uint32_t>(batch.instances.size()), 0, 0, 0);
        } else {
            vkCmdDraw(cmd, mesh.vertex_count, static_cast<uint32_t>(batch.instances.size()), 0, 0);
        }
    }

    // Optional face-normal visualization overlay
    if (show_face_normals_ && batch.type != GeometryType::Line && batch.type != GeometryType::Ray && batch.type != GeometryType::Grid) {
        const auto n_it = normal_meshes_.find(batch.type);
        if (n_it != normal_meshes_.end()) {
            const auto& nmesh = n_it->second;
            if (nmesh.vertex_buffer != VK_NULL_HANDLE) {
                const VkBuffer normal_vertex_buffers[] = {nmesh.vertex_buffer, instance_buffer.buffer};
                const VkDeviceSize normal_offsets[]    = {0, 0};
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline_);
                vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, view_proj.m.data());
                vkCmdBindVertexBuffers(cmd, 0, 2, normal_vertex_buffers, normal_offsets);

                if (nmesh.index_buffer != VK_NULL_HANDLE) {
                    vkCmdBindIndexBuffer(cmd, nmesh.index_buffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cmd, nmesh.index_count, static_cast<uint32_t>(batch.instances.size()), 0, 0, 0);
                } else {
                    vkCmdDraw(cmd, nmesh.vertex_count, static_cast<uint32_t>(batch.instances.size()), 0, 0);
                }
            }
        }
    }
}
