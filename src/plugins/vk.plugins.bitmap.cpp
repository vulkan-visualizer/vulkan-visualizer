module;
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
module vk.plugins.bitmap;
import vk.context;
import vk.toolkit.vulkan;
import vk.toolkit.math;

void vk::plugins::BitmapViewer::on_setup(const context::PluginContext& ctx) {
    ctx.caps->uses_depth       = VK_TRUE;
    ctx.caps->depth_attachment = vk::context::AttachmentRequest{
        .name           = "depth",
        .format         = VK_FORMAT_D32_SFLOAT,
        .usage          = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .aspect         = VK_IMAGE_ASPECT_DEPTH_BIT,
        .initial_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
    };
}
void vk::plugins::BitmapViewer::on_initialize(context::PluginContext& ctx) {
    this->create_geometry_buffers(*ctx.engine);
    this->create_pipeline(*ctx.engine, ctx.frame->color_attachments.front().format);
}
void vk::plugins::BitmapViewer::on_pre_render(context::PluginContext& ctx) {}
void vk::plugins::BitmapViewer::on_render(context::PluginContext& ctx) {}
void vk::plugins::BitmapViewer::on_post_render(context::PluginContext& ctx) {}
void vk::plugins::BitmapViewer::on_imgui(context::PluginContext& ctx) {}
void vk::plugins::BitmapViewer::on_present(context::PluginContext&) {}
void vk::plugins::BitmapViewer::on_cleanup(context::PluginContext& ctx) {
    this->destroy_pipeline(*ctx.engine);
    this->destroy_geometry_buffers(*ctx.engine);
}
void vk::plugins::BitmapViewer::on_event(const SDL_Event& event) {}
void vk::plugins::BitmapViewer::on_resize(uint32_t width, uint32_t height) {}

void vk::plugins::BitmapViewer::create_pipeline(const context::EngineContext& eng, VkFormat color_format) {
    const auto vert_module = toolkit::vulkan::load_shader("shader/bitfield.vert.spv", eng.device);
    const auto frag_module = toolkit::vulkan::load_shader("shader/bitfield.frag.spv", eng.device);

    // Pipeline layout
    VkPushConstantRange push_constant{};
    push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant.offset     = 0;
    push_constant.size       = sizeof(toolkit::math::Mat4);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push_constant;

    vkCreatePipelineLayout(eng.device, &layout_info, nullptr, &pipeline_layout_);

    // Vertex input for solid/transparent
    VkVertexInputBindingDescription bindings[2] = {};
    bindings[0].binding                         = 0;
    bindings[0].stride                          = 6 * sizeof(float);
    bindings[0].inputRate                       = VK_VERTEX_INPUT_RATE_VERTEX;

    bindings[1].binding   = 1;
    bindings[1].stride    = sizeof(vk::toolkit::math::Vec3) + sizeof(float); // position + occupied flag
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attributes[3] = {};
    attributes[0].location                          = 0;
    attributes[0].binding                           = 0;
    attributes[0].format                            = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset                            = 0;

    attributes[1].location = 1;
    attributes[1].binding  = 0;
    attributes[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset   = 3 * sizeof(float);

    attributes[2].location = 2;
    attributes[2].binding  = 1;
    attributes[2].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[2].offset   = 0;

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = 2;
    vertex_input.pVertexBindingDescriptions      = bindings;
    vertex_input.vertexAttributeDescriptionCount = 3;
    vertex_input.pVertexAttributeDescriptions    = attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode    = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable  = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments    = &color_blend_attachment;

    std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates    = dynamic_states.data();

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineShaderStageCreateInfo shader_stages[2] = {};
    shader_stages[0].sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage                           = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module                          = vert_module;
    shader_stages[0].pName                           = "main";

    shader_stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = frag_module;
    shader_stages[1].pName  = "main";

    VkPipelineRenderingCreateInfo pipeline_rendering{};
    pipeline_rendering.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering.colorAttachmentCount    = 1;
    pipeline_rendering.pColorAttachmentFormats = &color_format;
    pipeline_rendering.depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext               = &pipeline_rendering;
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = shader_stages;
    pipeline_info.pVertexInputState   = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState   = &multisampling;
    pipeline_info.pDepthStencilState  = &depth_stencil;
    pipeline_info.pColorBlendState    = &color_blending;
    pipeline_info.pDynamicState       = &dynamic_state;
    pipeline_info.layout              = pipeline_layout_;

    // Create solid pipeline
    vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &solid_pipeline_);

    // Create wireframe pipeline
    rasterizer.polygonMode  = VK_POLYGON_MODE_LINE;
    rasterizer.lineWidth    = 1.5f;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &wireframe_pipeline_);

    // Create point pipeline
    rasterizer.polygonMode                       = VK_POLYGON_MODE_POINT;
    input_assembly.topology                      = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    bindings[0].stride                           = sizeof(vk::toolkit::math::Vec3);
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.vertexAttributeDescriptionCount = 1;
    vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &point_pipeline_);

    // Create transparent pipeline
    rasterizer.polygonMode                       = VK_POLYGON_MODE_FILL;
    input_assembly.topology                      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bindings[0].stride                           = 6 * sizeof(float);
    vertex_input.vertexBindingDescriptionCount   = 2;
    vertex_input.vertexAttributeDescriptionCount = 3;
    color_blend_attachment.blendEnable           = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor   = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor   = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp          = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor   = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor   = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp          = VK_BLEND_OP_ADD;
    depth_stencil.depthWriteEnable               = VK_FALSE;
    vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &transparent_pipeline_);

    vkDestroyShaderModule(eng.device, vert_module, nullptr);
    vkDestroyShaderModule(eng.device, frag_module, nullptr);
}
void vk::plugins::BitmapViewer::destroy_pipeline(const context::EngineContext& eng) {
    if (solid_pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(eng.device, solid_pipeline_, nullptr);
    if (wireframe_pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(eng.device, wireframe_pipeline_, nullptr);
    if (point_pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(eng.device, point_pipeline_, nullptr);
    if (transparent_pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(eng.device, transparent_pipeline_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(eng.device, pipeline_layout_, nullptr);
}
void vk::plugins::BitmapViewer::create_geometry_buffers(const context::EngineContext& eng) {}
void vk::plugins::BitmapViewer::destroy_geometry_buffers(const context::EngineContext& eng) {}
