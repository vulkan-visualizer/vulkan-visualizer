module;
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <imgui.h>
#include <print>
#include <ranges>
#include <stb_image_write.h>
module vk.engine;
import vk.toolkit.log;

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
        if (std::ranges::find(this->renderer_caps_.extra_device_extensions, name) == this->renderer_caps_.extra_device_extensions.end()) this->renderer_caps_.extra_device_extensions.push_back(name);
    };
    if (this->renderer_caps_.need_acceleration_structure) {
        ensure_ext(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        ensure_ext(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    }
    if (this->renderer_caps_.need_ray_tracing_pipeline) {
        ensure_ext(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        ensure_ext(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        ensure_ext(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    }
    if (this->renderer_caps_.need_ray_query) {
        ensure_ext(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        ensure_ext(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    }
    if (this->renderer_caps_.need_mesh_shader) {
        ensure_ext(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    }
    if (this->renderer_caps_.buffer_device_address) {
        ensure_ext(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    }
    this->renderer_caps_.swapchain_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (this->renderer_caps_.presentation_mode != context::PresentationMode::DirectToSwapchain && this->renderer_caps_.color_attachments.empty()) this->renderer_caps_.color_attachments.push_back(context::AttachmentRequest{.name = "hdr_color"});
    if (this->renderer_caps_.presentation_attachment.empty() && !this->renderer_caps_.color_attachments.empty()) this->renderer_caps_.presentation_attachment = this->renderer_caps_.color_attachments.front().name;
    bool found = false;
    for (const auto& att : this->renderer_caps_.color_attachments) {
        if (att.name == this->renderer_caps_.presentation_attachment) {
            found = true;
            break;
        }
    }
    if (!found && !this->renderer_caps_.color_attachments.empty()) this->renderer_caps_.presentation_attachment = this->renderer_caps_.color_attachments.front().name;
    for (auto& att : this->renderer_caps_.color_attachments) {
        if (att.aspect == 0) att.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        if (att.samples == VK_SAMPLE_COUNT_1_BIT) att.samples = this->renderer_caps_.color_samples;
        if (this->renderer_caps_.presentation_mode == context::PresentationMode::EngineBlit && att.name == this->renderer_caps_.presentation_attachment) att.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (this->renderer_caps_.presentation_mode == context::PresentationMode::EngineBlit) this->renderer_caps_.swapchain_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (this->renderer_caps_.uses_depth == VK_TRUE && !this->renderer_caps_.depth_attachment.has_value()) {
        this->renderer_caps_.depth_attachment =
            context::AttachmentRequest{.name = "depth", .format = this->renderer_caps_.preferred_depth_format, .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, .samples = this->renderer_caps_.color_samples, .aspect = VK_IMAGE_ASPECT_DEPTH_BIT, .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED};
    }
    if (this->renderer_caps_.depth_attachment) {
        this->renderer_caps_.uses_depth = VK_TRUE;
        if (this->renderer_caps_.depth_attachment->aspect == 0) this->renderer_caps_.depth_attachment->aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (this->renderer_caps_.depth_attachment->samples == VK_SAMPLE_COUNT_1_BIT) this->renderer_caps_.depth_attachment->samples = this->renderer_caps_.color_samples;
    } else {
        this->renderer_caps_.uses_depth = VK_FALSE;
    }
    this->renderer_caps_.uses_offscreen = this->renderer_caps_.color_attachments.empty() ? VK_FALSE : VK_TRUE;
    if (this->renderer_caps_.presentation_mode == context::PresentationMode::DirectToSwapchain) {
        this->renderer_caps_.uses_offscreen = VK_FALSE;
        this->renderer_caps_.presentation_attachment.clear();
    }
}
void vk::engine::VulkanEngine::create_context() {
    vkb::InstanceBuilder ib;
    ib.set_app_name(this->state_.name.c_str()).request_validation_layers(false).use_default_debug_messenger().require_api_version(1, 3, 0);
    for (const char* ext : this->renderer_caps_.extra_instance_extensions) ib.enable_extension(ext);
    const vkb::Instance vkb_inst = ib.build().value();
    this->ctx_.instance                = vkb_inst.instance;
    this->ctx_.debug_messenger         = vkb_inst.debug_messenger;
    const int sdl_init_rc        = SDL_Init(SDL_INIT_VIDEO);
    REQUIRE_TRUE(sdl_init_rc, std::string("SDL_Init failed: ") + SDL_GetError());
    this->ctx_.window = SDL_CreateWindow(this->state_.name.c_str(), static_cast<int>(this->state_.width), static_cast<int>(this->state_.height), SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    REQUIRE_TRUE(this->ctx_.window != nullptr, std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    REQUIRE_TRUE(SDL_Vulkan_CreateSurface(this->ctx_.window, this->ctx_.instance, nullptr, &this->ctx_.surface), std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    VkPhysicalDeviceVulkan13Features f13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = nullptr, .synchronization2 = VK_TRUE, .dynamicRendering = VK_TRUE};
    VkPhysicalDeviceVulkan12Features f12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &f13, .descriptorIndexing = VK_TRUE, .bufferDeviceAddress = this->renderer_caps_.buffer_device_address ? VK_TRUE : VK_FALSE};
    vkb::PhysicalDeviceSelector selector(vkb_inst);
    selector.set_surface(this->ctx_.surface).set_minimum_version(1, 3).set_required_features_12(f12);
    for (const char* ext : this->renderer_caps_.extra_device_extensions) selector.add_required_extension(ext);
    vkb::PhysicalDevice phys = selector.select().value();
    this->ctx_.physical            = phys.physical_device;
    vkb::DeviceBuilder db(phys);
    vkb::Device vkbDev         = db.build().value();
    this->ctx_.device                = vkbDev.device;
    this->ctx_.graphics_queue        = vkbDev.get_queue(vkb::QueueType::graphics).value();
    this->ctx_.compute_queue         = vkbDev.get_queue(vkb::QueueType::compute).value();
    this->ctx_.transfer_queue        = vkbDev.get_queue(vkb::QueueType::transfer).value();
    this->ctx_.present_queue         = this->ctx_.graphics_queue;
    this->ctx_.graphics_queue_family = vkbDev.get_queue_index(vkb::QueueType::graphics).value();
    this->ctx_.compute_queue_family  = vkbDev.get_queue_index(vkb::QueueType::compute).value();
    this->ctx_.transfer_queue_family = vkbDev.get_queue_index(vkb::QueueType::transfer).value();
    this->ctx_.present_queue_family  = this->ctx_.graphics_queue_family;
    VmaAllocatorCreateInfo ac{.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, .physicalDevice = this->ctx_.physical, .device = this->ctx_.device, .instance = this->ctx_.instance, .vulkanApiVersion = VK_API_VERSION_1_3};
    toolkit::log::vk_check(vmaCreateAllocator(&ac, &this->ctx_.allocator));
    this->mdq_.emplace_back([&] { vmaDestroyAllocator(this->ctx_.allocator); });

    std::vector<context::DescriptorAllocator::PoolSizeRatio> sizes = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2.0f}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.0f}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4.0f}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4.0f}};
    this->ctx_.descriptor_allocator.init_pool(this->ctx_.device, 128, sizes);
    this->mdq_.emplace_back([&] { this->ctx_.descriptor_allocator.destroy_pool(this->ctx_.device); });

    VkSemaphoreTypeCreateInfo type_ci{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, .pNext = nullptr, .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = 0};
    VkSemaphoreCreateInfo sem_ci{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &type_ci, .flags = 0u};
    toolkit::log::vk_check(vkCreateSemaphore(this->ctx_.device, &sem_ci, nullptr, &this->render_timeline_));
    this->mdq_.emplace_back([&] { vkDestroySemaphore(this->ctx_.device, this->render_timeline_, nullptr); });
    this->timeline_value_ = 0;
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
    vkDeviceWaitIdle(this->ctx_.device);
    destroy_swapchain();
    destroy_renderer_targets();
    int pxw = 0, pxh = 0;
    SDL_GetWindowSizeInPixels(this->ctx_.window, &pxw, &pxh);
    this->state_.width  = std::max(1, pxw);
    this->state_.height = std::max(1, pxh);
    create_swapchain();
    create_renderer_targets();
    context::FrameContext frm = make_frame_context(this->state_.frame_number, 0u, this->swapchain_.swapchain_extent);
    frm.swapchain_image       = VK_NULL_HANDLE;
    frm.swapchain_image_view  = VK_NULL_HANDLE;
    this->state_.resize_requested   = false;
}
void vk::engine::VulkanEngine::create_renderer_targets() {
    this->destroy_renderer_targets();
    this->swapchain_.color_attachments.clear();
    this->swapchain_.color_attachments.reserve(this->renderer_caps_.color_attachments.size());
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
        toolkit::log::vk_check(vmaCreateImage(this->ctx_.allocator, &imgci, &ainfo, &out.image.image, &out.image.allocation, nullptr));
        const VkImageViewCreateInfo viewci{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext                                = nullptr,
            .flags                                = 0u,
            .image                                = out.image.image,
            .viewType                             = VK_IMAGE_VIEW_TYPE_2D,
            .format                               = req.format,
            .components                           = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange                     = {req.aspect, 0u, 1u, 0u, 1u}};
        toolkit::log::vk_check(vkCreateImageView(this->ctx_.device, &viewci, nullptr, &out.image.imageView));
        out.image.imageFormat = req.format;
        out.image.imageExtent = {this->state_.width, this->state_.height, 1u};
        out.usage             = req.usage;
        out.aspect            = req.aspect;
        out.samples           = req.samples;
        out.initial_layout    = req.initial_layout;
    };
    for (const auto& req : this->renderer_caps_.color_attachments) {
        context::AttachmentResource res{};
        res.name = req.name;
        create_image(req, res);
        this->swapchain_.color_attachments.push_back(std::move(res));
    }
    if (this->renderer_caps_.depth_attachment) {
        context::AttachmentResource depth{};
        depth.name = this->renderer_caps_.depth_attachment->name.empty() ? "depth" : this->renderer_caps_.depth_attachment->name;
        create_image(*this->renderer_caps_.depth_attachment, depth);
        this->swapchain_.depth_attachment = std::move(depth);
    } else {
        this->swapchain_.depth_attachment.reset();
    }
    this->presentation_attachment_index_ = -1;
    for (size_t i = 0; i < this->swapchain_.color_attachments.size(); ++i) {
        if (this->swapchain_.color_attachments[i].name == this->renderer_caps_.presentation_attachment) {
            this->presentation_attachment_index_ = static_cast<int>(i);
            break;
        }
    }
    if (this->presentation_attachment_index_ == -1 && !this->swapchain_.color_attachments.empty()) this->presentation_attachment_index_ = 0;
    this->mdq_.emplace_back([&] { destroy_renderer_targets(); });
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
    const VkCommandPoolCreateInfo pci{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .pNext = nullptr, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = this->ctx_.graphics_queue_family};
    for (auto& fr : this->frames_) {
        toolkit::log::vk_check(vkCreateCommandPool(this->ctx_.device, &pci, nullptr, &fr.commandPool));
        VkCommandBufferAllocateInfo ai{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .pNext = nullptr, .commandPool = fr.commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1u};
        toolkit::log::vk_check(vkAllocateCommandBuffers(this->ctx_.device, &ai, &fr.mainCommandBuffer));
        VkSemaphoreCreateInfo sci{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0u};
        toolkit::log::vk_check(vkCreateSemaphore(this->ctx_.device, &sci, nullptr, &fr.imageAcquired));
        toolkit::log::vk_check(vkCreateSemaphore(this->ctx_.device, &sci, nullptr, &fr.renderComplete));
        if (this->renderer_caps_.allow_async_compute && this->ctx_.compute_queue && this->ctx_.compute_queue != this->ctx_.graphics_queue) {
            VkCommandPoolCreateInfo cpool{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .pNext = nullptr, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = this->ctx_.compute_queue_family};
            toolkit::log::vk_check(vkCreateCommandPool(this->ctx_.device, &cpool, nullptr, &fr.computeCommandPool));
            VkCommandBufferAllocateInfo cai{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .pNext = nullptr, .commandPool = fr.computeCommandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1u};
            toolkit::log::vk_check(vkAllocateCommandBuffers(this->ctx_.device, &cai, &fr.asyncComputeCommandBuffer));
            VkSemaphoreCreateInfo sci2{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0u};
            toolkit::log::vk_check(vkCreateSemaphore(this->ctx_.device, &sci2, nullptr, &fr.asyncComputeFinished));
        }
    }
    this->mdq_.emplace_back([&] { this->destroy_command_buffers(); });
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
        toolkit::log::vk_check(vkWaitSemaphores(ctx_.device, &wi, UINT64_MAX));
    }
    const VkResult acq = vkAcquireNextImageKHR(ctx_.device, swapchain_.swapchain, UINT64_MAX, fr.imageAcquired, VK_NULL_HANDLE, &image_index);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_SUBOPTIMAL_KHR) {
        state_.resize_requested = true;
        cmd                     = VK_NULL_HANDLE;
        return;
    }
    toolkit::log::vk_check(acq);
    toolkit::log::vk_check(vkResetCommandBuffer(fr.mainCommandBuffer, 0));
    cmd = fr.mainCommandBuffer;
    constexpr VkCommandBufferBeginInfo bi{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .pNext = nullptr, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, .pInheritanceInfo = nullptr};
    toolkit::log::vk_check(vkBeginCommandBuffer(cmd, &bi));
}
void vk::engine::VulkanEngine::end_frame(uint32_t image_index, const VkCommandBuffer& cmd) {
    toolkit::log::vk_check(vkEndCommandBuffer(cmd));
    context::FrameData& fr = this->frames_[this->state_.frame_number % context::FRAME_OVERLAP];
    VkCommandBufferSubmitInfo cbsi{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .pNext = nullptr, .commandBuffer = cmd, .deviceMask = 0u};
    VkSemaphoreSubmitInfo waitInfos[2]{};
    uint32_t waitCount   = 0;
    waitInfos[waitCount] = VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .pNext = nullptr, .semaphore = fr.imageAcquired, .value = 0u, .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .deviceIndex = 0u};
    waitCount++;
    if (fr.asyncComputeSubmitted) {
        waitInfos[waitCount] = VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .pNext = nullptr, .semaphore = fr.asyncComputeFinished, .value = 0u, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .deviceIndex = 0u};
        waitCount++;
    }
    this->timeline_value_++;
    const uint64_t timeline_to_signal = this->timeline_value_;
    VkSemaphoreSubmitInfo signalInfos[2]{VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .pNext = nullptr, .semaphore = fr.renderComplete, .value = 0u, .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, .deviceIndex = 0u},
        VkSemaphoreSubmitInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .pNext = nullptr, .semaphore = this->render_timeline_, .value = timeline_to_signal, .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, .deviceIndex = 0u}};
    const VkSubmitInfo2 si{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .pNext = nullptr, .waitSemaphoreInfoCount = waitCount, .pWaitSemaphoreInfos = waitInfos, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbsi, .signalSemaphoreInfoCount = 2u, .pSignalSemaphoreInfos = signalInfos};
    toolkit::log::vk_check(vkQueueSubmit2(this->ctx_.graphics_queue, 1, &si, VK_NULL_HANDLE));
    fr.submitted_timeline_value = timeline_to_signal;
    const VkPresentInfoKHR pi{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .pNext = nullptr, .waitSemaphoreCount = 1u, .pWaitSemaphores = &fr.renderComplete, .swapchainCount = 1u, .pSwapchains = &this->swapchain_.swapchain, .pImageIndices = &image_index, .pResults = nullptr};
    const VkResult pres = vkQueuePresentKHR(this->ctx_.graphics_queue, &pi);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
        this->state_.resize_requested = true;
        return;
    }
    toolkit::log::vk_check(pres);
}

void vk::engine::VulkanEngine::blit_offscreen_to_swapchain(const uint32_t image_index, const VkCommandBuffer& cmd, const VkExtent2D extent) const {
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
    frm.swapchain_format = this->swapchain_.swapchain_image_format;
    frm.dt_sec           = this->state_.dt_sec;
    frm.time_sec         = this->state_.time_sec;
    if (image_index < this->swapchain_.swapchain_images.size()) {
        frm.swapchain_image      = this->swapchain_.swapchain_images[image_index];
        frm.swapchain_image_view = this->swapchain_.swapchain_image_views[image_index];
    }
    this->frame_attachment_views_.clear();
    this->frame_attachment_views_.reserve(this->swapchain_.color_attachments.size());
    for (const auto& [name, usage, aspect, samples, initial_layout, image] : this->swapchain_.color_attachments) {
        this->frame_attachment_views_.push_back(context::AttachmentView{.name = name, .image = image.image, .view = image.imageView, .format = image.imageFormat, .extent = image.imageExtent, .samples = samples, .usage = usage, .aspect = aspect, .current_layout = initial_layout});
    }
    frm.color_attachments = this->frame_attachment_views_;
    if (!this->frame_attachment_views_.empty()) {
        frm.offscreen_image      = this->frame_attachment_views_.front().image;
        frm.offscreen_image_view = this->frame_attachment_views_.front().view;
    } else {
        frm.offscreen_image      = VK_NULL_HANDLE;
        frm.offscreen_image_view = VK_NULL_HANDLE;
    }
    if (this->swapchain_.depth_attachment) {
        this->depth_attachment_view_ = context::AttachmentView{.name = this->swapchain_.depth_attachment->name,
            .image                                             = this->swapchain_.depth_attachment->image.image,
            .view                                              = this->swapchain_.depth_attachment->image.imageView,
            .format                                            = this->swapchain_.depth_attachment->image.imageFormat,
            .extent                                            = this->swapchain_.depth_attachment->image.imageExtent,
            .samples                                           = this->swapchain_.depth_attachment->samples,
            .usage                                             = this->swapchain_.depth_attachment->usage,
            .aspect                                            = this->swapchain_.depth_attachment->aspect,
            .current_layout                                    = this->swapchain_.depth_attachment->initial_layout};
        frm.depth_attachment   = &this->depth_attachment_view_;
        frm.depth_image        = this->depth_attachment_view_.image;
        frm.depth_image_view   = this->depth_attachment_view_.view;
    } else {
        frm.depth_attachment = nullptr;
        frm.depth_image      = VK_NULL_HANDLE;
        frm.depth_image_view = VK_NULL_HANDLE;
    }
    frm.presentation_mode = this->renderer_caps_.presentation_mode;
    return frm;
}
