module;
#include <vulkan/vulkan_raii.hpp>
export module vk.pipeline;
import vk.memory;
import vk.geometry;
import std;


namespace vk::pipeline {
    export struct GraphicsPipeline {
        raii::PipelineLayout layout{nullptr};
        raii::Pipeline pipeline{nullptr};
    };

    export struct GraphicsPipelineDesc {
        Format color_format{};
        Format depth_format{};
        bool use_depth{false};

        PrimitiveTopology topology{PrimitiveTopology::eTriangleList};
        CullModeFlags cull{CullModeFlagBits::eBack};
        FrontFace front_face{FrontFace::eCounterClockwise};

        PolygonMode polygon_mode{PolygonMode::eFill};

        bool enable_blend{false};

        uint32_t push_constant_bytes{0};
        ShaderStageFlags push_constant_stages{ShaderStageFlagBits::eVertex | ShaderStageFlagBits::eFragment};
    };

    export struct VertexInput {
        VertexInputBindingDescription binding{};
        std::vector<VertexInputAttributeDescription> attributes;
    };

    export template <typename VertexT>
    [[nodiscard]] VertexInput make_vertex_input();

    export [[nodiscard]]
    std::vector<std::byte> read_file_bytes(const std::string& path);

    export [[nodiscard]]
    raii::ShaderModule load_shader_module(const raii::Device& device, std::span<const std::byte> spv);

    export [[nodiscard]]
    GraphicsPipeline create_graphics_pipeline(const raii::Device& device, const VertexInput& vin, const GraphicsPipelineDesc& desc, const raii::ShaderModule& shader_module, const char* vs_entry, const char* fs_entry);
} // namespace vk::pipeline


template <>
vk::pipeline::VertexInput vk::pipeline::make_vertex_input<vk::geometry::VertexP2C4>() {
    using V = geometry::VertexP2C4;

    VertexInput out{};
    out.binding = VertexInputBindingDescription{
        .binding   = 0,
        .stride    = sizeof(V),
        .inputRate = VertexInputRate::eVertex,
    };

    out.attributes = {
        VertexInputAttributeDescription{.location = 0, .binding = 0, .format = Format::eR32G32Sfloat, .offset = static_cast<uint32_t>(offsetof(V, position))},
        VertexInputAttributeDescription{.location = 1, .binding = 0, .format = Format::eR32G32B32A32Sfloat, .offset = static_cast<uint32_t>(offsetof(V, color))},
    };
    return out;
}

template <>
vk::pipeline::VertexInput vk::pipeline::make_vertex_input<vk::geometry::VertexP3C4>() {
    using V = geometry::VertexP3C4;

    VertexInput out{};
    out.binding = VertexInputBindingDescription{
        .binding   = 0,
        .stride    = sizeof(V),
        .inputRate = VertexInputRate::eVertex,
    };

    out.attributes = {
        VertexInputAttributeDescription{
            .location = 0,
            .binding  = 0,
            .format   = Format::eR32G32B32Sfloat,
            .offset   = static_cast<uint32_t>(offsetof(V, position)),
        },
        VertexInputAttributeDescription{
            .location = 1,
            .binding  = 0,
            .format   = Format::eR32G32B32A32Sfloat,
            .offset   = static_cast<uint32_t>(offsetof(V, color)),
        },
    };
    return out;
}

template <>
vk::pipeline::VertexInput vk::pipeline::make_vertex_input<vk::geometry::Vertex>() {
    using V = geometry::Vertex;

    VertexInput out{};
    out.binding = VertexInputBindingDescription{
        .binding   = 0,
        .stride    = sizeof(V),
        .inputRate = VertexInputRate::eVertex,
    };

    out.attributes = {
        VertexInputAttributeDescription{
            .location = 0,
            .binding  = 0,
            .format   = Format::eR32G32B32Sfloat,
            .offset   = static_cast<uint32_t>(offsetof(V, position)),
        },
        VertexInputAttributeDescription{
            .location = 1,
            .binding  = 0,
            .format   = Format::eR32G32B32Sfloat,
            .offset   = static_cast<uint32_t>(offsetof(V, normal)),
        },
        VertexInputAttributeDescription{
            .location = 2,
            .binding  = 0,
            .format   = Format::eR32G32Sfloat,
            .offset   = static_cast<uint32_t>(offsetof(V, uv)),
        },
        VertexInputAttributeDescription{
            .location = 3,
            .binding  = 0,
            .format   = Format::eR32G32B32A32Sfloat,
            .offset   = static_cast<uint32_t>(offsetof(V, color)),
        },
    };
    return out;
}
