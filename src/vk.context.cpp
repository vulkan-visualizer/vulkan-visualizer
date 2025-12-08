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
