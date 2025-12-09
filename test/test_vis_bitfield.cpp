#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <vector>
#include <vk_mem_alloc.h>
#include <SDL3/SDL.h>
import vk.engine;
import vk.context;
import vk.plugins.viewport3d;
import vk.toolkit.camera;
import vk.toolkit.math;
import vk.toolkit.vulkan;

namespace {
    // 3D Occupancy Grid Configuration
    constexpr int GRID_SIZE = 64;  // 64x64x64 grid
    constexpr int TOTAL_VOXELS = GRID_SIZE * GRID_SIZE * GRID_SIZE;
    constexpr int BITMAP_SIZE = (TOTAL_VOXELS + 7) / 8;  // bits to bytes
    constexpr float VOXEL_SIZE = 0.05f;
    constexpr float GRID_CENTER = GRID_SIZE * VOXEL_SIZE * 0.5f;

    // Convert 3D coordinates to linear index
    inline int coord_to_index(int x, int y, int z) {
        return x + y * GRID_SIZE + z * GRID_SIZE * GRID_SIZE;
    }

    // Check if a bit is set in the bitmap
    inline bool is_occupied(const std::vector<uint8_t>& bitmap, int x, int y, int z) {
        int index = coord_to_index(x, y, z);
        int byte_index = index / 8;
        int bit_index = index % 8;
        return (bitmap[byte_index] & (1 << bit_index)) != 0;
    }

    // Set a bit in the bitmap
    inline void set_occupied(std::vector<uint8_t>& bitmap, int x, int y, int z) {
        int index = coord_to_index(x, y, z);
        int byte_index = index / 8;
        int bit_index = index % 8;
        bitmap[byte_index] |= (1 << bit_index);
    }

    // Create a centered sphere occupancy grid
    std::vector<uint8_t> create_sphere_occupancy_grid(float radius_ratio = 0.4f) {
        std::vector<uint8_t> bitmap(BITMAP_SIZE, 0);

        const float center = GRID_SIZE * 0.5f;
        const float radius = GRID_SIZE * radius_ratio;
        const float radius_sq = radius * radius;

        for (int z = 0; z < GRID_SIZE; ++z) {
            for (int y = 0; y < GRID_SIZE; ++y) {
                for (int x = 0; x < GRID_SIZE; ++x) {
                    float dx = x - center;
                    float dy = y - center;
                    float dz = z - center;
                    float dist_sq = dx * dx + dy * dy + dz * dz;

                    if (dist_sq <= radius_sq) {
                        set_occupied(bitmap, x, y, z);
                    }
                }
            }
        }

        return bitmap;
    }

    // Plugin for rendering occupancy grid
    class OccupancyGridRenderer {
    public:
        explicit OccupancyGridRenderer(const std::shared_ptr<vk::toolkit::camera::Camera>& camera,
                                       const std::vector<uint8_t>& bitmap)
            : camera_(camera), bitmap_(bitmap) {
            extract_voxel_positions();
        }

        [[nodiscard]] static constexpr vk::context::PluginPhase phases() noexcept {
            return vk::context::PluginPhase::Setup |
                   vk::context::PluginPhase::Initialize |
                   vk::context::PluginPhase::Render |
                   vk::context::PluginPhase::Cleanup;
        }

        void on_setup(vk::context::PluginContext& ctx) {
            ctx.caps->uses_depth = VK_TRUE;
            ctx.caps->depth_attachment = vk::context::AttachmentRequest{
                .name = "depth",
                .format = VK_FORMAT_D32_SFLOAT,
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
                .initial_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
            };
        }

        void on_initialize(vk::context::PluginContext& ctx) {
            create_cube_mesh(ctx.engine);
            create_instance_buffer(ctx.engine);
            create_pipeline(ctx.engine, ctx.frame);
        }

        static void on_pre_render(vk::context::PluginContext&) {}

        void on_render(vk::context::PluginContext& ctx) {
            auto& cmd = *ctx.cmd;
            const auto& frame = *ctx.frame;

            // Update camera
            camera_->update(static_cast<float>(ctx.delta_time),
                          static_cast<int>(frame.extent.width),
                          static_cast<int>(frame.extent.height));

            // Get view-projection matrix
            const auto view = camera_->view_matrix();
            const auto proj = camera_->proj_matrix();
            const auto view_proj = proj * view;

            // Begin rendering
            VkRenderingAttachmentInfo color_attachment{};
            color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color_attachment.imageView = frame.offscreen_image_view;
            color_attachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color_attachment.clearValue.color = {{0.01f, 0.01f, 0.01f, 1.0f}};

            VkRenderingAttachmentInfo depth_attachment{};
            depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth_attachment.imageView = frame.depth_image_view;
            depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depth_attachment.clearValue.depthStencil = {1.0f, 0};

            VkRenderingInfo render_info{};
            render_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            render_info.renderArea = {{0, 0}, frame.extent};
            render_info.layerCount = 1;
            render_info.colorAttachmentCount = 1;
            render_info.pColorAttachments = &color_attachment;
            render_info.pDepthAttachment = &depth_attachment;

            vkCmdBeginRendering(cmd, &render_info);

            // Bind pipeline
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

            // Set viewport and scissor
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(frame.extent.width);
            viewport.height = static_cast<float>(frame.extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = frame.extent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // Push constants
            vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                             0, sizeof(vk::toolkit::math::Mat4), &view_proj);

            // Bind vertex and index buffers
            VkBuffer vertex_buffers[] = {vertex_buffer_, instance_buffer_};
            VkDeviceSize offsets[] = {0, 0};
            vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);
            vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT32);

            // Draw instanced
            vkCmdDrawIndexed(cmd, index_count_, static_cast<uint32_t>(voxel_positions_.size()), 0, 0, 0);

            vkCmdEndRendering(cmd);
        }

        static void on_post_render(vk::context::PluginContext&) {}
        static void on_imgui(vk::context::PluginContext&) {}
        static void on_present(vk::context::PluginContext&) {}

        void on_cleanup(vk::context::PluginContext& ctx) {
            auto& eng = *ctx.engine;
            if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(eng.device, pipeline_, nullptr);
            if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(eng.device, pipeline_layout_, nullptr);
            if (vertex_buffer_ != VK_NULL_HANDLE) vmaDestroyBuffer(eng.allocator, vertex_buffer_, vertex_allocation_);
            if (index_buffer_ != VK_NULL_HANDLE) vmaDestroyBuffer(eng.allocator, index_buffer_, index_allocation_);
            if (instance_buffer_ != VK_NULL_HANDLE) vmaDestroyBuffer(eng.allocator, instance_buffer_, instance_allocation_);
        }

        static void on_event(const SDL_Event&) {}
        void on_resize(uint32_t, uint32_t) {}

    private:
        void extract_voxel_positions() {
            for (int z = 0; z < GRID_SIZE; ++z) {
                for (int y = 0; y < GRID_SIZE; ++y) {
                    for (int x = 0; x < GRID_SIZE; ++x) {
                        if (is_occupied(bitmap_, x, y, z)) {
                            vk::toolkit::math::Vec3 pos{
                                x * VOXEL_SIZE - GRID_CENTER,
                                y * VOXEL_SIZE - GRID_CENTER,
                                z * VOXEL_SIZE - GRID_CENTER
                            };
                            voxel_positions_.push_back(pos);
                        }
                    }
                }
            }
        }

        void create_cube_mesh(vk::context::EngineContext* eng) {
            // Simple cube vertices (position + normal)
            const float hs = VOXEL_SIZE * 0.5f; // half size
            std::vector<float> vertices = {
                // Front face (z+)
                -hs, -hs,  hs,  0.0f, 0.0f, 1.0f,
                 hs, -hs,  hs,  0.0f, 0.0f, 1.0f,
                 hs,  hs,  hs,  0.0f, 0.0f, 1.0f,
                -hs,  hs,  hs,  0.0f, 0.0f, 1.0f,
                // Back face (z-)
                 hs, -hs, -hs,  0.0f, 0.0f, -1.0f,
                -hs, -hs, -hs,  0.0f, 0.0f, -1.0f,
                -hs,  hs, -hs,  0.0f, 0.0f, -1.0f,
                 hs,  hs, -hs,  0.0f, 0.0f, -1.0f,
                // Right face (x+)
                 hs, -hs,  hs,  1.0f, 0.0f, 0.0f,
                 hs, -hs, -hs,  1.0f, 0.0f, 0.0f,
                 hs,  hs, -hs,  1.0f, 0.0f, 0.0f,
                 hs,  hs,  hs,  1.0f, 0.0f, 0.0f,
                // Left face (x-)
                -hs, -hs, -hs,  -1.0f, 0.0f, 0.0f,
                -hs, -hs,  hs,  -1.0f, 0.0f, 0.0f,
                -hs,  hs,  hs,  -1.0f, 0.0f, 0.0f,
                -hs,  hs, -hs,  -1.0f, 0.0f, 0.0f,
                // Top face (y+)
                -hs,  hs,  hs,  0.0f, 1.0f, 0.0f,
                 hs,  hs,  hs,  0.0f, 1.0f, 0.0f,
                 hs,  hs, -hs,  0.0f, 1.0f, 0.0f,
                -hs,  hs, -hs,  0.0f, 1.0f, 0.0f,
                // Bottom face (y-)
                -hs, -hs, -hs,  0.0f, -1.0f, 0.0f,
                 hs, -hs, -hs,  0.0f, -1.0f, 0.0f,
                 hs, -hs,  hs,  0.0f, -1.0f, 0.0f,
                -hs, -hs,  hs,  0.0f, -1.0f, 0.0f,
            };

            std::vector<uint32_t> indices = {
                0, 1, 2, 2, 3, 0,       // Front
                4, 5, 6, 6, 7, 4,       // Back
                8, 9, 10, 10, 11, 8,    // Right
                12, 13, 14, 14, 15, 12, // Left
                16, 17, 18, 18, 19, 16, // Top
                20, 21, 22, 22, 23, 20  // Bottom
            };

            vertex_count_ = static_cast<uint32_t>(vertices.size() / 6);
            index_count_ = static_cast<uint32_t>(indices.size());

            // Create vertex buffer
            VkBufferCreateInfo buffer_info{};
            buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_info.size = vertices.size() * sizeof(float);
            buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VmaAllocationCreateInfo alloc_info{};
            alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

            vmaCreateBuffer(eng->allocator, &buffer_info, &alloc_info, &vertex_buffer_, &vertex_allocation_, nullptr);

            void* data;
            vmaMapMemory(eng->allocator, vertex_allocation_, &data);
            std::memcpy(data, vertices.data(), vertices.size() * sizeof(float));
            vmaUnmapMemory(eng->allocator, vertex_allocation_);

            // Create index buffer
            buffer_info.size = indices.size() * sizeof(uint32_t);
            buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            vmaCreateBuffer(eng->allocator, &buffer_info, &alloc_info, &index_buffer_, &index_allocation_, nullptr);

            vmaMapMemory(eng->allocator, index_allocation_, &data);
            std::memcpy(data, indices.data(), indices.size() * sizeof(uint32_t));
            vmaUnmapMemory(eng->allocator, index_allocation_);
        }

        void create_instance_buffer(vk::context::EngineContext* eng) {
            VkBufferCreateInfo buffer_info{};
            buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_info.size = voxel_positions_.size() * sizeof(vk::toolkit::math::Vec3);
            buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VmaAllocationCreateInfo alloc_info{};
            alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

            vmaCreateBuffer(eng->allocator, &buffer_info, &alloc_info, &instance_buffer_, &instance_allocation_, nullptr);

            void* data;
            vmaMapMemory(eng->allocator, instance_allocation_, &data);
            std::memcpy(data, voxel_positions_.data(), voxel_positions_.size() * sizeof(vk::toolkit::math::Vec3));
            vmaUnmapMemory(eng->allocator, instance_allocation_);
        }

        void create_pipeline(vk::context::EngineContext* eng, vk::context::FrameContext* frame) {
            // Load shaders
            auto vert_code = load_shader_file("shader/bitfield.vert.spv");
            auto frag_code = load_shader_file("shader/bitfield.frag.spv");

            VkShaderModuleCreateInfo shader_info{};
            shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

            shader_info.codeSize = vert_code.size();
            shader_info.pCode = reinterpret_cast<const uint32_t*>(vert_code.data());
            VkShaderModule vert_module;
            vkCreateShaderModule(eng->device, &shader_info, nullptr, &vert_module);

            shader_info.codeSize = frag_code.size();
            shader_info.pCode = reinterpret_cast<const uint32_t*>(frag_code.data());
            VkShaderModule frag_module;
            vkCreateShaderModule(eng->device, &shader_info, nullptr, &frag_module);

            // Pipeline layout
            VkPushConstantRange push_constant{};
            push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            push_constant.offset = 0;
            push_constant.size = sizeof(vk::toolkit::math::Mat4);

            VkPipelineLayoutCreateInfo layout_info{};
            layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.pushConstantRangeCount = 1;
            layout_info.pPushConstantRanges = &push_constant;

            vkCreatePipelineLayout(eng->device, &layout_info, nullptr, &pipeline_layout_);

            // Vertex input
            VkVertexInputBindingDescription bindings[2] = {};
            bindings[0].binding = 0;
            bindings[0].stride = 6 * sizeof(float);
            bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            bindings[1].binding = 1;
            bindings[1].stride = sizeof(vk::toolkit::math::Vec3);
            bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

            VkVertexInputAttributeDescription attributes[3] = {};
            attributes[0].location = 0;
            attributes[0].binding = 0;
            attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributes[0].offset = 0;

            attributes[1].location = 1;
            attributes[1].binding = 0;
            attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributes[1].offset = 3 * sizeof(float);

            attributes[2].location = 2;
            attributes[2].binding = 1;
            attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributes[2].offset = 0;

            VkPipelineVertexInputStateCreateInfo vertex_input{};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_input.vertexBindingDescriptionCount = 2;
            vertex_input.pVertexBindingDescriptions = bindings;
            vertex_input.vertexAttributeDescriptionCount = 3;
            vertex_input.pVertexAttributeDescriptions = attributes;

            VkPipelineInputAssemblyStateCreateInfo input_assembly{};
            input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depth_stencil{};
            depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil.depthTestEnable = VK_TRUE;
            depth_stencil.depthWriteEnable = VK_TRUE;
            depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

            VkPipelineColorBlendAttachmentState color_blend_attachment{};
            color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

            VkPipelineColorBlendStateCreateInfo color_blending{};
            color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blending.attachmentCount = 1;
            color_blending.pAttachments = &color_blend_attachment;

            std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic_state{};
            dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
            dynamic_state.pDynamicStates = dynamic_states.data();

            VkPipelineViewportStateCreateInfo viewport_state{};
            viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            VkPipelineShaderStageCreateInfo shader_stages[2] = {};
            shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            shader_stages[0].module = vert_module;
            shader_stages[0].pName = "main";

            shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            shader_stages[1].module = frag_module;
            shader_stages[1].pName = "main";

            VkPipelineRenderingCreateInfo pipeline_rendering{};
            pipeline_rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            pipeline_rendering.colorAttachmentCount = 1;
            VkFormat color_format = frame->color_attachments[0].format;
            pipeline_rendering.pColorAttachmentFormats = &color_format;
            pipeline_rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

            VkGraphicsPipelineCreateInfo pipeline_info{};
            pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeline_info.pNext = &pipeline_rendering;
            pipeline_info.stageCount = 2;
            pipeline_info.pStages = shader_stages;
            pipeline_info.pVertexInputState = &vertex_input;
            pipeline_info.pInputAssemblyState = &input_assembly;
            pipeline_info.pViewportState = &viewport_state;
            pipeline_info.pRasterizationState = &rasterizer;
            pipeline_info.pMultisampleState = &multisampling;
            pipeline_info.pDepthStencilState = &depth_stencil;
            pipeline_info.pColorBlendState = &color_blending;
            pipeline_info.pDynamicState = &dynamic_state;
            pipeline_info.layout = pipeline_layout_;

            vkCreateGraphicsPipelines(eng->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_);

            vkDestroyShaderModule(eng->device, vert_module, nullptr);
            vkDestroyShaderModule(eng->device, frag_module, nullptr);
        }

        static std::vector<char> load_shader_file(const char* filename) {
            FILE* file = fopen(filename, "rb");
            if (!file) {
                throw std::runtime_error(std::string("Failed to open shader file: ") + filename);
            }

            fseek(file, 0, SEEK_END);
            size_t size = ftell(file);
            fseek(file, 0, SEEK_SET);

            std::vector<char> buffer(size);
            fread(buffer.data(), 1, size, file);
            fclose(file);

            return buffer;
        }

        std::shared_ptr<vk::toolkit::camera::Camera> camera_;
        std::vector<uint8_t> bitmap_;
        std::vector<vk::toolkit::math::Vec3> voxel_positions_;

        VkBuffer vertex_buffer_{VK_NULL_HANDLE};
        VkBuffer index_buffer_{VK_NULL_HANDLE};
        VkBuffer instance_buffer_{VK_NULL_HANDLE};
        VmaAllocation vertex_allocation_{};
        VmaAllocation index_allocation_{};
        VmaAllocation instance_allocation_{};
        uint32_t vertex_count_{0};
        uint32_t index_count_{0};

        VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
        VkPipeline pipeline_{VK_NULL_HANDLE};
    };
}

int main() {
    vk::engine::VulkanEngine engine;

    // Create occupancy grid (centered sphere)
    auto bitmap = create_sphere_occupancy_grid(0.4f);

    // Create camera (uses default orbit mode with distance 5.0)
    auto camera = std::make_shared<vk::toolkit::camera::Camera>();

    // Create plugins
    vk::plugins::Viewport3D viewport(camera);
    OccupancyGridRenderer grid_renderer(camera, bitmap);

    // Run engine
    engine.init(viewport, grid_renderer);
    engine.run(viewport, grid_renderer);
    engine.cleanup();

    return 0;
}

