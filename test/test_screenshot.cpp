import vk.engine;
import vk.plugins.screenshot;

int main() {
    vk::engine::VulkanEngine engine;
    vk::plugins::Screenshot screenshot;

    engine.init(screenshot);
    engine.run(screenshot);
    engine.cleanup();
}
