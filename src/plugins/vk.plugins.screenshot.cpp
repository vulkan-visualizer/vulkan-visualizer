module;
#include <SDL3/SDL.h>
#include <chrono>
#include <stb_image_write.h>
#include <stdexcept>
#include <vk_mem_alloc.h>
module vk.plugins.screenshot;
import vk.toolkit.log;

void vk::plugins::Screenshot::on_pre_render(const context::PluginContext& ctx) {
    if (pending_capture_.buffer != VK_NULL_HANDLE && ctx.engine) {
        vkQueueWaitIdle(ctx.engine->graphics_queue);

        void* pixel_data = nullptr;
        vmaMapMemory(ctx.engine->allocator, pending_capture_.allocation, &pixel_data);

        if (pixel_data) {
            if (!pixel_data) return;

            const auto* bgra = static_cast<const uint8_t*>(pixel_data);
            std::vector<uint8_t> rgba(static_cast<size_t>(pending_capture_.width) * static_cast<size_t>(pending_capture_.height) * 4);

            for (size_t i = 0, n = static_cast<size_t>(pending_capture_.width) * static_cast<size_t>(pending_capture_.height); i < n; ++i) {
                rgba[i * 4 + 0] = bgra[i * 4 + 2];
                rgba[i * 4 + 1] = bgra[i * 4 + 1];
                rgba[i * 4 + 2] = bgra[i * 4 + 0];
                rgba[i * 4 + 3] = bgra[i * 4 + 3];
            }

            switch (config_.format) {
            case ScreenshotFormat::PNG: stbi_write_png(pending_capture_.output_path.c_str(), static_cast<int>(pending_capture_.width), static_cast<int>(pending_capture_.height), 4, rgba.data(), static_cast<int>(pending_capture_.width) * 4); break;
            case ScreenshotFormat::JPG: stbi_write_jpg(pending_capture_.output_path.c_str(), static_cast<int>(pending_capture_.width), static_cast<int>(pending_capture_.height), 4, rgba.data(), config_.jpeg_quality); break;
            case ScreenshotFormat::BMP: stbi_write_bmp(pending_capture_.output_path.c_str(), static_cast<int>(pending_capture_.width), static_cast<int>(pending_capture_.height), 4, rgba.data()); break;
            case ScreenshotFormat::TGA: stbi_write_tga(pending_capture_.output_path.c_str(), static_cast<int>(pending_capture_.width), static_cast<int>(pending_capture_.height), 4, rgba.data()); break;
            default: throw std::runtime_error("Unsupported screenshot format");
            }
            vmaUnmapMemory(ctx.engine->allocator, pending_capture_.allocation);
        }

        vmaDestroyBuffer(ctx.engine->allocator, pending_capture_.buffer, pending_capture_.allocation);
        pending_capture_ = {};
    }
}
void vk::plugins::Screenshot::on_present(context::PluginContext& ctx) {
    if (!screenshot_requested_) return;

    if (!ctx.engine || !ctx.cmd || !ctx.frame) throw std::runtime_error("Invalid arguments");

    const auto width  = ctx.frame->extent.width;
    const auto height = ctx.frame->extent.height;
    const auto img    = ctx.frame->swapchain_image;

    if (img == VK_NULL_HANDLE) throw std::runtime_error("Swapchain image is null");

    const auto buffer_size = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc{};
    VmaAllocationInfo alloc_info{};

    const VkBufferCreateInfo buffer_ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = buffer_size, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

    constexpr VmaAllocationCreateInfo alloc_ci{.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO, .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};

    toolkit::log::vk_check(vmaCreateBuffer(ctx.engine->allocator, &buffer_ci, &alloc_ci, &buffer, &alloc, &alloc_info), "Failed to create screenshot buffer");

    const VkImageMemoryBarrier2 to_src{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask                         = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask                        = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask                         = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask                        = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout                            = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .newLayout                            = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .image                                = img,
        .subresourceRange                     = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

    const VkDependencyInfo dep_to_src{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_src};

    vkCmdPipelineBarrier2(*ctx.cmd, &dep_to_src);

    const VkBufferImageCopy region{.bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageOffset = {0, 0, 0}, .imageExtent = {width, height, 1}};

    vkCmdCopyImageToBuffer(*ctx.cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);

    const VkImageMemoryBarrier2 back_present{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask                               = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask                              = VK_ACCESS_2_TRANSFER_READ_BIT,
        .dstStageMask                               = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        .dstAccessMask                              = 0,
        .oldLayout                                  = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout                                  = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image                                      = img,
        .subresourceRange                           = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

    const VkDependencyInfo dep_back{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &back_present};

    vkCmdPipelineBarrier2(*ctx.cmd, &dep_back);

    pending_capture_ = {buffer, alloc, width, height, generate_filename()};

    screenshot_requested_ = false;
}
void vk::plugins::Screenshot::on_cleanup(const context::PluginContext& ctx) {
    if (this->pending_capture_.buffer != VK_NULL_HANDLE && ctx.engine) {
        vmaDestroyBuffer(ctx.engine->allocator, this->pending_capture_.buffer, this->pending_capture_.allocation);
        this->pending_capture_ = {};
    }
}
void vk::plugins::Screenshot::on_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1) {
        this->screenshot_requested_ = true;
    }
}

std::string vk::plugins::Screenshot::generate_filename() const {
    if (!this->config_.auto_filename) return std::format("{}/{}", this->config_.output_directory, this->config_.filename_prefix);

    const auto now        = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    const auto ms         = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time_t_now);
#else
    localtime_r(&time_t_now, &tm);
#endif

    std::ostringstream oss;
    oss << this->config_.output_directory << "/" << this->config_.filename_prefix << "_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << std::setfill('0') << std::setw(3) << ms.count();

    switch (this->config_.format) {
    case ScreenshotFormat::PNG: oss << ".png"; break;
    case ScreenshotFormat::JPG: oss << ".jpg"; break;
    case ScreenshotFormat::BMP: oss << ".bmp"; break;
    case ScreenshotFormat::TGA: oss << ".tga"; break;
    default: throw std::runtime_error("Unsupported screenshot format");
    }

    return oss.str();
}
