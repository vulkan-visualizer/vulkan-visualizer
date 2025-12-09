module;
#include <format>
#include <fstream>
#include <string>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
export module vk.toolkit.vulkan;
import vk.context;
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

    export void transition_image_layout(const VkCommandBuffer& cmd, const context::AttachmentView& target, VkImageLayout old_layout, VkImageLayout new_layout);
    export void transition_to_color_attachment(const VkCommandBuffer& cmd, VkImage image, VkImageLayout old_layout);

    export void create_buffer_with_data(const vk::context::EngineContext& eng, const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VmaAllocation& allocation) {
        const VkBufferCreateInfo buffer_ci{
            .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size        = size,
            .usage       = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        constexpr VmaAllocationCreateInfo alloc_ci{
            .flags         = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage         = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        };

        VmaAllocationInfo alloc_info{};
        log::vk_check(vmaCreateBuffer(eng.allocator, &buffer_ci, &alloc_ci, &buffer, &allocation, &alloc_info), "Failed to create geometry buffer");

        void* mapped = nullptr;
        vmaMapMemory(eng.allocator, allocation, &mapped);
        std::memcpy(mapped, data, size);
        vmaUnmapMemory(eng.allocator, allocation);
    }
} // namespace vk::toolkit::vulkan
