module;
#include <SDL3/SDL.h>
#include <imgui.h>
#include <print>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>
module vk.plugins.transform;
import vk.context;
import vk.toolkit.geometry;
import vk.toolkit.log;
import vk.toolkit.vulkan;
import vk.toolkit.math;
import vk.toolkit.camera;

vk::plugins::MeshBuffer vk::plugins::create_vertex_buffer(const context::EngineContext& eng, std::span<const toolkit::geometry::Vertex> vertices) {
    MeshBuffer mb{};
    if (vertices.empty()) return mb;

    const VkDeviceSize size = static_cast<VkDeviceSize>(vertices.size() * sizeof(toolkit::geometry::Vertex));

    const VkBufferCreateInfo buffer_ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

    toolkit::log::vk_check(vkCreateBuffer(eng.device, &buffer_ci, nullptr, &mb.buffer), "Failed to create vertex buffer");

    VkMemoryRequirements mem_req{};
    vkGetBufferMemoryRequirements(eng.device, mb.buffer, &mem_req);

    auto find_memory_type = [&](uint32_t type_bits, VkMemoryPropertyFlags properties) -> uint32_t {
        VkPhysicalDeviceMemoryProperties mem_props{};
        vkGetPhysicalDeviceMemoryProperties(eng.physical, &mem_props);
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
            if ((type_bits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw std::runtime_error("Failed to find suitable memory type");
    };

    const VkMemoryAllocateInfo alloc_info{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mem_req.size, .memoryTypeIndex = find_memory_type(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};

    toolkit::log::vk_check(vkAllocateMemory(eng.device, &alloc_info, nullptr, &mb.memory), "Failed to allocate vertex buffer memory");
    toolkit::log::vk_check(vkBindBufferMemory(eng.device, mb.buffer, mb.memory, 0), "Failed to bind vertex buffer memory");

    void* mapped = nullptr;
    toolkit::log::vk_check(vkMapMemory(eng.device, mb.memory, 0, size, 0, &mapped), "Failed to map vertex buffer memory");
    std::memcpy(mapped, vertices.data(), static_cast<size_t>(size));
    vkUnmapMemory(eng.device, mb.memory);

    mb.vertex_count = static_cast<uint32_t>(vertices.size());
    return mb;
}
void vk::plugins::TransformViewer::on_setup(const context::PluginContext& ctx) {
    if (!ctx.caps) return;
    ctx.caps->allow_async_compute        = false;
    ctx.caps->presentation_mode          = context::PresentationMode::EngineBlit;
    ctx.caps->preferred_swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
    ctx.caps->color_samples              = VK_SAMPLE_COUNT_1_BIT;
    ctx.caps->uses_depth                 = VK_FALSE;
    ctx.caps->color_attachments          = {context::AttachmentRequest{.name = "color", .format = VK_FORMAT_B8G8R8A8_UNORM, .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, .samples = VK_SAMPLE_COUNT_1_BIT, .aspect = VK_IMAGE_ASPECT_COLOR_BIT, .initial_layout = VK_IMAGE_LAYOUT_GENERAL}};
    ctx.caps->presentation_attachment    = "color";
}
void vk::plugins::TransformViewer::on_initialize(context::PluginContext& ctx) {
    constexpr float average_radius_{4.0f};
    constexpr float frustum_near = std::max(0.12f, average_radius_ * 0.06f);
    constexpr float frustum_far  = std::max(0.32f, average_radius_ * 0.12f);

    std::vector<toolkit::geometry::Vertex> vertices;
    append_lines(vertices, toolkit::geometry::make_frustum_lines(this->poses_, frustum_near, frustum_far, 45.0f));
    append_lines(vertices, toolkit::geometry::make_axis_lines(this->poses_, std::max(0.2f, average_radius_ * 0.08f)));
    append_lines(vertices, toolkit::geometry::make_path_lines(this->poses_, {0.7f, 0.72f, 0.78f}));
    this->mesh_buffer_ = create_vertex_buffer(*ctx.engine, vertices);

    this->create_pipeline(*ctx.engine, ctx.frame->color_attachments.front().format);
}
void vk::plugins::TransformViewer::on_pre_render(context::PluginContext& ctx) const {
    camera_->update(ctx.delta_time, ctx.frame->extent.width, ctx.frame->extent.height);
}
void vk::plugins::TransformViewer::on_render(context::PluginContext& ctx) const {
    constexpr VkClearValue clear_value{.color = {{0.f, 0.f, 0.f, 1.f}}};
    const auto& target = ctx.frame->color_attachments.front();
    toolkit::vulkan::transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const VkRenderingAttachmentInfo color_attachment{
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = target.view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = clear_value,
    };
    const VkRenderingInfo render_info{
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = {{0, 0}, ctx.frame->extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_attachment,
    };
    vkCmdBeginRendering(*ctx.cmd, &render_info);


    const VkViewport viewport{.x = 0.0f, .y = 0.0f, .width = static_cast<float>(ctx.frame->extent.width), .height = static_cast<float>(ctx.frame->extent.height), .minDepth = 0.0f, .maxDepth = 1.0f};
    const VkRect2D scissor{{0, 0}, ctx.frame->extent};
    vkCmdSetViewport(*ctx.cmd, 0, 1, &viewport);
    vkCmdSetScissor(*ctx.cmd, 0, 1, &scissor);
    vkCmdSetLineWidth(*ctx.cmd, 1.6f);

    vkCmdBindPipeline(*ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipeline_);
    constexpr VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(*ctx.cmd, 0, 1, &this->mesh_buffer_.buffer, offsets);

    const auto mvp = this->camera_->proj_matrix() * this->camera_->view_matrix();
    vkCmdPushConstants(*ctx.cmd, this->pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, mvp.m.data());

    vkCmdDraw(*ctx.cmd, this->mesh_buffer_.vertex_count, 1, 0, 0);

    vkCmdEndRendering(*ctx.cmd);
    toolkit::vulkan::transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
}
void vk::plugins::TransformViewer::on_imgui(context::PluginContext& ctx) const {
    // FPS inspector overlay in top-left corner
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##FPSOverlay", nullptr, window_flags)) {
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
    }
    ImGui::End();

    this->camera_->draw_mini_axis_gizmo();
}
void vk::plugins::TransformViewer::on_cleanup(context::PluginContext& ctx) {
    if (ctx.engine) {
        vkDeviceWaitIdle(ctx.engine->device);
        if (mesh_buffer_.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx.engine->device, mesh_buffer_.buffer, nullptr);
        }
        if (mesh_buffer_.memory != VK_NULL_HANDLE) {
            vkFreeMemory(ctx.engine->device, mesh_buffer_.memory, nullptr);
        }
        mesh_buffer_ = {};
        destroy_pipeline(*ctx.engine);
    }
}
void vk::plugins::TransformViewer::on_event(const SDL_Event& event) const {
    const auto& io = ImGui::GetIO();
    if (const bool imgui_wants_input = io.WantCaptureMouse || io.WantCaptureKeyboard; !imgui_wants_input) {
        this->camera_->handle_event(event);
    }
}
void vk::plugins::TransformViewer::create_pipeline(const context::EngineContext& eng, VkFormat color_format) {
    const auto vert_module = toolkit::vulkan::load_shader("test_camera_transform.vert.spv", eng.device);
    const auto frag_module = toolkit::vulkan::load_shader("test_camera_transform.frag.spv", eng.device);

    const VkPushConstantRange push_constant{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(float) * 16};
    const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1, .pPushConstantRanges = &push_constant};
    toolkit::log::vk_check(vkCreatePipelineLayout(eng.device, &layout_info, nullptr, &pipeline_layout_), "Failed to create pipeline layout");

    const VkPipelineShaderStageCreateInfo vert_stage{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main"};
    const VkPipelineShaderStageCreateInfo frag_stage{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main"};
    const VkPipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

    const VkVertexInputBindingDescription binding{.binding = 0, .stride = sizeof(toolkit::geometry::Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(toolkit::geometry::Vertex, pos)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(toolkit::geometry::Vertex, color)},
    };

    const VkPipelineVertexInputStateCreateInfo vertex_input{.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding, .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attributes};

    const VkPipelineInputAssemblyStateCreateInfo input_assembly{.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST};

    const VkPipelineViewportStateCreateInfo viewport_state{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};

    const VkPipelineRasterizationStateCreateInfo rasterizer{.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.5f};

    const VkPipelineMultisampleStateCreateInfo msaa{.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

    const VkPipelineColorBlendAttachmentState color_blend_attachment{.blendEnable = VK_FALSE, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    const VkPipelineColorBlendStateCreateInfo color_blend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &color_blend_attachment};

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH};
    const VkPipelineDynamicStateCreateInfo dynamic_state{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 3, .pDynamicStates = dynamic_states};

    VkPipelineRenderingCreateInfo rendering_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1, .pColorAttachmentFormats = &color_format};

    const VkGraphicsPipelineCreateInfo pipeline_info{.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext                                              = &rendering_info,
        .stageCount                                         = 2,
        .pStages                                            = stages,
        .pVertexInputState                                  = &vertex_input,
        .pInputAssemblyState                                = &input_assembly,
        .pViewportState                                     = &viewport_state,
        .pRasterizationState                                = &rasterizer,
        .pMultisampleState                                  = &msaa,
        .pDepthStencilState                                 = nullptr,
        .pColorBlendState                                   = &color_blend,
        .pDynamicState                                      = &dynamic_state,
        .layout                                             = pipeline_layout_,
        .renderPass                                         = VK_NULL_HANDLE,
        .subpass                                            = 0};

    toolkit::log::vk_check(vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_), "Failed to create frustum pipeline");

    vkDestroyShaderModule(eng.device, vert_module, nullptr);
    vkDestroyShaderModule(eng.device, frag_module, nullptr);
}
void vk::plugins::TransformViewer::destroy_pipeline(const context::EngineContext& eng) {
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(eng.device, pipeline_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(eng.device, pipeline_layout_, nullptr);
    pipeline_        = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
}
