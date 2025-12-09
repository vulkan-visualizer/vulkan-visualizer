import vk.engine;
import vk.context;
import vk.plugins.viewport3d;
import vk.toolkit.camera;
import vk.toolkit.math;
import vk.toolkit.vulkan;

int main() {
    vk::engine::VulkanEngine engine;
    engine.init();
    engine.run();
    engine.cleanup();
    return 0;
}
