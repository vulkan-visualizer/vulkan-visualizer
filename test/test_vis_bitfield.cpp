#include <memory>
import vk.engine;
import vk.context;
import vk.plugins.viewport3d;
import vk.plugins.bitmap;
import vk.toolkit.camera;
import vk.toolkit.math;
import vk.toolkit.vulkan;
import vk.toolkit.geometry;

int main() {
    vk::engine::VulkanEngine engine;

    auto bitmap = vk::toolkit::geometry::make_centered_sphere<64, 64, 64>(0.4f);
    auto camera = std::make_shared<vk::toolkit::camera::Camera>();
    vk::plugins::Viewport3D viewport(camera);
    vk::plugins::BitmapViewer bitmap_viewer(camera, bitmap.view());

    engine.init(viewport, bitmap_viewer);
    engine.run(viewport, bitmap_viewer);
    engine.cleanup();
    return 0;
}
