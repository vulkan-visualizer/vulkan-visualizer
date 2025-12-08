#include <SDL3/SDL.h>
#include <cmath>
#include <vulkan/vulkan.h>

import vk.engine;
import vk.context;
import vk.plugins;
import vk.camera;

int main() {
    vk::engine::VulkanEngine engine;
    vk::plugins::Viewport3DPlugin viewport_plugin;
    vk::plugins::GeometryPlugin geometry_plugin;
    vk::plugins::ScreenshotPlugin screenshot_plugin;

    geometry_plugin.set_viewport_plugin(&viewport_plugin);
    geometry_plugin.set_show_face_normals(false);
    geometry_plugin.set_normal_length(0.15f);

    auto add_single = [&](vk::plugins::GeometryType type, const vk::camera::Vec3& pos, const vk::camera::Vec3& rot, const vk::camera::Vec3& scale, const vk::camera::Vec3& color) {
        vk::plugins::GeometryBatch batch{type, vk::plugins::RenderMode::Filled};
        batch.instances.push_back({pos, rot, scale, color, 1.0f});
        geometry_plugin.add_batch(batch);
    };

    // Arrange all built-in geometries in a grid
    const float spacing = 3.0f;
    int row             = 0;
    int col             = 0;
    auto place          = [&](vk::plugins::GeometryType type, const vk::camera::Vec3& scale, const vk::camera::Vec3& color) {
        const vk::camera::Vec3 pos{col * spacing, 0.0f, row * spacing};
        add_single(type, pos, {0, 0, 0}, scale, color);
        col++;
        if (col >= 4) {
            col = 0;
            row++;
        }
    };

    place(vk::plugins::GeometryType::Sphere, {1, 1, 1}, {1.0f, 1.0f, 1.0f});
    place(vk::plugins::GeometryType::Box, {1, 1, 1}, {1.0f, 1.0f, 1.0f});
    place(vk::plugins::GeometryType::Cylinder, {1, 1, 1}, {1.0f, 1.0f, 1.0f});
    place(vk::plugins::GeometryType::Cone, {1, 1.5f, 1}, {1.0f, 1.0f, 1.0f});
    place(vk::plugins::GeometryType::Torus, {1, 1, 1}, {1.0f, 1.0f, 1.0f});
    place(vk::plugins::GeometryType::Capsule, {1, 1, 1}, {1.0f, 1.0f, 1.0f});
    place(vk::plugins::GeometryType::Plane, {3, 1, 3}, {1.0f, 1.0f, 1.0f});
    place(vk::plugins::GeometryType::Circle, {2, 1, 2}, {1.0f, 1.0f, 1.0f});

    // Lines/Grid/Rays for completeness (wireframe-only)
    geometry_plugin.add_grid({-6.0f, -1.5f, -6.0f}, 12.0f, 12, {0.5f, 0.5f, 0.5f});
    geometry_plugin.add_line({-5, 0, -5}, {-3, 1, -3}, {1, 0, 0});
    geometry_plugin.add_ray({-5, 0.5f, -1}, {1, 0.2f, 0.4f}, 2.0f, {1, 1, 0});

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
