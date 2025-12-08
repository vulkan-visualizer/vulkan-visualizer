#include <SDL3/SDL.h>
#include <cmath>
#include <numbers>
#include <print>
#include <random>
#include <vulkan/vulkan.h>

import vk.engine;
import vk.context;
import vk.plugins;
import vk.camera;

// ============================================================================
// NeRF Ray Sampling Visualization Test
// ============================================================================
// This tool visualizes the complete NeRF ray generation pipeline:
// - Camera position and orientation
// - Camera frustum
// - Image plane with sampling grid
// - Training space AABB (bounding volume)
// - Rays from camera through image plane pixels
// - Ray-AABB intersections (entry/exit points)
// ============================================================================

namespace {
    // NeRF Training Configuration
    struct NeRFConfig {
        // Camera parameters
        vk::camera::Vec3 camera_position{0, 2, 5};
        vk::camera::Vec3 camera_target{0, 0, 0};
        float fov_deg{50.0f};
        float aspect_ratio{16.0f / 9.0f};

        // Image sampling
        int image_width{800};
        int image_height{450};
        int sample_rays_x{16};  // Number of rays to sample in X direction
        int sample_rays_y{9};   // Number of rays to sample in Y direction

        // Training space AABB
        vk::camera::Vec3 aabb_min{-2, -2, -2};
        vk::camera::Vec3 aabb_max{2, 2, 2};

        // Visualization
        float image_plane_distance{1.0f};
        bool show_frustum{false};
        bool show_image_plane{false};
        bool show_aabb{false};
        bool show_rays{true};
        bool show_all_rays{true};  // If false, shows subset
        bool show_coordinate_axes{true};

        // Ray appearance
        float ray_length{10.0f};
        vk::camera::Vec3 ray_color{1, 0.5f, 0};
        vk::camera::Vec3 hit_color{0, 1, 0.2f};
    };

    // Generate rays from camera through image plane
    struct Ray {
        vk::camera::Vec3 origin;
        vk::camera::Vec3 direction;
    };

    // Calculate camera basis vectors from position and target
    void compute_camera_basis(const vk::camera::Vec3& position, const vk::camera::Vec3& target,
                              vk::camera::Vec3& forward, vk::camera::Vec3& right, vk::camera::Vec3& up) {
        forward = (target - position).normalized();
        const vk::camera::Vec3 world_up{0, 1, 0};
        right = forward.cross(world_up).normalized();
        up = right.cross(forward).normalized();
    }

    // Generate a single ray through a pixel
    Ray generate_ray(const NeRFConfig& config, int pixel_x, int pixel_y) {
        // Compute camera basis
        vk::camera::Vec3 forward, right, up;
        compute_camera_basis(config.camera_position, config.camera_target, forward, right, up);

        // Convert pixel coordinates to normalized device coordinates [-1, 1]
        const float ndc_x = (static_cast<float>(pixel_x) + 0.5f) / static_cast<float>(config.image_width) * 2.0f - 1.0f;
        const float ndc_y = 1.0f - (static_cast<float>(pixel_y) + 0.5f) / static_cast<float>(config.image_height) * 2.0f;

        // Convert to image plane coordinates
        const float fov_rad = config.fov_deg * std::numbers::pi_v<float> / 180.0f;
        const float tan_half_fov = std::tan(fov_rad * 0.5f);

        const float plane_half_height = tan_half_fov * config.image_plane_distance;
        const float plane_half_width = plane_half_height * config.aspect_ratio;

        const float plane_x = ndc_x * plane_half_width;
        const float plane_y = ndc_y * plane_half_height;

        // Point on image plane
        const auto plane_point = config.camera_position +
                                forward * config.image_plane_distance +
                                right * plane_x +
                                up * plane_y;

        // Ray direction from camera to plane point
        const auto direction = (plane_point - config.camera_position).normalized();

        return {config.camera_position, direction};
    }

    // Generate stratified ray samples (uniform grid)
    std::vector<Ray> generate_ray_samples(const NeRFConfig& config) {
        std::vector<Ray> rays;
        rays.reserve(config.sample_rays_x * config.sample_rays_y);

        for (int y = 0; y < config.sample_rays_y; ++y) {
            for (int x = 0; x < config.sample_rays_x; ++x) {
                // Sample at regular intervals
                const int pixel_x = (x * config.image_width) / config.sample_rays_x;
                const int pixel_y = (y * config.image_height) / config.sample_rays_y;

                rays.push_back(generate_ray(config, pixel_x, pixel_y));
            }
        }

        return rays;
    }

    // Generate random ray samples (for stress testing)
    std::vector<Ray> generate_random_rays(const NeRFConfig& config, int num_rays) {
        std::vector<Ray> rays;
        rays.reserve(num_rays);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist_x(0, config.image_width - 1);
        std::uniform_int_distribution<> dist_y(0, config.image_height - 1);

        for (int i = 0; i < num_rays; ++i) {
            const int pixel_x = dist_x(gen);
            const int pixel_y = dist_y(gen);
            rays.push_back(generate_ray(config, pixel_x, pixel_y));
        }

        return rays;
    }
}

int main() {
    vk::engine::VulkanEngine engine;
    vk::plugins::Viewport3DPlugin viewport_plugin;
    vk::plugins::GeometryPlugin geometry_plugin;
    vk::plugins::ScreenshotPlugin screenshot_plugin;

    // Wire up plugins
    geometry_plugin.set_viewport_plugin(&viewport_plugin);

    // Configuration
    NeRFConfig config;

    std::println("=== NeRF Ray Sampling Visualizer ===");
    std::println("Camera: [{}, {}, {}]", config.camera_position.x, config.camera_position.y, config.camera_position.z);
    std::println("Target: [{}, {}, {}]", config.camera_target.x, config.camera_target.y, config.camera_target.z);
    std::println("FOV: {}°, Aspect: {:.2f}", config.fov_deg, config.aspect_ratio);
    std::println("Image: {}x{}", config.image_width, config.image_height);
    std::println("Ray samples: {}x{} = {}", config.sample_rays_x, config.sample_rays_y,
                config.sample_rays_x * config.sample_rays_y);
    std::println("AABB: [{}, {}, {}] to [{}, {}, {}]",
                config.aabb_min.x, config.aabb_min.y, config.aabb_min.z,
                config.aabb_max.x, config.aabb_max.y, config.aabb_max.z);
    std::println("=====================================\n");

    // Compute camera basis
    vk::camera::Vec3 forward, right, up;
    compute_camera_basis(config.camera_position, config.camera_target, forward, right, up);

    // ========================================================================
    // 1. Visualize World Coordinate System
    // ========================================================================
    if (config.show_coordinate_axes) {
        geometry_plugin.add_coordinate_axes({0, 0, 0}, 1.5f);
        std::println("[Visualization] World coordinate axes added");
    }

    // ========================================================================
    // 2. Visualize Training Space AABB
    // ========================================================================
    if (config.show_aabb) {
        geometry_plugin.add_aabb(config.aabb_min, config.aabb_max, {0, 1, 1}, vk::plugins::RenderMode::Wireframe);

        // Mark AABB corners
        const float corner_size = 0.08f;
        for (int i = 0; i < 8; ++i) {
            const vk::camera::Vec3 corner{
                (i & 1) ? config.aabb_max.x : config.aabb_min.x,
                (i & 2) ? config.aabb_max.y : config.aabb_min.y,
                (i & 4) ? config.aabb_max.z : config.aabb_min.z
            };
            geometry_plugin.add_sphere(corner, corner_size, {0, 1, 1}, vk::plugins::RenderMode::Filled);
        }

        std::println("[Visualization] AABB with 8 corner markers added");
    }

    // ========================================================================
    // 3. Visualize Camera Position and Orientation
    // ========================================================================
    // Camera position marker
    geometry_plugin.add_sphere(config.camera_position, 0.15f, {1, 1, 0}, vk::plugins::RenderMode::Filled);

    // Camera coordinate system
    const float axis_length = 0.5f;
    geometry_plugin.add_line(config.camera_position, config.camera_position + right * axis_length, {1, 0, 0});
    geometry_plugin.add_line(config.camera_position, config.camera_position + up * axis_length, {0, 1, 0});
    geometry_plugin.add_line(config.camera_position, config.camera_position + forward * axis_length, {0, 0, 1});

    // Line to target
    geometry_plugin.add_line(config.camera_position, config.camera_target, {1, 1, 0.3f});
    geometry_plugin.add_sphere(config.camera_target, 0.1f, {1, 1, 0.3f}, vk::plugins::RenderMode::Filled);

    std::println("[Visualization] Camera position and orientation added");

    // ========================================================================
    // 4. Visualize Camera Frustum
    // ========================================================================
    if (config.show_frustum) {
        constexpr float near_dist = 0.5f;
        constexpr float far_dist = 8.0f;
        geometry_plugin.add_camera_frustum(config.camera_position, forward, up,
                                          config.fov_deg, config.aspect_ratio,
                                          near_dist, far_dist, {1, 0.8f, 0});
        std::println("[Visualization] Camera frustum added");
    }

    // ========================================================================
    // 5. Visualize Image Plane
    // ========================================================================
    if (config.show_image_plane) {
        const int grid_divisions = 10;
        geometry_plugin.add_image_plane(config.camera_position, forward, up,
                                       config.fov_deg, config.aspect_ratio,
                                       config.image_plane_distance, grid_divisions,
                                       {0.6f, 0.6f, 0.6f});

        // Mark image plane center
        const auto plane_center = config.camera_position + forward * config.image_plane_distance;
        geometry_plugin.add_sphere(plane_center, 0.05f, {0.6f, 0.6f, 0.6f}, vk::plugins::RenderMode::Filled);

        std::println("[Visualization] Image plane ({}x{} grid) added", grid_divisions, grid_divisions);
    }

    // ========================================================================
    // 6. Generate and Visualize Ray Samples
    // ========================================================================
    if (config.show_rays) {
        const auto rays = config.show_all_rays
            ? generate_random_rays(config, 1000)  // Many random rays
            : generate_ray_samples(config);        // Grid samples

        std::println("[Ray Generation] Generated {} rays", rays.size());

        // Convert to GeometryPlugin format and add with AABB intersection
        std::vector<vk::plugins::GeometryPlugin::RayInfo> ray_infos;
        ray_infos.reserve(rays.size());

        for (const auto& ray : rays) {
            ray_infos.push_back({
                ray.origin,
                ray.direction,
                config.ray_length,
                config.ray_color
            });
        }

        // Use batch rendering with AABB intersection
        geometry_plugin.add_ray_batch(ray_infos, &config.aabb_min, &config.aabb_max);

        std::println("[Visualization] {} rays with AABB intersection added", rays.size());

        // Mark sample points on image plane
        const float fov_rad = config.fov_deg * std::numbers::pi_v<float> / 180.0f;
        const float tan_half_fov = std::tan(fov_rad * 0.5f);
        const float plane_half_height = tan_half_fov * config.image_plane_distance;
        const float plane_half_width = plane_half_height * config.aspect_ratio;

        int sample_point_count = 0;
        for (int y = 0; y < config.sample_rays_y; ++y) {
            for (int x = 0; x < config.sample_rays_x; ++x) {
                const int pixel_x = (x * config.image_width) / config.sample_rays_x;
                const int pixel_y = (y * config.image_height) / config.sample_rays_y;

                const float ndc_x = (static_cast<float>(pixel_x) + 0.5f) / static_cast<float>(config.image_width) * 2.0f - 1.0f;
                const float ndc_y = 1.0f - (static_cast<float>(pixel_y) + 0.5f) / static_cast<float>(config.image_height) * 2.0f;

                const float plane_x = ndc_x * plane_half_width;
                const float plane_y = ndc_y * plane_half_height;

                const auto sample_point = config.camera_position +
                                        forward * config.image_plane_distance +
                                        right * plane_x +
                                        up * plane_y;

                geometry_plugin.add_sphere(sample_point, 0.03f, {1, 0.5f, 0}, vk::plugins::RenderMode::Filled);
                sample_point_count++;
            }
        }

        std::println("[Visualization] {} sample points on image plane added", sample_point_count);
    }

    // ========================================================================
    // 7. Add Reference Grid on Ground
    // ========================================================================
    geometry_plugin.add_grid({0, -2.5f, 0}, 10.0f, 20, {0.3f, 0.3f, 0.3f});
    std::println("[Visualization] Ground reference grid added");

    // ========================================================================
    // Summary
    // ========================================================================
    std::println("\n=== Visualization Summary ===");
    std::println("Total geometry batches: {}", geometry_plugin.batch_count());
    std::println("\nControls:");
    std::println("  - Mouse + Space/Alt: Rotate, Pan, Zoom camera");
    std::println("  - H: Reset to home view");
    std::println("  - F1: Take screenshot");
    std::println("  - ESC: Exit");
    std::println("\nColor Legend:");
    std::println("  - Yellow: NeRF camera position");
    std::println("  - Cyan: Training space AABB");
    std::println("  - Orange: Rays from camera");
    std::println("  - Green: Ray-AABB intersection points");
    std::println("  - Gray: Image plane with sampling grid");
    std::println("  - Red/Green/Blue: X/Y/Z axes");
    std::println("==============================\n");

    // Initialize and run engine
    engine.init(viewport_plugin, geometry_plugin, screenshot_plugin);
    engine.run(viewport_plugin, geometry_plugin, screenshot_plugin);
    engine.cleanup();

    return 0;
}

