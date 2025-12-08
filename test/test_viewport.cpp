#include <memory>
import vk.engine;
import vk.context;
import vk.plugins.viewport3d;

int main() {
    vk::engine::VulkanEngine engine;
    vk::plugins::Viewport3D viewport3d(std::make_shared<vk::context::Camera>());

    engine.init(viewport3d);
    engine.run(viewport3d);
    engine.cleanup();
}
