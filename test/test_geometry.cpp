#include <SDL3/SDL.h>
#include <cmath>
#include <vulkan/vulkan.h>

import vk.engine;
import vk.context;
import vk.plugins.geometry;
import vk.plugins.viewport3d;
import vk.plugins.screenshot;

int main() {
    vk::engine::VulkanEngine engine;
    vk::plugins::Viewport3D viewport;
    vk::plugins::Geometry geometry;
    vk::plugins::Screenshot screenshot;

    vk::plugins::GeometryBatch batch{vk::plugins::GeometryType::Plane, vk::plugins::RenderMode::Filled};
    batch.instances.push_back({
        .position = {0.f, 0.f, 0.f},
        .rotation = {0.f, 0.f, 0.f},
        .scale    = {1.f, 1.f, 1.f},
        .color    = {1.f, 1.f, 1.f},
        .alpha    = 1.0f,
    });
    geometry.add_batch(batch);
    geometry.set_camera_reference(&viewport.camera_);

    engine.init(viewport, geometry, screenshot);
    engine.run(viewport, geometry, screenshot);
    engine.cleanup();

    return 0;
}
