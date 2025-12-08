module;
#include <vulkan/vulkan.h>
export module vk.toolkit.imgui;
import vk.context;

namespace vk::toolkit::imgui {
    export void create_imgui(context::EngineContext& eng, VkFormat swapchain_format);
    export void destroy_imgui();
    export void begin_imgui_frame();
    export void end_imgui_frame(const VkCommandBuffer& cmd, const context::FrameContext& frm);
} // namespace vk::toolkit::imgui
