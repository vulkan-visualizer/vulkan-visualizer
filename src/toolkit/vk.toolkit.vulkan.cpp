module;
#include <vulkan/vulkan.h>
#include <tuple>
module vk.toolkit.vulkan;
import vk.context;

void vk::toolkit::vulkan::transition_image_layout(const VkCommandBuffer& cmd, const context::AttachmentView& target, const VkImageLayout old_layout, const VkImageLayout new_layout) {
    const auto [src_stage, dst_stage, src_access, dst_access] = [&] {
        if (old_layout == VK_IMAGE_LAYOUT_GENERAL && new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            return std::tuple{VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
        }
        return std::tuple{VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    }();

    const VkImageMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = src_stage, .srcAccessMask = src_access, .dstStageMask = dst_stage, .dstAccessMask = dst_access, .oldLayout = old_layout, .newLayout = new_layout, .image = target.image, .subresourceRange = {target.aspect, 0, 1, 0, 1}};

    const VkDependencyInfo dep_info{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};

    vkCmdPipelineBarrier2(cmd, &dep_info);
}

void vk::toolkit::vulkan::transition_to_color_attachment(const VkCommandBuffer& cmd, const VkImage image, const VkImageLayout old_layout) {
    const VkImageMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask                          = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask                         = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask                          = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask                         = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
        .oldLayout                             = old_layout,
        .newLayout                             = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image                                 = image,
        .subresourceRange                      = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

    const VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};

    vkCmdPipelineBarrier2(cmd, &dep);
}