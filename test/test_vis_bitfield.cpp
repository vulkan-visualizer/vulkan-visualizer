#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <vector>
#include <vk_mem_alloc.h>
#include <SDL3/SDL.h>
#include <imgui.h>
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
                    float dx = static_cast<float>(x) - center;
                    float dy = static_cast<float>(y) - center;
                    float dz = static_cast<float>(z) - center;
                    float dist_sq = dx * dx + dy * dy + dz * dz;

                    if (dist_sq <= radius_sq) {
                        set_occupied(bitmap, x, y, z);
                    }
                }
            }
        }

        return bitmap;
    }

    // Visualization modes
    enum class VisualizationMode {
        WireframeGrid,       // Grid showing all cells with different colors (DEFAULT)
        SolidCubes,          // Filled cubes for occupied voxels
        Points,              // Point cloud of occupied voxels
        TransparentShell,    // Semi-transparent occupied voxels with edges
        DensityColored       // Color based on local density
    };

    // Plugin for rendering occupancy grid
    class OccupancyGridRenderer {
    public:
        explicit OccupancyGridRenderer(const std::shared_ptr<vk::toolkit::camera::Camera>& camera,
                                       const std::vector<uint8_t>& bitmap)
            : camera_(camera), bitmap_(bitmap) {
            extract_voxel_positions();
            compute_density_colors();
        }

        [[nodiscard]] static constexpr vk::context::PluginPhase phases() noexcept {
            return vk::context::PluginPhase::Setup |
                   vk::context::PluginPhase::Initialize |
                   vk::context::PluginPhase::Render |
                   vk::context::PluginPhase::ImGUI |
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
            create_grid_mesh(ctx.engine);
            create_instance_buffer(ctx.engine);
            create_all_grid_instance_buffer(ctx.engine);
            create_pipelines(ctx.engine, ctx.frame);
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

            // Render based on selected mode
            switch (viz_mode_) {
                case VisualizationMode::WireframeGrid:
                    render_wireframe_grid(cmd, view_proj);
                    break;
                case VisualizationMode::SolidCubes:
                    render_solid_cubes(cmd, view_proj);
                    break;
                case VisualizationMode::Points:
                    render_points(cmd, view_proj);
                    break;
                case VisualizationMode::TransparentShell:
                    render_transparent_shell(cmd, view_proj);
                    break;
                case VisualizationMode::DensityColored:
                    render_density_colored(cmd, view_proj);
                    break;
            }

            vkCmdEndRendering(cmd);
        }

        static void on_post_render(vk::context::PluginContext&) {}

        void on_imgui(vk::context::PluginContext&) {
            if (!show_panel_) return;

            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("Occupancy Grid Visualizer", &show_panel_)) {
                ImGui::Text("3D Occupancy Grid Visualization");
                ImGui::Separator();

                // Statistics section
                if (ImGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Text("Grid Size: %dx%dx%d", GRID_SIZE, GRID_SIZE, GRID_SIZE);
                    ImGui::Text("Total Voxels: %d", TOTAL_VOXELS);
                    ImGui::Text("Occupied Voxels: %zu", voxel_positions_.size());
                    float occupancy_rate = (float)voxel_positions_.size() / TOTAL_VOXELS * 100.0f;
                    ImGui::Text("Occupancy Rate: %.2f%%", occupancy_rate);
                    ImGui::Text("Bitmap Size: %d bytes", BITMAP_SIZE);
                    ImGui::Separator();
                    ImGui::Text("Voxel Size: %.3f", VOXEL_SIZE);
                    ImGui::Text("Grid Bounds: [%.2f, %.2f]", -GRID_CENTER, GRID_CENTER);
                }

                ImGui::Spacing();

                // Visualization mode selection
                if (ImGui::CollapsingHeader("Visualization Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const char* mode_names[] = {
                        "Wireframe Grid",
                        "Solid Cubes",
                        "Point Cloud",
                        "Transparent Shell",
                        "Density Colored"
                    };

                    int current_mode = static_cast<int>(viz_mode_);

                    if (ImGui::RadioButton("Wireframe Grid", current_mode == 0)) {
                        viz_mode_ = VisualizationMode::WireframeGrid;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Shows ALL voxels (occupied: bright cyan, empty: dim gray)\nBest for understanding grid structure");
                    }

                    if (ImGui::RadioButton("Solid Cubes", current_mode == 1)) {
                        viz_mode_ = VisualizationMode::SolidCubes;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Filled cubes for occupied voxels only\nGood for dense visualization");
                    }

                    if (ImGui::RadioButton("Point Cloud", current_mode == 2)) {
                        viz_mode_ = VisualizationMode::Points;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Fastest rendering - single point per voxel\nBest for performance and large grids");
                    }

                    if (ImGui::RadioButton("Transparent Shell", current_mode == 3)) {
                        viz_mode_ = VisualizationMode::TransparentShell;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Semi-transparent cubes with alpha blending\nGood for seeing internal structure");
                    }

                    if (ImGui::RadioButton("Density Colored", current_mode == 4)) {
                        viz_mode_ = VisualizationMode::DensityColored;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Colors based on local neighbor density\nBlue=sparse, Cyan=medium, Yellow=dense, Red=very dense");
                    }

                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Current: %s", mode_names[current_mode]);
                }

                ImGui::Spacing();

                // Rendering info
                if (ImGui::CollapsingHeader("Rendering Info")) {
                    const char* render_info[] = {
                        "All voxels with LINE_LIST topology",
                        "Occupied voxels with TRIANGLE_LIST",
                        "Occupied voxels with POINT_LIST",
                        "Occupied voxels with alpha blending",
                        "Occupied voxels with density colors"
                    };
                    int current_mode = static_cast<int>(viz_mode_);
                    ImGui::BulletText("Topology: %s", render_info[current_mode]);

                    size_t draw_count = (viz_mode_ == VisualizationMode::WireframeGrid)
                        ? TOTAL_VOXELS : voxel_positions_.size();
                    ImGui::BulletText("Instances: %zu", draw_count);
                    ImGui::BulletText("Draw Calls: 1 (instanced)");
                }

                ImGui::Spacing();

                // Controls help
                if (ImGui::CollapsingHeader("Controls")) {
                    ImGui::TextWrapped("Camera:");
                    ImGui::BulletText("Left Mouse: Rotate (Orbit mode)");
                    ImGui::BulletText("Right Mouse: Pan");
                    ImGui::BulletText("Middle Mouse / Scroll: Zoom");
                    ImGui::BulletText("Press H: Home view");
                    ImGui::Spacing();
                    ImGui::TextWrapped("Shortcuts:");
                    ImGui::BulletText("1-5: Quick switch visualization modes");
                    ImGui::BulletText("C: Toggle camera panel");
                    ImGui::BulletText("G: Toggle this panel");
                }

                ImGui::Spacing();
                ImGui::Separator();

                // Color legend
                if (ImGui::CollapsingHeader("Color Legend")) {
                    if (viz_mode_ == VisualizationMode::WireframeGrid) {
                        ImGui::ColorButton("##occupied", ImVec4(0.0f, 1.0f, 0.8f, 1.0f));
                        ImGui::SameLine();
                        ImGui::Text("Occupied Voxels");

                        ImGui::ColorButton("##empty", ImVec4(0.15f, 0.15f, 0.2f, 1.0f));
                        ImGui::SameLine();
                        ImGui::Text("Empty Voxels");
                    } else if (viz_mode_ == VisualizationMode::DensityColored) {
                        ImGui::ColorButton("##blue", ImVec4(0.0f, 0.5f, 1.0f, 1.0f));
                        ImGui::SameLine();
                        ImGui::Text("Sparse (< 25%% density)");

                        ImGui::ColorButton("##cyan", ImVec4(0.0f, 1.0f, 0.8f, 1.0f));
                        ImGui::SameLine();
                        ImGui::Text("Medium (25-50%% density)");

                        ImGui::ColorButton("##yellow", ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
                        ImGui::SameLine();
                        ImGui::Text("Dense (50-75%% density)");

                        ImGui::ColorButton("##red", ImVec4(1.0f, 0.3f, 0.0f, 1.0f));
                        ImGui::SameLine();
                        ImGui::Text("Very Dense (> 75%% density)");
                    } else {
                        ImGui::TextWrapped("Position-based coloring for depth perception");
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Press 'G' to toggle this panel");
            }
            ImGui::End();
        }

        static void on_present(vk::context::PluginContext&) {}

        void on_cleanup(vk::context::PluginContext& ctx) {
            auto& eng = *ctx.engine;
            if (solid_pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(eng.device, solid_pipeline_, nullptr);
            if (wireframe_pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(eng.device, wireframe_pipeline_, nullptr);
            if (point_pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(eng.device, point_pipeline_, nullptr);
            if (transparent_pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(eng.device, transparent_pipeline_, nullptr);
            if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(eng.device, pipeline_layout_, nullptr);
            if (vertex_buffer_ != VK_NULL_HANDLE) vmaDestroyBuffer(eng.allocator, vertex_buffer_, vertex_allocation_);
            if (index_buffer_ != VK_NULL_HANDLE) vmaDestroyBuffer(eng.allocator, index_buffer_, index_allocation_);
            if (grid_vertex_buffer_ != VK_NULL_HANDLE) vmaDestroyBuffer(eng.allocator, grid_vertex_buffer_, grid_vertex_allocation_);
            if (grid_index_buffer_ != VK_NULL_HANDLE) vmaDestroyBuffer(eng.allocator, grid_index_buffer_, grid_index_allocation_);
            if (instance_buffer_ != VK_NULL_HANDLE) vmaDestroyBuffer(eng.allocator, instance_buffer_, instance_allocation_);
            if (all_grid_instance_buffer_ != VK_NULL_HANDLE) vmaDestroyBuffer(eng.allocator, all_grid_instance_buffer_, all_grid_instance_allocation_);
            if (density_color_buffer_ != VK_NULL_HANDLE) vmaDestroyBuffer(eng.allocator, density_color_buffer_, density_color_allocation_);
        }

        void on_event(const SDL_Event& event) {
            if (event.type == SDL_EVENT_KEY_DOWN) {
                switch (event.key.key) {
                    case SDLK_1:
                        viz_mode_ = VisualizationMode::WireframeGrid;
                        break;
                    case SDLK_2:
                        viz_mode_ = VisualizationMode::SolidCubes;
                        break;
                    case SDLK_3:
                        viz_mode_ = VisualizationMode::Points;
                        break;
                    case SDLK_4:
                        viz_mode_ = VisualizationMode::TransparentShell;
                        break;
                    case SDLK_5:
                        viz_mode_ = VisualizationMode::DensityColored;
                        break;
                    case SDLK_G:
                        show_panel_ = !show_panel_;
                        break;
                }
            }
        }

        void on_resize(uint32_t, uint32_t) {}

    private:
        void extract_voxel_positions() {
            for (int z = 0; z < GRID_SIZE; ++z) {
                for (int y = 0; y < GRID_SIZE; ++y) {
                    for (int x = 0; x < GRID_SIZE; ++x) {
                        if (is_occupied(bitmap_, x, y, z)) {
                            vk::toolkit::math::Vec3 pos{
                                static_cast<float>(x) * VOXEL_SIZE - GRID_CENTER,
                                static_cast<float>(y) * VOXEL_SIZE - GRID_CENTER,
                                static_cast<float>(z) * VOXEL_SIZE - GRID_CENTER
                            };
                            voxel_positions_.push_back(pos);
                        }
                    }
                }
            }
        }

        void compute_density_colors() {
            density_colors_.clear();
            density_colors_.reserve(voxel_positions_.size());

            for (int z = 0; z < GRID_SIZE; ++z) {
                for (int y = 0; y < GRID_SIZE; ++y) {
                    for (int x = 0; x < GRID_SIZE; ++x) {
                        if (is_occupied(bitmap_, x, y, z)) {
                            // Count neighbors (3x3x3 kernel)
                            int neighbor_count = 0;
                            for (int dz = -1; dz <= 1; ++dz) {
                                for (int dy = -1; dy <= 1; ++dy) {
                                    for (int dx = -1; dx <= 1; ++dx) {
                                        int nx = x + dx, ny = y + dy, nz = z + dz;
                                        if (nx >= 0 && nx < GRID_SIZE && ny >= 0 && ny < GRID_SIZE &&
                                            nz >= 0 && nz < GRID_SIZE && is_occupied(bitmap_, nx, ny, nz)) {
                                            neighbor_count++;
                                        }
                                    }
                                }
                            }

                            // Color: blue (sparse) -> cyan -> green -> yellow -> red (dense)
                            float density = neighbor_count / 27.0f;
                            vk::toolkit::math::Vec3 color;
                            if (density < 0.25f) {
                                color = {0.0f, 0.5f, 1.0f}; // Blue
                            } else if (density < 0.5f) {
                                color = {0.0f, 1.0f, 0.8f}; // Cyan
                            } else if (density < 0.75f) {
                                color = {1.0f, 1.0f, 0.0f}; // Yellow
                            } else {
                                color = {1.0f, 0.3f, 0.0f}; // Red-Orange
                            }
                            density_colors_.push_back(color);
                        }
                    }
                }
            }
        }

        void render_solid_cubes(VkCommandBuffer cmd, const vk::toolkit::math::Mat4& view_proj) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, solid_pipeline_);
            vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(vk::toolkit::math::Mat4), &view_proj);

            VkBuffer vertex_buffers[] = {vertex_buffer_, instance_buffer_};
            VkDeviceSize offsets[] = {0, 0};
            vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);
            vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, index_count_, static_cast<uint32_t>(voxel_positions_.size()), 0, 0, 0);
        }

        void render_wireframe_grid(VkCommandBuffer cmd, const vk::toolkit::math::Mat4& view_proj) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe_pipeline_);
            vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(vk::toolkit::math::Mat4), &view_proj);

            VkBuffer vertex_buffers[] = {grid_vertex_buffer_, all_grid_instance_buffer_};
            VkDeviceSize offsets[] = {0, 0};
            vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);
            vkCmdBindIndexBuffer(cmd, grid_index_buffer_, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, grid_index_count_, TOTAL_VOXELS, 0, 0, 0);
        }

        void render_points(VkCommandBuffer cmd, const vk::toolkit::math::Mat4& view_proj) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, point_pipeline_);
            vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(vk::toolkit::math::Mat4), &view_proj);

            VkBuffer vertex_buffers[] = {instance_buffer_};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
            vkCmdDraw(cmd, 1, static_cast<uint32_t>(voxel_positions_.size()), 0, 0);
        }

        void render_transparent_shell(VkCommandBuffer cmd, const vk::toolkit::math::Mat4& view_proj) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, transparent_pipeline_);
            vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(vk::toolkit::math::Mat4), &view_proj);

            VkBuffer vertex_buffers[] = {vertex_buffer_, instance_buffer_};
            VkDeviceSize offsets[] = {0, 0};
            vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);
            vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, index_count_, static_cast<uint32_t>(voxel_positions_.size()), 0, 0, 0);
        }

        void render_density_colored(VkCommandBuffer cmd, const vk::toolkit::math::Mat4& view_proj) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, solid_pipeline_);
            vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, sizeof(vk::toolkit::math::Mat4), &view_proj);

            VkBuffer vertex_buffers[] = {vertex_buffer_, instance_buffer_, density_color_buffer_};
            VkDeviceSize offsets[] = {0, 0, 0};
            vkCmdBindVertexBuffers(cmd, 0, 3, vertex_buffers, offsets);
            vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, index_count_, static_cast<uint32_t>(voxel_positions_.size()), 0, 0, 0);
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

        void create_grid_mesh(vk::context::EngineContext* eng) {
            // Create wireframe cube (edges only) for grid visualization
            const float hs = VOXEL_SIZE * 0.5f;
            std::vector<float> vertices = {
                // 8 corners of cube
                -hs, -hs, -hs,  0.0f, 0.0f, 0.0f,
                 hs, -hs, -hs,  0.0f, 0.0f, 0.0f,
                 hs,  hs, -hs,  0.0f, 0.0f, 0.0f,
                -hs,  hs, -hs,  0.0f, 0.0f, 0.0f,
                -hs, -hs,  hs,  0.0f, 0.0f, 0.0f,
                 hs, -hs,  hs,  0.0f, 0.0f, 0.0f,
                 hs,  hs,  hs,  0.0f, 0.0f, 0.0f,
                -hs,  hs,  hs,  0.0f, 0.0f, 0.0f,
            };

            // 12 edges of cube
            std::vector<uint32_t> indices = {
                0, 1,  1, 2,  2, 3,  3, 0,  // Bottom face
                4, 5,  5, 6,  6, 7,  7, 4,  // Top face
                0, 4,  1, 5,  2, 6,  3, 7   // Vertical edges
            };

            grid_index_count_ = static_cast<uint32_t>(indices.size());

            VkBufferCreateInfo buffer_info{};
            buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_info.size = vertices.size() * sizeof(float);
            buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VmaAllocationCreateInfo alloc_info{};
            alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

            vmaCreateBuffer(eng->allocator, &buffer_info, &alloc_info, &grid_vertex_buffer_, &grid_vertex_allocation_, nullptr);

            void* data;
            vmaMapMemory(eng->allocator, grid_vertex_allocation_, &data);
            std::memcpy(data, vertices.data(), vertices.size() * sizeof(float));
            vmaUnmapMemory(eng->allocator, grid_vertex_allocation_);

            buffer_info.size = indices.size() * sizeof(uint32_t);
            buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            vmaCreateBuffer(eng->allocator, &buffer_info, &alloc_info, &grid_index_buffer_, &grid_index_allocation_, nullptr);

            vmaMapMemory(eng->allocator, grid_index_allocation_, &data);
            std::memcpy(data, indices.data(), indices.size() * sizeof(uint32_t));
            vmaUnmapMemory(eng->allocator, grid_index_allocation_);
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

        void create_all_grid_instance_buffer(vk::context::EngineContext* eng) {
            // Create instance data for ALL grid cells (occupied and empty)
            struct GridInstance {
                vk::toolkit::math::Vec3 position;
                float occupied; // 1.0 = occupied, 0.0 = empty
            };

            std::vector<GridInstance> instances;
            instances.reserve(TOTAL_VOXELS);

            for (int z = 0; z < GRID_SIZE; ++z) {
                for (int y = 0; y < GRID_SIZE; ++y) {
                    for (int x = 0; x < GRID_SIZE; ++x) {
                        GridInstance inst;
                        inst.position = vk::toolkit::math::Vec3{
                            static_cast<float>(x) * VOXEL_SIZE - GRID_CENTER,
                            static_cast<float>(y) * VOXEL_SIZE - GRID_CENTER,
                            static_cast<float>(z) * VOXEL_SIZE - GRID_CENTER
                        };
                        inst.occupied = is_occupied(bitmap_, x, y, z) ? 1.0f : 0.0f;
                        instances.push_back(inst);
                    }
                }
            }

            VkBufferCreateInfo buffer_info{};
            buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_info.size = instances.size() * sizeof(GridInstance);
            buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VmaAllocationCreateInfo alloc_info{};
            alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

            vmaCreateBuffer(eng->allocator, &buffer_info, &alloc_info, &all_grid_instance_buffer_, &all_grid_instance_allocation_, nullptr);

            void* data;
            vmaMapMemory(eng->allocator, all_grid_instance_allocation_, &data);
            std::memcpy(data, instances.data(), instances.size() * sizeof(GridInstance));
            vmaUnmapMemory(eng->allocator, all_grid_instance_allocation_);

            // Create density color buffer
            if (!density_colors_.empty()) {
                buffer_info.size = density_colors_.size() * sizeof(vk::toolkit::math::Vec3);
                vmaCreateBuffer(eng->allocator, &buffer_info, &alloc_info, &density_color_buffer_, &density_color_allocation_, nullptr);

                vmaMapMemory(eng->allocator, density_color_allocation_, &data);
                std::memcpy(data, density_colors_.data(), density_colors_.size() * sizeof(vk::toolkit::math::Vec3));
                vmaUnmapMemory(eng->allocator, density_color_allocation_);
            }
        }

        void create_pipelines(vk::context::EngineContext* eng, vk::context::FrameContext* frame) {
            // Load shaders - reuse bitfield shaders for now
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
            push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            push_constant.offset = 0;
            push_constant.size = sizeof(vk::toolkit::math::Mat4);

            VkPipelineLayoutCreateInfo layout_info{};
            layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.pushConstantRangeCount = 1;
            layout_info.pPushConstantRanges = &push_constant;

            vkCreatePipelineLayout(eng->device, &layout_info, nullptr, &pipeline_layout_);

            // Vertex input for solid/transparent
            VkVertexInputBindingDescription bindings[2] = {};
            bindings[0].binding = 0;
            bindings[0].stride = 6 * sizeof(float);
            bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            bindings[1].binding = 1;
            bindings[1].stride = sizeof(vk::toolkit::math::Vec3) + sizeof(float); // position + occupied flag
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

            // Create solid pipeline
            vkCreateGraphicsPipelines(eng->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &solid_pipeline_);

            // Create wireframe pipeline
            rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
            rasterizer.lineWidth = 1.5f;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            vkCreateGraphicsPipelines(eng->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &wireframe_pipeline_);

            // Create point pipeline
            rasterizer.polygonMode = VK_POLYGON_MODE_POINT;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            bindings[0].stride = sizeof(vk::toolkit::math::Vec3);
            vertex_input.vertexBindingDescriptionCount = 1;
            vertex_input.vertexAttributeDescriptionCount = 1;
            vkCreateGraphicsPipelines(eng->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &point_pipeline_);

            // Create transparent pipeline
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            bindings[0].stride = 6 * sizeof(float);
            vertex_input.vertexBindingDescriptionCount = 2;
            vertex_input.vertexAttributeDescriptionCount = 3;
            color_blend_attachment.blendEnable = VK_TRUE;
            color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            depth_stencil.depthWriteEnable = VK_FALSE;
            vkCreateGraphicsPipelines(eng->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &transparent_pipeline_);

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
        std::vector<vk::toolkit::math::Vec3> density_colors_;

        // Mesh buffers
        VkBuffer vertex_buffer_{VK_NULL_HANDLE};
        VkBuffer index_buffer_{VK_NULL_HANDLE};
        VkBuffer grid_vertex_buffer_{VK_NULL_HANDLE};
        VkBuffer grid_index_buffer_{VK_NULL_HANDLE};
        VkBuffer instance_buffer_{VK_NULL_HANDLE};
        VkBuffer all_grid_instance_buffer_{VK_NULL_HANDLE};
        VkBuffer density_color_buffer_{VK_NULL_HANDLE};

        VmaAllocation vertex_allocation_{};
        VmaAllocation index_allocation_{};
        VmaAllocation grid_vertex_allocation_{};
        VmaAllocation grid_index_allocation_{};
        VmaAllocation instance_allocation_{};
        VmaAllocation all_grid_instance_allocation_{};
        VmaAllocation density_color_allocation_{};

        uint32_t vertex_count_{0};
        uint32_t index_count_{0};
        uint32_t grid_index_count_{0};

        // Pipelines
        VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
        VkPipeline solid_pipeline_{VK_NULL_HANDLE};
        VkPipeline wireframe_pipeline_{VK_NULL_HANDLE};
        VkPipeline point_pipeline_{VK_NULL_HANDLE};
        VkPipeline transparent_pipeline_{VK_NULL_HANDLE};

        // Visualization state
        VisualizationMode viz_mode_{VisualizationMode::WireframeGrid};
        bool show_panel_{true};
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

