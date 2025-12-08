import vk.engine;
import vk.plugins.viewport3d;

int main() {
    vk::engine::VulkanEngine engine;
    vk::plugins::Viewport3D viewport3d;

    engine.init(viewport3d);
    engine.run(viewport3d);
    engine.cleanup();
}
