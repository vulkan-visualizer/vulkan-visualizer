#include <SDL3/SDL.h>
#include <cmath>
#include <vulkan/vulkan.h>

import vk.engine;
import vk.context;
import vk.plugins;
import vk.camera;

// ============================================================================
// GeometryPlugin Test - Demonstrates various geometry types
// ============================================================================

int main() {
    vk::engine::VulkanEngine engine;
    vk::plugins::Viewport3DPlugin viewport_plugin;
    vk::plugins::GeometryPlugin geometry_plugin;
    vk::plugins::ScreenshotPlugin screenshot_plugin;

    // Wire up plugins
    geometry_plugin.set_viewport_plugin(&viewport_plugin);

    // ========================================================================
    // Example 1: Single geometry objects
    // ========================================================================

    // Add a red sphere at origin
    geometry_plugin.add_sphere({0, 0, 0}, 1.0f, {1, 0.2f, 0.2f});

    // Add a green wireframe box
    geometry_plugin.add_box({3, 0, 0}, {1, 1, 1}, {0.2f, 1, 0.2f},
                            vk::plugins::RenderMode::Wireframe);

    // Add a blue torus (both filled and wireframe)
    vk::plugins::GeometryBatch torus_batch{vk::plugins::GeometryType::Torus,
                                           vk::plugins::RenderMode::Both};
    torus_batch.instances.push_back({{-3, 0, 0}, {0, 0, 0}, {1, 1, 1}, {0.2f, 0.2f, 1}, 1.0f});
    geometry_plugin.add_batch(torus_batch);

    // ========================================================================
    // Example 2: Grid of spheres (instanced rendering!)
    // ========================================================================

    vk::plugins::GeometryBatch sphere_grid{vk::plugins::GeometryType::Sphere,
                                           vk::plugins::RenderMode::Filled};
    for (int x = 0; x < 10; x++) {
        for (int z = 0; z < 10; z++) {
            sphere_grid.instances.push_back({
                {x * 2.0f - 9.0f, -2.0f, z * 2.0f - 9.0f},  // position
                {0, 0, 0},                                   // rotation
                {0.3f, 0.3f, 0.3f},                         // scale
                {x/10.0f, 0.5f, z/10.0f},                   // color (gradient)
                1.0f                                         // alpha
            });
        }
    }
    geometry_plugin.add_batch(sphere_grid);

    // ========================================================================
    // Example 3: Rotating cylinders
    // ========================================================================

    vk::plugins::GeometryBatch cylinders{vk::plugins::GeometryType::Cylinder,
                                         vk::plugins::RenderMode::Filled};
    for (int i = 0; i < 8; i++) {
        float angle = i * 45.0f;
        float rad = angle * 3.14159f / 180.0f;
        cylinders.instances.push_back({
            {std::cos(rad) * 5.0f, 1.0f, std::sin(rad) * 5.0f},  // circular arrangement
            {0, angle, 0},                                        // rotate around Y
            {0.5f, 1.0f, 0.5f},                                  // scale
            {1.0f, 0.8f, 0.2f},                                  // golden color
            1.0f
        });
    }
    geometry_plugin.add_batch(cylinders);

    // ========================================================================
    // Example 4: Lines and rays
    // ========================================================================

    // Coordinate axes
    geometry_plugin.add_line({0, 0, 0}, {2, 0, 0}, {1, 0, 0});  // X axis (red)
    geometry_plugin.add_line({0, 0, 0}, {0, 2, 0}, {0, 1, 0});  // Y axis (green)
    geometry_plugin.add_line({0, 0, 0}, {0, 0, 2}, {0, 0, 1});  // Z axis (blue)

    // Some random rays
    vk::plugins::GeometryBatch rays{vk::plugins::GeometryType::Ray,
                                    vk::plugins::RenderMode::Wireframe};
    for (int i = 0; i < 20; i++) {
        float angle = i * 18.0f;
        float rad = angle * 3.14159f / 180.0f;
        vk::camera::Vec3 direction{std::cos(rad), 0.5f, std::sin(rad)};
        geometry_plugin.add_ray({0, 2, 0}, direction.normalized(), 3.0f, {1, 1, 0});
    }

    // ========================================================================
    // Example 5: Ground plane and grid
    // ========================================================================

    // Large ground plane
    vk::plugins::GeometryBatch ground{vk::plugins::GeometryType::Plane,
                                      vk::plugins::RenderMode::Filled};
    ground.instances.push_back({{0, -3, 0}, {0, 0, 0}, {20, 1, 20}, {0.3f, 0.3f, 0.3f}, 1.0f});
    geometry_plugin.add_batch(ground);

    // Grid overlay
    geometry_plugin.add_grid({0, -2.99f, 0}, 20.0f, 20, {0.5f, 0.5f, 0.5f});

    // ========================================================================
    // Example 6: Mixed geometry showcase
    // ========================================================================

    // Cone
    vk::plugins::GeometryBatch cone{vk::plugins::GeometryType::Cone,
                                    vk::plugins::RenderMode::Both};
    cone.instances.push_back({{0, 3, -5}, {0, 0, 0}, {1, 2, 1}, {0.8f, 0.4f, 0.8f}, 1.0f});
    geometry_plugin.add_batch(cone);

    // Capsules
    vk::plugins::GeometryBatch capsules{vk::plugins::GeometryType::Capsule,
                                        vk::plugins::RenderMode::Filled};
    capsules.instances.push_back({{-5, 1, -5}, {0, 0, 0}, {1.5f, 1.5f, 1.5f}, {0.2f, 0.8f, 0.8f}, 1.0f});
    capsules.instances.push_back({{5, 1, -5}, {0, 0, 90}, {1.5f, 1.5f, 1.5f}, {0.8f, 0.2f, 0.8f}, 1.0f});
    geometry_plugin.add_batch(capsules);

    // ========================================================================
    // Initialize engine with all plugins
    // ========================================================================

    engine.init(viewport_plugin, geometry_plugin, screenshot_plugin);

    // Run main loop
    engine.run(viewport_plugin, geometry_plugin, screenshot_plugin);

    // Cleanup
    engine.cleanup();

    return 0;
}

