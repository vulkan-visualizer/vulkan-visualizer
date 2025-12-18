module;
#include <vulkan/vulkan_raii.hpp>
module vk.pipeline;
import std;

namespace {

    [[nodiscard]]
    vk::PipelineColorBlendAttachmentState make_blend_attachment(bool enable) {
        if (enable) {
            return {
                .blendEnable         = VK_TRUE,
                .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
                .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
                .colorBlendOp        = vk::BlendOp::eAdd,
                .srcAlphaBlendFactor = vk::BlendFactor::eOne,
                .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
                .alphaBlendOp        = vk::BlendOp::eAdd,
                .colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
            };
        }

        return {
            .blendEnable    = VK_FALSE,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
    }

    [[nodiscard]]
    bool has_stencil(vk::Format fmt) {
        return fmt == vk::Format::eD32SfloatS8Uint || fmt == vk::Format::eD24UnormS8Uint;
    }
} // namespace

std::vector<std::byte> vk::pipeline::read_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("Failed to open file: " + path);

    const auto size = static_cast<size_t>(f.tellg());
    std::vector<std::byte> data(size);

    f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    return data;
}

vk::raii::ShaderModule vk::pipeline::load_shader_module(const raii::Device& device, std::span<const std::byte> spv) {
    if (spv.size_bytes() % 4 != 0) throw std::runtime_error("SPIR-V size must be multiple of 4");

    const ShaderModuleCreateInfo ci{
        .codeSize = spv.size_bytes(),
        .pCode    = reinterpret_cast<const uint32_t*>(spv.data()),
    };
    return {device, ci};
}

vk::pipeline::GraphicsPipeline vk::pipeline::create_graphics_pipeline(const raii::Device& device, const VertexInput& vin, const GraphicsPipelineDesc& desc, const raii::ShaderModule& shader_module, const char* vs_entry, const char* fs_entry) {
    GraphicsPipeline out{};

    /* ------------------------------------------------------------ */
    /* Pipeline layout (descriptor sets + push constants)           */
    /* ------------------------------------------------------------ */

    std::vector<PushConstantRange> push_ranges;
    if (desc.push_constant_bytes > 0) {
        push_ranges.push_back({
            .stageFlags = desc.push_constant_stages,
            .offset     = 0,
            .size       = desc.push_constant_bytes,
        });
    }

    const PipelineLayoutCreateInfo pl_ci{
        .setLayoutCount         = static_cast<uint32_t>(desc.set_layouts.size()),
        .pSetLayouts            = desc.set_layouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(push_ranges.size()),
        .pPushConstantRanges    = push_ranges.data(),
    };

    out.layout = raii::PipelineLayout{device, pl_ci};

    /* ------------------------------------------------------------ */
    /* Shader stages                                                */
    /* ------------------------------------------------------------ */

    const std::array<PipelineShaderStageCreateInfo, 2> stages{{
        {
            .stage  = ShaderStageFlagBits::eVertex,
            .module = *shader_module,
            .pName  = vs_entry,
        },
        {
            .stage  = ShaderStageFlagBits::eFragment,
            .module = *shader_module,
            .pName  = fs_entry,
        },
    }};

    /* ------------------------------------------------------------ */
    /* Fixed-function states                                        */
    /* ------------------------------------------------------------ */

    const PipelineVertexInputStateCreateInfo vi{
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &vin.binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vin.attributes.size()),
        .pVertexAttributeDescriptions    = vin.attributes.data(),
    };

    const PipelineInputAssemblyStateCreateInfo ia{
        .topology = desc.topology,
    };

    const PipelineViewportStateCreateInfo vp{
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    const PipelineRasterizationStateCreateInfo rs{
        .polygonMode = desc.polygon_mode,
        .cullMode    = desc.cull,
        .frontFace   = desc.front_face,
        .lineWidth   = 1.0f,
    };

    const PipelineMultisampleStateCreateInfo ms{
        .rasterizationSamples = SampleCountFlagBits::e1,
    };

    PipelineDepthStencilStateCreateInfo ds{};
    if (desc.use_depth) {
        ds = {
            .depthTestEnable  = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp   = CompareOp::eLessOrEqual,
        };
    }

    const auto blend_att = make_blend_attachment(desc.enable_blend);
    const PipelineColorBlendStateCreateInfo cb{
        .attachmentCount = 1,
        .pAttachments    = &blend_att,
    };

    constexpr DynamicState dyn_states[] = {
        DynamicState::eViewport,
        DynamicState::eScissor,
    };
    const PipelineDynamicStateCreateInfo dyn{
        .dynamicStateCount = static_cast<uint32_t>(std::size(dyn_states)),
        .pDynamicStates    = dyn_states,
    };

    /* ------------------------------------------------------------ */
    /* Dynamic rendering                                            */
    /* ------------------------------------------------------------ */

    PipelineRenderingCreateInfo rendering{
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &desc.color_format,
    };

    if (desc.use_depth) {
        rendering.depthAttachmentFormat = desc.depth_format;
        if (has_stencil(desc.depth_format)) {
            rendering.stencilAttachmentFormat = desc.depth_format;
        }
    }

    /* ------------------------------------------------------------ */
    /* Pipeline creation                                            */
    /* ------------------------------------------------------------ */

    const GraphicsPipelineCreateInfo gpi{
        .pNext               = &rendering,
        .stageCount          = static_cast<uint32_t>(stages.size()),
        .pStages             = stages.data(),
        .pVertexInputState   = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState      = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState   = &ms,
        .pDepthStencilState  = desc.use_depth ? &ds : nullptr,
        .pColorBlendState    = &cb,
        .pDynamicState       = &dyn,
        .layout              = *out.layout,
    };

    out.pipeline = raii::Pipeline{device, nullptr, gpi};
    return out;
}
