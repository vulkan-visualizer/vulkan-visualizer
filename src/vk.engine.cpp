module;
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <print>
#include <ranges>
#include <stb_image_write.h>
module vk.engine;

// clang-format off
#ifndef IF_NOT_NULL_DO
#define IF_NOT_NULL_DO(ptr, stmt) do { if ((ptr) != nullptr) { stmt; } } while (false)
#endif
#ifndef IF_NOT_NULL_DO_AND_SET
#define IF_NOT_NULL_DO_AND_SET(ptr, stmt, val) do { if ((ptr) != nullptr) { stmt; (ptr) = (val); } } while (false)
#endif
#ifndef REQUIRE_TRUE
#define REQUIRE_TRUE(expr, msg) do { if (!(expr)) { throw std::runtime_error(std::string("Check failed: ") + #expr + " | " + (msg)); } } while (false)
#endif
// clang-format on

void vk::engine::VulkanEngine::process_capacity() {
    auto ensure_ext = [&](const char* name) {
        if (std::ranges::find(renderer_caps_.extra_device_extensions, name) == renderer_caps_.extra_device_extensions.end()) renderer_caps_.extra_device_extensions.push_back(name);
    };
    if (renderer_caps_.need_acceleration_structure) {
        ensure_ext(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        ensure_ext(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    }
    if (renderer_caps_.need_ray_tracing_pipeline) {
        ensure_ext(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        ensure_ext(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        ensure_ext(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    }
    if (renderer_caps_.need_ray_query) {
        ensure_ext(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        ensure_ext(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    }
    if (renderer_caps_.need_mesh_shader) {
        ensure_ext(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    }
    if (renderer_caps_.buffer_device_address) {
        ensure_ext(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    }
    renderer_caps_.swapchain_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (renderer_caps_.presentation_mode != context::PresentationMode::DirectToSwapchain && renderer_caps_.color_attachments.empty()) renderer_caps_.color_attachments.push_back(context::AttachmentRequest{.name = "hdr_color"});
    if (renderer_caps_.presentation_attachment.empty() && !renderer_caps_.color_attachments.empty()) renderer_caps_.presentation_attachment = renderer_caps_.color_attachments.front().name;
    bool found = false;
    for (const auto& att : renderer_caps_.color_attachments) {
        if (att.name == renderer_caps_.presentation_attachment) {
            found = true;
            break;
        }
    }
    if (!found && !renderer_caps_.color_attachments.empty()) renderer_caps_.presentation_attachment = renderer_caps_.color_attachments.front().name;
    for (auto& att : renderer_caps_.color_attachments) {
        if (att.aspect == 0) att.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        if (att.samples == VK_SAMPLE_COUNT_1_BIT) att.samples = renderer_caps_.color_samples;
        if (renderer_caps_.presentation_mode == context::PresentationMode::EngineBlit && att.name == renderer_caps_.presentation_attachment) att.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (renderer_caps_.presentation_mode == context::PresentationMode::EngineBlit) renderer_caps_.swapchain_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (renderer_caps_.uses_depth == VK_TRUE && !renderer_caps_.depth_attachment.has_value()) {
        renderer_caps_.depth_attachment =
            context::AttachmentRequest{.name = "depth", .format = renderer_caps_.preferred_depth_format, .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, .samples = renderer_caps_.color_samples, .aspect = VK_IMAGE_ASPECT_DEPTH_BIT, .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED};
    }
    if (renderer_caps_.depth_attachment) {
        renderer_caps_.uses_depth = VK_TRUE;
        if (renderer_caps_.depth_attachment->aspect == 0) renderer_caps_.depth_attachment->aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (renderer_caps_.depth_attachment->samples == VK_SAMPLE_COUNT_1_BIT) renderer_caps_.depth_attachment->samples = renderer_caps_.color_samples;
    } else {
        renderer_caps_.uses_depth = VK_FALSE;
    }
    renderer_caps_.uses_offscreen = renderer_caps_.color_attachments.empty() ? VK_FALSE : VK_TRUE;
    if (renderer_caps_.presentation_mode == context::PresentationMode::DirectToSwapchain) {
        renderer_caps_.uses_offscreen = VK_FALSE;
        renderer_caps_.presentation_attachment.clear();
    }
}
void vk::engine::VulkanEngine::create_context() {
    vkb::InstanceBuilder ib;
    ib.set_app_name(this->state_.name.c_str()).request_validation_layers(false).use_default_debug_messenger().require_api_version(1, 3, 0);
    for (const char* ext : renderer_caps_.extra_instance_extensions) ib.enable_extension(ext);
    const vkb::Instance vkb_inst = ib.build().value();
    ctx_.instance                = vkb_inst.instance;
    ctx_.debug_messenger         = vkb_inst.debug_messenger;
    const int sdl_init_rc        = SDL_Init(SDL_INIT_VIDEO);
    REQUIRE_TRUE(sdl_init_rc, std::string("SDL_Init failed: ") + SDL_GetError());
    ctx_.window = SDL_CreateWindow(this->state_.name.c_str(), static_cast<int>(this->state_.width), static_cast<int>(this->state_.height), SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    REQUIRE_TRUE(ctx_.window != nullptr, std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    REQUIRE_TRUE(SDL_Vulkan_CreateSurface(ctx_.window, ctx_.instance, nullptr, &ctx_.surface), std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    VkPhysicalDeviceVulkan13Features f13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = nullptr, .synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE};
    VkPhysicalDeviceVulkan12Features f12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &f13, .descriptorIndexing = VK_TRUE, .bufferDeviceAddress = renderer_caps_.buffer_device_address ? VK_TRUE : VK_FALSE};
    vkb::PhysicalDeviceSelector selector(vkb_inst);
    selector.set_surface(ctx_.surface).set_minimum_version(1, 3).set_required_features_12(f12);
    for (const char* ext : renderer_caps_.extra_device_extensions) selector.add_required_extension(ext);
    vkb::PhysicalDevice phys = selector.select().value();
    ctx_.physical            = phys.physical_device;
    vkb::DeviceBuilder db(phys);
    vkb::Device vkbDev         = db.build().value();
    ctx_.device                = vkbDev.device;
    ctx_.graphics_queue        = vkbDev.get_queue(vkb::QueueType::graphics).value();
    ctx_.compute_queue         = vkbDev.get_queue(vkb::QueueType::compute).value();
    ctx_.transfer_queue        = vkbDev.get_queue(vkb::QueueType::transfer).value();
    ctx_.present_queue         = ctx_.graphics_queue;
    ctx_.graphics_queue_family = vkbDev.get_queue_index(vkb::QueueType::graphics).value();
    ctx_.compute_queue_family  = vkbDev.get_queue_index(vkb::QueueType::compute).value();
    ctx_.transfer_queue_family = vkbDev.get_queue_index(vkb::QueueType::transfer).value();
    ctx_.present_queue_family  = ctx_.graphics_queue_family;
    VmaAllocatorCreateInfo ac{.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, .physicalDevice = ctx_.physical, .device = ctx_.device, .instance = ctx_.instance, .vulkanApiVersion = VK_API_VERSION_1_3};
    context::vk_check(vmaCreateAllocator(&ac, &ctx_.allocator));
    mdq_.emplace_back([&] { vmaDestroyAllocator(ctx_.allocator); });

    std::vector<context::DescriptorAllocator::PoolSizeRatio> sizes = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2.0f}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.0f}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4.0f}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4.0f}};
    ctx_.descriptor_allocator.init_pool(ctx_.device, 128, sizes);
    mdq_.emplace_back([&] { ctx_.descriptor_allocator.destroy_pool(ctx_.device); });

    VkSemaphoreTypeCreateInfo type_ci{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, .pNext = nullptr, .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = 0};
    VkSemaphoreCreateInfo sem_ci{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &type_ci, .flags = 0u};
    context::vk_check(vkCreateSemaphore(ctx_.device, &sem_ci, nullptr, &render_timeline_));
    mdq_.emplace_back([&] { vkDestroySemaphore(ctx_.device, render_timeline_, nullptr); });
    timeline_value_ = 0;
}
void vk::engine::VulkanEngine::destroy_context() {
    for (auto& f : std::ranges::reverse_view(mdq_)) f();
    mdq_.clear();
    IF_NOT_NULL_DO_AND_SET(ctx_.device, vkDestroyDevice(ctx_.device, nullptr), nullptr);
    IF_NOT_NULL_DO_AND_SET(ctx_.surface, vkDestroySurfaceKHR(ctx_.instance, ctx_.surface, nullptr), nullptr);
    IF_NOT_NULL_DO_AND_SET(ctx_.window, SDL_DestroyWindow(ctx_.window), nullptr);
    IF_NOT_NULL_DO_AND_SET(
        ctx_.debug_messenger,
        {
            const auto f = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(ctx_.instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (ctx_.instance && f) f(ctx_.instance, ctx_.debug_messenger, nullptr);
        },
        nullptr);
    IF_NOT_NULL_DO_AND_SET(ctx_.instance, vkDestroyInstance(ctx_.instance, nullptr), nullptr);
    SDL_Quit();
}
void vk::engine::VulkanEngine::create_swapchain() {
    swapchain_.swapchain_image_format = renderer_caps_.preferred_swapchain_format;
    VkSurfaceFormatKHR surface_fmt{swapchain_.swapchain_image_format, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    vkb::Swapchain sc                = vkb::SwapchainBuilder(ctx_.physical, ctx_.device, ctx_.surface).set_desired_format(surface_fmt).set_desired_present_mode(renderer_caps_.present_mode).set_desired_extent(this->state_.width, this->state_.height).add_image_usage_flags(renderer_caps_.swapchain_usage).build().value();
    swapchain_.swapchain             = sc.swapchain;
    swapchain_.swapchain_extent      = sc.extent;
    swapchain_.swapchain_images      = sc.get_images().value();
    swapchain_.swapchain_image_views = sc.get_image_views().value();
    mdq_.emplace_back([&] { destroy_swapchain(); });
}
void vk::engine::VulkanEngine::destroy_swapchain() {
    for (auto v : swapchain_.swapchain_image_views) IF_NOT_NULL_DO_AND_SET(v, vkDestroyImageView(ctx_.device, v, nullptr), VK_NULL_HANDLE);
    swapchain_.swapchain_image_views.clear();
    swapchain_.swapchain_images.clear();
    IF_NOT_NULL_DO_AND_SET(swapchain_.swapchain, vkDestroySwapchainKHR(ctx_.device, swapchain_.swapchain, nullptr), VK_NULL_HANDLE);
}
void vk::engine::VulkanEngine::recreate_swapchain() {
    vkDeviceWaitIdle(ctx_.device);
    destroy_swapchain();
    destroy_renderer_targets();
    int pxw = 0, pxh = 0;
    SDL_GetWindowSizeInPixels(ctx_.window, &pxw, &pxh);
    this->state_.width  = std::max(1, pxw);
    this->state_.height = std::max(1, pxh);
    create_swapchain();
    create_renderer_targets();
    context::FrameContext frm = make_frame_context(state_.frame_number, 0u, swapchain_.swapchain_extent);
    frm.swapchain_image       = VK_NULL_HANDLE;
    frm.swapchain_image_view  = VK_NULL_HANDLE;
    state_.resize_requested   = false;
}
void vk::engine::VulkanEngine::create_renderer_targets() {
    destroy_renderer_targets();
    swapchain_.color_attachments.clear();
    swapchain_.color_attachments.reserve(renderer_caps_.color_attachments.size());
    auto create_image = [&](const context::AttachmentRequest& req, context::AttachmentResource& out) {
        VkImageCreateInfo imgci{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                     = nullptr,
            .flags                     = 0u,
            .imageType                 = VK_IMAGE_TYPE_2D,
            .format                    = req.format,
            .extent                    = {this->state_.width, this->state_.height, 1u},
            .mipLevels                 = 1u,
            .arrayLayers               = 1u,
            .samples                   = req.samples,
            .tiling                    = VK_IMAGE_TILING_OPTIMAL,
            .usage                     = req.usage,
            .sharingMode               = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount     = 0u,
            .pQueueFamilyIndices       = nullptr,
            .initialLayout             = req.initial_layout};
        constexpr VmaAllocationCreateInfo ainfo{.flags = 0u, .usage = VMA_MEMORY_USAGE_GPU_ONLY, .requiredFlags = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), .preferredFlags = 0u, .memoryTypeBits = 0u, .pool = VK_NULL_HANDLE, .pUserData = nullptr, .priority = 1.0f};
        context::vk_check(vmaCreateImage(ctx_.allocator, &imgci, &ainfo, &out.image.image, &out.image.allocation, nullptr));
        const VkImageViewCreateInfo viewci{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext                                = nullptr,
            .flags                                = 0u,
            .image                                = out.image.image,
            .viewType                             = VK_IMAGE_VIEW_TYPE_2D,
            .format                               = req.format,
            .components                           = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange                     = {req.aspect, 0u, 1u, 0u, 1u}};
        context::vk_check(vkCreateImageView(ctx_.device, &viewci, nullptr, &out.image.imageView));
        out.image.imageFormat = req.format;
        out.image.imageExtent = {this->state_.width, this->state_.height, 1u};
        out.usage             = req.usage;
        out.aspect            = req.aspect;
        out.samples           = req.samples;
        out.initial_layout    = req.initial_layout;
    };
    for (const auto& req : renderer_caps_.color_attachments) {
        context::AttachmentResource res{};
        res.name = req.name;
        create_image(req, res);
        swapchain_.color_attachments.push_back(std::move(res));
    }
    if (renderer_caps_.depth_attachment) {
        context::AttachmentResource depth{};
        depth.name = renderer_caps_.depth_attachment->name.empty() ? "depth" : renderer_caps_.depth_attachment->name;
        create_image(*renderer_caps_.depth_attachment, depth);
        swapchain_.depth_attachment = std::move(depth);
    } else {
        swapchain_.depth_attachment.reset();
    }
    presentation_attachment_index_ = -1;
    for (size_t i = 0; i < swapchain_.color_attachments.size(); ++i) {
        if (swapchain_.color_attachments[i].name == renderer_caps_.presentation_attachment) {
            presentation_attachment_index_ = static_cast<int>(i);
            break;
        }
    }
    if (presentation_attachment_index_ == -1 && !swapchain_.color_attachments.empty()) presentation_attachment_index_ = 0;
    mdq_.emplace_back([&] { destroy_renderer_targets(); });
}
void vk::engine::VulkanEngine::destroy_renderer_targets() {
    for (auto& att : swapchain_.color_attachments) {
        IF_NOT_NULL_DO_AND_SET(att.image.imageView, vkDestroyImageView(ctx_.device, att.image.imageView, nullptr), VK_NULL_HANDLE);
        IF_NOT_NULL_DO_AND_SET(att.image.image, vmaDestroyImage(ctx_.allocator, att.image.image, att.image.allocation), VK_NULL_HANDLE);
        att.image = {};
    }
    swapchain_.color_attachments.clear();
    if (swapchain_.depth_attachment) {
        IF_NOT_NULL_DO_AND_SET(swapchain_.depth_attachment->image.imageView, vkDestroyImageView(ctx_.device, swapchain_.depth_attachment->image.imageView, nullptr), VK_NULL_HANDLE);
        IF_NOT_NULL_DO_AND_SET(swapchain_.depth_attachment->image.image, vmaDestroyImage(ctx_.allocator, swapchain_.depth_attachment->image.image, swapchain_.depth_attachment->image.allocation), VK_NULL_HANDLE);
        swapchain_.depth_attachment.reset();
    }
    frame_attachment_views_.clear();
    depth_attachment_view_         = {};
    presentation_attachment_index_ = -1;
}
void vk::engine::VulkanEngine::create_command_buffers() {
    const VkCommandPoolCreateInfo pci{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .pNext = nullptr, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = ctx_.graphics_queue_family};
    for (auto& fr : frames_) {
        context::vk_check(vkCreateCommandPool(ctx_.device, &pci, nullptr, &fr.commandPool));
        VkCommandBufferAllocateInfo ai{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .pNext = nullptr, .commandPool = fr.commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1u};
        context::vk_check(vkAllocateCommandBuffers(ctx_.device, &ai, &fr.mainCommandBuffer));
        VkSemaphoreCreateInfo sci{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0u};
        context::vk_check(vkCreateSemaphore(ctx_.device, &sci, nullptr, &fr.imageAcquired));
        context::vk_check(vkCreateSemaphore(ctx_.device, &sci, nullptr, &fr.renderComplete));
        if (renderer_caps_.allow_async_compute && ctx_.compute_queue && ctx_.compute_queue != ctx_.graphics_queue) {
            VkCommandPoolCreateInfo cpool{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .pNext = nullptr, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = ctx_.compute_queue_family};
            context::vk_check(vkCreateCommandPool(ctx_.device, &cpool, nullptr, &fr.computeCommandPool));
            VkCommandBufferAllocateInfo cai{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .pNext = nullptr, .commandPool = fr.computeCommandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1u};
            context::vk_check(vkAllocateCommandBuffers(ctx_.device, &cai, &fr.asyncComputeCommandBuffer));
            VkSemaphoreCreateInfo sci2{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0u};
            context::vk_check(vkCreateSemaphore(ctx_.device, &sci2, nullptr, &fr.asyncComputeFinished));
        }
    }
    mdq_.emplace_back([&] { destroy_command_buffers(); });
}
void vk::engine::VulkanEngine::destroy_command_buffers() {
    for (auto& fr : frames_) {
        for (auto& f : std::ranges::reverse_view(fr.dq)) f();
        fr.dq.clear();
        IF_NOT_NULL_DO_AND_SET(fr.imageAcquired, vkDestroySemaphore(ctx_.device, fr.imageAcquired, nullptr), VK_NULL_HANDLE);
        IF_NOT_NULL_DO_AND_SET(fr.renderComplete, vkDestroySemaphore(ctx_.device, fr.renderComplete, nullptr), VK_NULL_HANDLE);
        IF_NOT_NULL_DO_AND_SET(fr.commandPool, vkDestroyCommandPool(ctx_.device, fr.commandPool, nullptr), VK_NULL_HANDLE);
        IF_NOT_NULL_DO_AND_SET(fr.asyncComputeFinished, vkDestroySemaphore(ctx_.device, fr.asyncComputeFinished, nullptr), VK_NULL_HANDLE);
        IF_NOT_NULL_DO_AND_SET(fr.computeCommandPool, vkDestroyCommandPool(ctx_.device, fr.computeCommandPool, nullptr), VK_NULL_HANDLE);
        fr.mainCommandBuffer         = VK_NULL_HANDLE;
        fr.asyncComputeCommandBuffer = VK_NULL_HANDLE;
        fr.submitted_timeline_value  = 0;
    }
}
void vk::engine::VulkanEngine::begin_frame(uint32_t& image_index, VkCommandBuffer& cmd) {
    const context::FrameData& fr = frames_[state_.frame_number % context::FRAME_OVERLAP];
    if (fr.submitted_timeline_value > 0) {
        VkSemaphore sem = render_timeline_;
        uint64_t val    = fr.submitted_timeline_value;
        const VkSemaphoreWaitInfo wi{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, .pNext = nullptr, .flags = 0u, .semaphoreCount = 1u, .pSemaphores = &sem, .pValues = &val};
        context::vk_check(vkWaitSemaphores(ctx_.device, &wi, UINT64_MAX));
    }
    const VkResult acq = vkAcquireNextImageKHR(ctx_.device, swapchain_.swapchain, UINT64_MAX, fr.imageAcquired, VK_NULL_HANDLE, &image_index);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_SUBOPTIMAL_KHR) {
        state_.resize_requested = true;
        cmd                     = VK_NULL_HANDLE;
        return;
    }
    context::vk_check(acq);
    context::vk_check(vkResetCommandBuffer(fr.mainCommandBuffer, 0));
    cmd = fr.mainCommandBuffer;
    constexpr VkCommandBufferBeginInfo bi{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .pNext = nullptr, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, .pInheritanceInfo = nullptr};
    context::vk_check(vkBeginCommandBuffer(cmd, &bi));
}
void vk::engine::VulkanEngine::end_frame(uint32_t image_index, VkCommandBuffer& cmd) {
    context::vk_check(vkEndCommandBuffer(cmd));
    context::FrameData& fr = frames_[state_.frame_number % context::FRAME_OVERLAP];
    VkCommandBufferSubmitInfo cbsi{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .pNext = nullptr, .commandBuffer = cmd, .deviceMask = 0u};
    VkSemaphoreSubmitInfo waitInfos[2]{};
    uint32_t waitCount   = 0;
    waitInfos[waitCount] = VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .pNext = nullptr, .semaphore = fr.imageAcquired, .value = 0u, .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .deviceIndex = 0u};
    waitCount++;
    if (fr.asyncComputeSubmitted) {
        waitInfos[waitCount] = VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .pNext = nullptr, .semaphore = fr.asyncComputeFinished, .value = 0u, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .deviceIndex = 0u};
        waitCount++;
    }
    timeline_value_++;
    const uint64_t timeline_to_signal = timeline_value_;
    VkSemaphoreSubmitInfo signalInfos[2]{VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .pNext = nullptr, .semaphore = fr.renderComplete, .value = 0u, .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, .deviceIndex = 0u},
        VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .pNext = nullptr, .semaphore = render_timeline_, .value = timeline_to_signal, .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, .deviceIndex = 0u}};
    const VkSubmitInfo2 si{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .pNext = nullptr, .waitSemaphoreInfoCount = waitCount, .pWaitSemaphoreInfos = waitInfos, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbsi, .signalSemaphoreInfoCount = 2u, .pSignalSemaphoreInfos = signalInfos};
    context::vk_check(vkQueueSubmit2(ctx_.graphics_queue, 1, &si, VK_NULL_HANDLE));
    fr.submitted_timeline_value = timeline_to_signal;
    const VkPresentInfoKHR pi{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .pNext = nullptr, .waitSemaphoreCount = 1u, .pWaitSemaphores = &fr.renderComplete, .swapchainCount = 1u, .pSwapchains = &swapchain_.swapchain, .pImageIndices = &image_index, .pResults = nullptr};
    const VkResult pres = vkQueuePresentKHR(ctx_.graphics_queue, &pi);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
        state_.resize_requested = true;
        return;
    }
    context::vk_check(pres);
}
void vk::engine::VulkanEngine::create_imgui() {
    constexpr std::array pool_sizes{VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000}, VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    const VkDescriptorPoolCreateInfo pool_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, .maxSets = 1000u * static_cast<uint32_t>(pool_sizes.size()), .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()), .pPoolSizes = pool_sizes.data()};
    context::vk_check(vkCreateDescriptorPool(ctx_.device, &pool_info, nullptr, &ctx_.descriptor_allocator.pool), "Failed to create descriptor pool");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        auto& style                       = ImGui::GetStyle();
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    if (!ImGui_ImplSDL3_InitForVulkan(ctx_.window)) {
        throw std::runtime_error("Failed to initialize ImGui SDL3 backend");
    }

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance            = ctx_.instance;
    init_info.PhysicalDevice      = ctx_.physical;
    init_info.Device              = ctx_.device;
    init_info.QueueFamily         = ctx_.graphics_queue_family;
    init_info.Queue               = ctx_.graphics_queue;
    init_info.DescriptorPool      = ctx_.descriptor_allocator.pool;
    init_info.MinImageCount       = context::FRAME_OVERLAP;
    init_info.ImageCount          = context::FRAME_OVERLAP;
    init_info.MSAASamples         = VK_SAMPLE_COUNT_1_BIT;
    init_info.UseDynamicRendering = VK_TRUE;
    init_info.CheckVkResultFn     = [](const VkResult res) { context::vk_check(res, "ImGui Vulkan operation"); };

    VkPipelineRenderingCreateInfo rendering_info{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1, .pColorAttachmentFormats = &swapchain_.swapchain_image_format};

    init_info.PipelineRenderingCreateInfo = rendering_info;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
    }

    mdq_.emplace_back([&] { destroy_imgui(); });
}
void vk::engine::VulkanEngine::begin_imgui_frame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}
void vk::engine::VulkanEngine::end_imgui_frame(VkCommandBuffer& cmd, context::FrameContext& frm) {
    ImGui::Render();

    VkImage target_image    = VK_NULL_HANDLE;
    VkImageView target_view = VK_NULL_HANDLE;

    if (frm.presentation_mode == context::PresentationMode::DirectToSwapchain) {
        target_image = frm.swapchain_image;
        target_view  = frm.swapchain_image_view;
        context::transition_to_color_attachment(cmd, target_image, VK_IMAGE_LAYOUT_UNDEFINED);
    } else {
        if (!frm.color_attachments.empty()) {
            target_image = frm.color_attachments[0].image;
            target_view  = frm.color_attachments[0].view;
            context::transition_to_color_attachment(cmd, target_image, VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    if (target_image != VK_NULL_HANDLE && target_view != VK_NULL_HANDLE) {
        const VkRenderingAttachmentInfo color_attachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = target_view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};

        const VkRenderingInfo rendering_info{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, frm.extent}, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color_attachment};

        vkCmdBeginRendering(cmd, &rendering_info);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRendering(cmd);

        if (const auto& io = ImGui::GetIO(); io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        if (frm.presentation_mode != context::PresentationMode::DirectToSwapchain) {
            const VkImageMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask                          = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask                         = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .dstStageMask                          = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask                         = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                .oldLayout                             = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout                             = VK_IMAGE_LAYOUT_GENERAL,
                .image                                 = target_image,
                .subresourceRange                      = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

            const VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};

            vkCmdPipelineBarrier2(cmd, &dep);
        }
    }
}
void vk::engine::VulkanEngine::destroy_imgui() {
    vkDeviceWaitIdle(ctx_.device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void vk::engine::VulkanEngine::blit_offscreen_to_swapchain(uint32_t image_index, VkCommandBuffer& cmd, VkExtent2D extent) {
    if (renderer_caps_.presentation_mode != context::PresentationMode::EngineBlit) return;
    if (image_index >= swapchain_.swapchain_images.size()) return;
    if (presentation_attachment_index_ < 0 || presentation_attachment_index_ >= static_cast<int>(swapchain_.color_attachments.size())) return;
    const auto& srcAtt = swapchain_.color_attachments[static_cast<size_t>(presentation_attachment_index_)];
    VkImage src        = srcAtt.image.image;
    if (src == VK_NULL_HANDLE) return;
    VkImage dst = swapchain_.swapchain_images[image_index];
    VkImageMemoryBarrier2 barriers[2]{};
    barriers[0].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[0].srcStageMask     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barriers[0].srcAccessMask    = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barriers[0].dstStageMask     = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barriers[0].dstAccessMask    = VK_ACCESS_2_TRANSFER_READ_BIT;
    barriers[0].oldLayout        = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].image            = src;
    barriers[0].subresourceRange = {srcAtt.aspect, 0u, 1u, 0u, 1u};
    barriers[1].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barriers[1].srcStageMask     = VK_PIPELINE_STAGE_2_NONE;
    barriers[1].srcAccessMask    = 0u;
    barriers[1].dstStageMask     = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barriers[1].dstAccessMask    = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barriers[1].oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].image            = dst;
    barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 2u;
    dep.pImageMemoryBarriers    = barriers;
    vkCmdPipelineBarrier2(cmd, &dep);
    VkImageBlit2 blit{};
    blit.sType          = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    blit.srcSubresource = {srcAtt.aspect, 0, 0, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0]  = {0, 0, 0};
    blit.srcOffsets[1]  = {static_cast<int32_t>(srcAtt.image.imageExtent.width), static_cast<int32_t>(srcAtt.image.imageExtent.height), 1};
    blit.dstOffsets[0]  = {0, 0, 0};
    blit.dstOffsets[1]  = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
    VkBlitImageInfo2 bi{};
    bi.sType          = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    bi.srcImage       = src;
    bi.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bi.dstImage       = dst;
    bi.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bi.regionCount    = 1u;
    bi.pRegions       = &blit;
    bi.filter         = VK_FILTER_LINEAR;
    vkCmdBlitImage2(cmd, &bi);
}
vk::context::FrameContext vk::engine::VulkanEngine::make_frame_context(const uint64_t frame_index, uint32_t image_index, VkExtent2D extent) {
    context::FrameContext frm{};
    frm.frame_index      = frame_index;
    frm.image_index      = image_index;
    frm.extent           = extent;
    frm.swapchain_format = swapchain_.swapchain_image_format;
    frm.dt_sec           = state_.dt_sec;
    frm.time_sec         = state_.time_sec;
    if (image_index < swapchain_.swapchain_images.size()) {
        frm.swapchain_image      = swapchain_.swapchain_images[image_index];
        frm.swapchain_image_view = swapchain_.swapchain_image_views[image_index];
    }
    frame_attachment_views_.clear();
    frame_attachment_views_.reserve(swapchain_.color_attachments.size());
    for (const auto& [name, usage, aspect, samples, initial_layout, image] : swapchain_.color_attachments) {
        frame_attachment_views_.push_back(context::AttachmentView{.name = name, .image = image.image, .view = image.imageView, .format = image.imageFormat, .extent = image.imageExtent, .samples = samples, .usage = usage, .aspect = aspect, .current_layout = initial_layout});
    }
    frm.color_attachments = frame_attachment_views_;
    if (!frame_attachment_views_.empty()) {
        frm.offscreen_image      = frame_attachment_views_.front().image;
        frm.offscreen_image_view = frame_attachment_views_.front().view;
    } else {
        frm.offscreen_image      = VK_NULL_HANDLE;
        frm.offscreen_image_view = VK_NULL_HANDLE;
    }
    if (swapchain_.depth_attachment) {
        depth_attachment_view_ = context::AttachmentView{.name = swapchain_.depth_attachment->name,
            .image                                             = swapchain_.depth_attachment->image.image,
            .view                                              = swapchain_.depth_attachment->image.imageView,
            .format                                            = swapchain_.depth_attachment->image.imageFormat,
            .extent                                            = swapchain_.depth_attachment->image.imageExtent,
            .samples                                           = swapchain_.depth_attachment->samples,
            .usage                                             = swapchain_.depth_attachment->usage,
            .aspect                                            = swapchain_.depth_attachment->aspect,
            .current_layout                                    = swapchain_.depth_attachment->initial_layout};
        frm.depth_attachment   = &depth_attachment_view_;
        frm.depth_image        = depth_attachment_view_.image;
        frm.depth_image_view   = depth_attachment_view_.view;
    } else {
        frm.depth_attachment = nullptr;
        frm.depth_image      = VK_NULL_HANDLE;
        frm.depth_image_view = VK_NULL_HANDLE;
    }
    frm.presentation_mode = renderer_caps_.presentation_mode;
    return frm;
}
