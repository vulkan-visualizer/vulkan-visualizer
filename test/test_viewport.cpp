import vk.engine;
import vk.plugins;
import vk.camera;

int main() {
    vk::engine::VulkanEngine engine;
    vk::plugins::Viewport3DPlugin viewport_plugin;

    // Initialize engine with viewport plugin
    engine.init(viewport_plugin);

    // Run main loop
    engine.run(viewport_plugin);

    // Cleanup
    engine.cleanup();

    return 0;
}
