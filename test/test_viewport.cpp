import vk.engine;
import vk.plugins.viewport;
import vk.camera;

int main() {
    vk::engine::VulkanEngine engine;
    vk::plugins::ViewportRenderer renderer;
    vk::plugins::ViewportUI ui_system;
    vk::plugins::ViewpoertPlugin plugin;

    // Wire up camera references
    renderer.set_camera(&plugin.get_camera());
    ui_system.set_camera(&plugin.get_camera());

    // Set initial viewport size
    plugin.set_viewport_size(1920, 1280);

    engine.init(renderer, ui_system, plugin);
    engine.run(renderer, ui_system, plugin);
    engine.cleanup();

    return 0;
}
