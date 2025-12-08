module;
#include <format>
#include <stdexcept>
#include <vulkan/vulkan.h>
export module vk.toolkit.log;

namespace vk::toolkit::log {
    export constexpr auto reset   = "\033[0m";
    export constexpr auto bold    = "\033[1m";
    export constexpr auto red     = "\033[31m";
    export constexpr auto green   = "\033[32m";
    export constexpr auto yellow  = "\033[33m";
    export constexpr auto blue    = "\033[34m";
    export constexpr auto magenta = "\033[35m";
    export constexpr auto cyan    = "\033[36m";

    export void vk_check(const VkResult result, const char* operation = "") {
        if (result != VK_SUCCESS) {
            throw std::runtime_error(std::format("{}{}Vulkan Error{}: {} (code: {})", toolkit::log::bold, toolkit::log::red, toolkit::log::reset, operation, static_cast<int>(result)));
        }
    }
} // namespace vk::toolkit::log
