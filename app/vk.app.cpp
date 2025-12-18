module vk.app;
import vk.context;
import vk.swapchain;


vk::app::VulkanApp::VulkanApp(const VulkanAppInfo& info) {
    auto [vkctx, surface]   = context::setup_vk_context_glfw("App", "Engine");
    swapchain::Swapchain sc = swapchain::setup_swapchain(vkctx, surface);

    this->vkctx   = std::move(vkctx);
    this->surface = std::move(surface);
}

void vk::app::VulkanApp::run() {
    return;
}

int main() {
    vk::app::VulkanApp app{{}};
    return 0;
}
