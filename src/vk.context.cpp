module;
#include "VkBootstrap.h"
module vk.context;

void vk::context::DescriptorAllocator::init_pool(VkDevice device, uint32_t maxSets, std::span<const PoolSizeRatio> ratios) {
    maxSets = std::max(1u, maxSets);
    std::vector<VkDescriptorPoolSize> sizes;
    sizes.reserve(ratios.size());
    for (const auto& [type, ratio] : ratios) {
        const uint32_t count = std::max(1u, static_cast<uint32_t>(ratio * static_cast<float>(maxSets)));
        sizes.push_back(VkDescriptorPoolSize{.type = type, .descriptorCount = count});
    }
    const VkDescriptorPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .pNext = nullptr, .flags = 0u, .maxSets = maxSets, .poolSizeCount = static_cast<uint32_t>(sizes.size()), .pPoolSizes = sizes.data()};
    vk_check(vkCreateDescriptorPool(device, &info, nullptr, &pool));
}
void vk::context::DescriptorAllocator::clear_descriptors(VkDevice device) const {
    if (pool) vkResetDescriptorPool(device, pool, 0);
}
void vk::context::DescriptorAllocator::destroy_pool(VkDevice device) const {
    if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
}
VkDescriptorSet vk::context::DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout) const {
    const VkDescriptorSetAllocateInfo ai{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .pNext = nullptr, .descriptorPool = pool, .descriptorSetCount = 1u, .pSetLayouts = &layout};
    VkDescriptorSet ds{};
    vk_check(vkAllocateDescriptorSets(device, &ai, &ds));
    return ds;
}


void vk::context::transition_image_layout(const VkCommandBuffer &cmd, const AttachmentView& target, const VkImageLayout old_layout, const VkImageLayout new_layout) {
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

void vk::context::transition_to_color_attachment(const VkCommandBuffer &cmd, const VkImage image, const VkImageLayout old_layout) {
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
