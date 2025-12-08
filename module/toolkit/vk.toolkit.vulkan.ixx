module;
#include <format>
#include <fstream>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
export module vk.toolkit.vulkan;
import vk.toolkit.log;

namespace vk::toolkit::vulkan {
    export VkShaderModule load_shader(const char* filename, const VkDevice& device) {
        std::ifstream file(std::string("shader/") + filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) throw std::runtime_error(std::format("Failed to open shader file: {}", filename));

        const size_t file_size = file.tellg();
        std::vector<char> code(file_size);
        file.seekg(0);
        file.read(code.data(), static_cast<std::streamsize>(file_size));

        const VkShaderModuleCreateInfo create_info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = code.size(), .pCode = reinterpret_cast<const uint32_t*>(code.data())};
        VkShaderModule module = VK_NULL_HANDLE;
        log::vk_check(vkCreateShaderModule(device, &create_info, nullptr, &module), "Failed to create shader module");
        return module;
    }
} // namespace vk::toolkit::vulkan
