export module vk.app;
import std;
import vk.context;
import vk.swapchain;

namespace vk::app {
    export class VulkanApp {
    public:
        void run();
        struct VulkanAppInfo {
        } Info;

        explicit VulkanApp(const VulkanAppInfo& info);
        ~VulkanApp()                           = default;
        VulkanApp(const VulkanApp&)            = delete;
        VulkanApp& operator=(const VulkanApp&) = delete;
        VulkanApp(VulkanApp&&)                 = delete;
        VulkanApp& operator=(VulkanApp&&)      = delete;

    private:
        context::VulkanContext vkctx;
        context::SurfaceContext surface;
        swapchain::Swapchain swapchain;
    };
} // namespace vk::app
