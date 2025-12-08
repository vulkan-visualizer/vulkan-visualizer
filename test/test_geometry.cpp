#include <memory>
import vk.engine;
import vk.context;
import vk.plugins.geometry;
import vk.plugins.viewport3d;
import vk.plugins.screenshot;
import vk.toolkit.camera;

int main() {
    vk::engine::VulkanEngine engine;
    const auto camera = std::make_shared<vk::toolkit::camera::Camera>();
    vk::plugins::Viewport3D viewport(camera);
    vk::plugins::Geometry geometry(camera);
    vk::plugins::Screenshot screenshot;

    vk::plugins::GeometryBatch batch{vk::plugins::GeometryType::Sphere, vk::plugins::RenderMode::Filled};
    batch.instances.push_back({
        .position = {0.f, 0.f, 0.f},
        .rotation = {0.f, 0.f, 0.f},
        .scale    = {1.f, 1.f, 1.f},
        .color    = {1.f, 1.f, 1.f},
        .alpha    = 1.0f,
    });
    geometry.add_batch(batch);

    engine.init(viewport, geometry, screenshot);
    engine.run(viewport, geometry, screenshot);
    engine.cleanup();

    return 0;
}
