#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <imgui.h>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <vulkan/vulkan.h>
import vk.context;
import vk.engine;
import vk.toolkit.math;
import vk.toolkit.log;
import vk.toolkit.camera;
import vk.toolkit.geometry;
import vk.toolkit.vulkan;

namespace {
    struct MeshBuffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        uint32_t vertex_count{0};
    };

    std::vector<vk::toolkit::math::Mat4> generate_nerf_like_camera_path(std::size_t count, float radius, float height_variation) {
        std::vector<vk::toolkit::math::Mat4> poses;
        poses.reserve(count);

        constexpr vk::toolkit::math::Vec3 target{0.0f, 0.0f, 0.0f};
        const float two_pi = 2.0f * std::numbers::pi_v<float>;

        for (std::size_t i = 0; i < count; ++i) {
            const float t        = static_cast<float>(i) / static_cast<float>(count);
            const float theta    = t * two_pi;
            const float wobble   = std::sin(theta * 3.0f) * 0.25f;
            const float distance = radius * (0.8f + 0.15f * std::cos(theta * 0.5f));
            const float height   = height_variation * std::cos(theta * 1.5f) + wobble * 0.35f;

            const vk::toolkit::math::Vec3 position{distance * std::cos(theta), height, distance * std::sin(theta)};
            poses.push_back(vk::toolkit::geometry::build_pose(position, target, vk::toolkit::math::Vec3{0.0f, 1.0f, 0.0f}));
        }

        return poses;
    }

    std::vector<vk::toolkit::geometry::ColoredLine> make_path_lines(const std::vector<vk::toolkit::math::Mat4>& poses) {
        std::vector<vk::toolkit::geometry::ColoredLine> lines;
        lines.reserve(poses.size());
        if (poses.size() < 2) return lines;

        const vk::toolkit::math::Vec3 color{0.7f, 0.72f, 0.78f};
        vk::toolkit::math::Vec3 prev = extract_position(poses.front());
        for (std::size_t i = 1; i < poses.size(); ++i) {
            const vk::toolkit::math::Vec3 curr = extract_position(poses[i]);
            lines.push_back(vk::toolkit::geometry::ColoredLine{prev, curr, color});
            prev = curr;
        }
        lines.push_back(vk::toolkit::geometry::ColoredLine{prev, extract_position(poses.front()), color});
        return lines;
    }

    void append_lines(std::vector<vk::toolkit::geometry::Vertex>& out, const std::vector<vk::toolkit::geometry::ColoredLine>& lines) {
        out.reserve(out.size() + lines.size() * 2);
        for (const auto& l : lines) {
            out.push_back(vk::toolkit::geometry::Vertex{l.a, l.color});
            out.push_back(vk::toolkit::geometry::Vertex{l.b, l.color});
        }
    }

    MeshBuffer create_vertex_buffer(const vk::context::EngineContext& eng, std::span<const vk::toolkit::geometry::Vertex> vertices) {
        MeshBuffer mb{};
        if (vertices.empty()) return mb;

        const VkDeviceSize size = static_cast<VkDeviceSize>(vertices.size() * sizeof(vk::toolkit::geometry::Vertex));

        const VkBufferCreateInfo buffer_ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

        vk::toolkit::log::vk_check(vkCreateBuffer(eng.device, &buffer_ci, nullptr, &mb.buffer), "Failed to create vertex buffer");

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

        vk::toolkit::log::vk_check(vkAllocateMemory(eng.device, &alloc_info, nullptr, &mb.memory), "Failed to allocate vertex buffer memory");
        vk::toolkit::log::vk_check(vkBindBufferMemory(eng.device, mb.buffer, mb.memory, 0), "Failed to bind vertex buffer memory");

        void* mapped = nullptr;
        vk::toolkit::log::vk_check(vkMapMemory(eng.device, mb.memory, 0, size, 0, &mapped), "Failed to map vertex buffer memory");
        std::memcpy(mapped, vertices.data(), static_cast<size_t>(size));
        vkUnmapMemory(eng.device, mb.memory);

        mb.vertex_count = static_cast<uint32_t>(vertices.size());
        return mb;
    }
} // namespace

class CameraTransformPlugin {
public:
    explicit CameraTransformPlugin(std::shared_ptr<vk::toolkit::camera::Camera> camera) : camera_(std::move(camera)) {}

    [[nodiscard]] static constexpr vk::context::PluginPhase phases() noexcept {
        using vk::context::PluginPhase;
        return PluginPhase::Setup | PluginPhase::Initialize | PluginPhase::PreRender | PluginPhase::Render | PluginPhase::ImGUI | PluginPhase::Cleanup;
    }

    void on_setup(const vk::context::PluginContext& ctx) {
        if (!ctx.caps) return;
        ctx.caps->allow_async_compute        = false;
        ctx.caps->presentation_mode          = vk::context::PresentationMode::EngineBlit;
        ctx.caps->preferred_swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
        ctx.caps->color_samples              = VK_SAMPLE_COUNT_1_BIT;
        ctx.caps->uses_depth                 = VK_FALSE;
        ctx.caps->color_attachments       = {vk::context::AttachmentRequest{.name = "color", .format = VK_FORMAT_B8G8R8A8_UNORM, .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, .samples = VK_SAMPLE_COUNT_1_BIT, .aspect = VK_IMAGE_ASPECT_COLOR_BIT, .initial_layout = VK_IMAGE_LAYOUT_GENERAL}};
        ctx.caps->presentation_attachment = "color";
    }

    void on_initialize(vk::context::PluginContext&) {
        poses_                             = generate_nerf_like_camera_path(28, 4.0f, 0.6f);
        std::tie(center_, average_radius_) = vk::toolkit::geometry::compute_center_and_radius(poses_);

        const float frustum_near = std::max(0.12f, average_radius_ * 0.06f);
        const float frustum_far  = std::max(0.32f, average_radius_ * 0.12f);
        frustum_lines_           = vk::toolkit::geometry::make_frustum_lines(poses_, frustum_near, frustum_far, 45.0f);
        axis_lines_              = vk::toolkit::geometry::make_axis_lines(poses_, std::max(0.2f, average_radius_ * 0.08f));
        path_lines_              = make_path_lines(poses_);

        if (camera_) {
            auto s      = camera_->state();
            s.target    = center_;
            s.distance  = std::max(average_radius_ * 1.8f, 3.5f);
            s.yaw_deg   = -120.0f;
            s.pitch_deg = 22.0f;
            s.fov_y_deg = 55.0f;
            s.znear     = 0.05f;
            s.zfar      = std::max(50.0f, average_radius_ * 6.0f);
            camera_->set_state(s);
        }

        rebuild_mesh_buffer_ = true;
    }

    void on_pre_render(vk::context::PluginContext& ctx) {
        if (!camera_) return;
        if (ctx.frame) {
            viewport_width_  = ctx.frame->extent.width;
            viewport_height_ = ctx.frame->extent.height;
        }
        camera_->update(ctx.delta_time, static_cast<int>(viewport_width_), static_cast<int>(viewport_height_));

        if (rebuild_mesh_buffer_ && ctx.engine) {
            rebuild_mesh_buffer_ = false;
            build_mesh(*ctx.engine);
        }
    }

    void on_render(vk::context::PluginContext& ctx) {
        if (!ctx.cmd || !ctx.frame || ctx.frame->color_attachments.empty()) return;
        if (!ctx.engine) return;
        const auto& target = ctx.frame->color_attachments.front();

        // Recreate pipeline if swapchain format changed
        if (color_format_ != target.format) {
            destroy_pipeline(*ctx.engine);
            create_pipeline(*ctx.engine, target.format);
        }

        if (pipeline_ == VK_NULL_HANDLE || mesh_.buffer == VK_NULL_HANDLE) return;

        vk::context::transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        constexpr VkClearValue clear_value{.color = {{0.05f, 0.06f, 0.08f, 1.0f}}};
        const VkRenderingAttachmentInfo color_attachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = target.view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = clear_value};
        const VkRenderingInfo render_info{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, ctx.frame->extent}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color_attachment};
        vkCmdBeginRendering(*ctx.cmd, &render_info);

        const VkViewport viewport{.x = 0.0f, .y = 0.0f, .width = static_cast<float>(ctx.frame->extent.width), .height = static_cast<float>(ctx.frame->extent.height), .minDepth = 0.0f, .maxDepth = 1.0f};
        const VkRect2D scissor{{0, 0}, ctx.frame->extent};
        vkCmdSetViewport(*ctx.cmd, 0, 1, &viewport);
        vkCmdSetScissor(*ctx.cmd, 0, 1, &scissor);
        vkCmdSetLineWidth(*ctx.cmd, 1.6f);

        vkCmdBindPipeline(*ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        const VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(*ctx.cmd, 0, 1, &mesh_.buffer, offsets);

        const auto mvp = camera_->proj_matrix() * camera_->view_matrix();
        vkCmdPushConstants(*ctx.cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, mvp.m.data());

        vkCmdDraw(*ctx.cmd, mesh_.vertex_count, 1, 0, 0);

        vkCmdEndRendering(*ctx.cmd);
        vk::context::transition_image_layout(*ctx.cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    static void on_post_render(vk::context::PluginContext&) {}

    void on_imgui(vk::context::PluginContext&) {
        ImGui::SetNextWindowBgAlpha(0.9f);
        if (ImGui::Begin("Camera Transform Debug")) {
            ImGui::TextUnformatted("Vulkan line-rendered frustums");
            ImGui::Text("Poses: %zu", poses_.size());
            ImGui::Text("Center: [%.2f, %.2f, %.2f]", center_.x, center_.y, center_.z);
            ImGui::Text("Average radius: %.2f", average_radius_);

            if (ImGui::CollapsingHeader("Sample transforms", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (std::size_t i = 0; i < poses_.size(); ++i) {
                    const std::string label = std::format("pose [{}]", i);
                    if (ImGui::TreeNode(label.c_str())) {
                        const auto& m = poses_[i].m;
                        for (int row = 0; row < 4; ++row) {
                            ImGui::Text("%.3f  %.3f  %.3f  %.3f", m[0 + row], m[4 + row], m[8 + row], m[12 + row]);
                        }
                        ImGui::TreePop();
                    }
                }
            }
        }
        ImGui::End();
    }

    static void on_present(vk::context::PluginContext&) {}

    void on_cleanup(vk::context::PluginContext& ctx) {
        if (ctx.engine) {
            vkDeviceWaitIdle(ctx.engine->device);
            destroy_mesh(*ctx.engine);
            destroy_pipeline(*ctx.engine);
        }
    }

    void on_event(const SDL_Event& event) {
        if (!camera_) return;
        const auto& io = ImGui::GetIO();
        if (io.WantCaptureMouse || io.WantCaptureKeyboard) return;
        camera_->handle_event(event);
    }

    void on_resize(uint32_t width, uint32_t height) {
        viewport_width_  = width;
        viewport_height_ = height;
        color_format_    = VK_FORMAT_UNDEFINED;
    }

private:
    void build_mesh(const vk::context::EngineContext& eng) {
        destroy_mesh(eng);
        std::vector<vk::toolkit::geometry::Vertex> vertices;
        append_lines(vertices, frustum_lines_);
        append_lines(vertices, axis_lines_);
        append_lines(vertices, path_lines_);
        mesh_ = create_vertex_buffer(eng, vertices);
    }

    void create_pipeline(const vk::context::EngineContext& eng, VkFormat color_format) {
        color_format_ = color_format;

        const auto vert_module = vk::toolkit::vulkan::load_shader("test_camera_transform.vert.spv", eng.device);
        const auto frag_module = vk::toolkit::vulkan::load_shader("test_camera_transform.frag.spv", eng.device);

        const VkPushConstantRange push_constant{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(float) * 16};
        const VkPipelineLayoutCreateInfo layout_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1, .pPushConstantRanges = &push_constant};
        vk::toolkit::log::vk_check(vkCreatePipelineLayout(eng.device, &layout_info, nullptr, &pipeline_layout_), "Failed to create pipeline layout");

        const VkPipelineShaderStageCreateInfo vert_stage{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main"};
        const VkPipelineShaderStageCreateInfo frag_stage{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main"};
        const VkPipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

        const VkVertexInputBindingDescription binding{.binding = 0, .stride = sizeof(vk::toolkit::geometry::Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attributes[] = {
            {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk::toolkit::geometry::Vertex, pos)},
            {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk::toolkit::geometry::Vertex, color)},
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

        VkPipelineRenderingCreateInfo rendering_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1, .pColorAttachmentFormats = &color_format_};

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

        vk::toolkit::log::vk_check(vkCreateGraphicsPipelines(eng.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_), "Failed to create frustum pipeline");

        vkDestroyShaderModule(eng.device, vert_module, nullptr);
        vkDestroyShaderModule(eng.device, frag_module, nullptr);
    }

    void destroy_mesh(const vk::context::EngineContext& eng) {
        if (mesh_.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(eng.device, mesh_.buffer, nullptr);
        }
        if (mesh_.memory != VK_NULL_HANDLE) {
            vkFreeMemory(eng.device, mesh_.memory, nullptr);
        }
        mesh_ = {};
    }

    void destroy_pipeline(const vk::context::EngineContext& eng) {
        if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(eng.device, pipeline_, nullptr);
        if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(eng.device, pipeline_layout_, nullptr);
        pipeline_        = VK_NULL_HANDLE;
        pipeline_layout_ = VK_NULL_HANDLE;
    }

    std::shared_ptr<vk::toolkit::camera::Camera> camera_{};
    std::vector<vk::toolkit::math::Mat4> poses_{};
    std::vector<vk::toolkit::geometry::ColoredLine> frustum_lines_{};
    std::vector<vk::toolkit::geometry::ColoredLine> axis_lines_{};
    std::vector<vk::toolkit::geometry::ColoredLine> path_lines_{};
    vk::toolkit::math::Vec3 center_{0.0f, 0.0f, 0.0f};
    float average_radius_{4.0f};
    uint32_t viewport_width_{1280};
    uint32_t viewport_height_{720};

    MeshBuffer mesh_{};
    VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    VkFormat color_format_{VK_FORMAT_UNDEFINED};
    bool rebuild_mesh_buffer_{false};
};

int main() {
    vk::engine::VulkanEngine engine;
    const auto camera = std::make_shared<vk::toolkit::camera::Camera>();
    CameraTransformPlugin transform_visualizer(camera);

    engine.init(transform_visualizer);
    engine.run(transform_visualizer);
    engine.cleanup();
    return 0;
}
