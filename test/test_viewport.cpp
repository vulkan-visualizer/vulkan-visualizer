import vk.engine;
import vk.plugins.viewport;

int main() {
    vk::engine::VulkanEngine engine;
    vk::plugins::ViewportRenderer renderer;
    vk::plugins::ViewportUI ui_system;
    vk::plugins::ViewpoertPlugin plugin;

    engine.init(renderer, ui_system, plugin);
    engine.run(renderer, ui_system, plugin);
    engine.cleanup();

    return 0;
}
