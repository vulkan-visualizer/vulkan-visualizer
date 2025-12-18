module;
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_raii.hpp>
module vk.swapchain;
import vk.context;
import std;


namespace {

    [[nodiscard]] vk::SurfaceFormatKHR choose_surface_format(const std::span<const vk::SurfaceFormatKHR> formats) {
        auto pick = [&](const vk::Format fmt, const vk::ColorSpaceKHR cs) -> std::optional<vk::SurfaceFormatKHR> {
            for (auto const& f : formats)
                if (f.format == fmt && f.colorSpace == cs) return f;
            return std::nullopt;
        };

        constexpr auto cs = vk::ColorSpaceKHR::eSrgbNonlinear;

        if (auto v = pick(vk::Format::eB8G8R8A8Srgb, cs)) return *v;
        if (auto v = pick(vk::Format::eR8G8B8A8Srgb, cs)) return *v;

        if (auto v = pick(vk::Format::eB8G8R8A8Unorm, cs)) return *v;
        if (auto v = pick(vk::Format::eR8G8B8A8Unorm, cs)) return *v;

        return formats.front();
    }

    [[nodiscard]] vk::PresentModeKHR choose_present_mode(const std::span<const vk::PresentModeKHR> modes) {
        for (auto const m : modes)
            if (m == vk::PresentModeKHR::eMailbox) return m;
        return vk::PresentModeKHR::eFifo;
    }

    [[nodiscard]] vk::Extent2D choose_extent(const vk::SurfaceCapabilitiesKHR& caps, const vk::Extent2D requested) {
        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) return caps.currentExtent;
        return {
            std::clamp(requested.width, caps.minImageExtent.width, caps.maxImageExtent.width),
            std::clamp(requested.height, caps.minImageExtent.height, caps.maxImageExtent.height),
        };
    }

    [[nodiscard]] uint32_t choose_image_count(const vk::SurfaceCapabilitiesKHR& caps) {
        uint32_t count = std::max(3u, caps.minImageCount);
        if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
        return count;
    }

    [[nodiscard]] uint32_t find_memory_type(const vk::raii::PhysicalDevice& pd, const uint32_t type_bits, const vk::MemoryPropertyFlags props) {
        const auto mp = pd.getMemoryProperties();
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            const bool ok_bits  = (type_bits & (1u << i)) != 0;
            const bool ok_props = (mp.memoryTypes[i].propertyFlags & props) == props;
            if (ok_bits && ok_props) return i;
        }
        throw std::runtime_error("No suitable memory type found");
    }

    [[nodiscard]] bool supports_depth_attachment(const vk::raii::PhysicalDevice& pd, const vk::Format fmt) {
        const auto p = pd.getFormatProperties(fmt);
        return (p.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) != vk::FormatFeatureFlagBits{};
    }

    [[nodiscard]] vk::Format choose_depth_format(const vk::raii::PhysicalDevice& pd) {
        const vk::Format candidates[] = {
            vk::Format::eD32Sfloat,
            vk::Format::eD32SfloatS8Uint,
            vk::Format::eD24UnormS8Uint,
        };
        for (auto f : candidates)
            if (supports_depth_attachment(pd, f)) return f;
        throw std::runtime_error("No supported depth format found");
    }

    [[nodiscard]] vk::ImageAspectFlags depth_aspect(const vk::Format fmt) {
        switch (fmt) {
        case vk::Format::eD32SfloatS8Uint:
        case vk::Format::eD24UnormS8Uint: return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        default: return vk::ImageAspectFlagBits::eDepth;
        }
    }

    struct DepthResources {
        vk::raii::Image image{nullptr};
        vk::raii::DeviceMemory memory{nullptr};
        vk::raii::ImageView view{nullptr};
    };

    [[nodiscard]] DepthResources create_depth_resources(const vk::raii::Device& device, const vk::raii::PhysicalDevice& pd, const vk::Extent2D extent, const vk::Format format, const vk::ImageAspectFlags aspect) {
        DepthResources out{};

        const vk::ImageCreateInfo image_ci{
            .imageType     = vk::ImageType::e2D,
            .format        = format,
            .extent        = vk::Extent3D{extent.width, extent.height, 1},
            .mipLevels     = 1,
            .arrayLayers   = 1,
            .samples       = vk::SampleCountFlagBits::e1,
            .tiling        = vk::ImageTiling::eOptimal,
            .usage         = vk::ImageUsageFlagBits::eDepthStencilAttachment,
            .sharingMode   = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        };

        out.image = vk::raii::Image{device, image_ci};

        const vk::ImageMemoryRequirementsInfo2 req_info{.image = *out.image};
        const vk::MemoryRequirements2 req2 = device.getImageMemoryRequirements2(req_info);

        const vk::MemoryAllocateInfo alloc_ci{
            .allocationSize  = req2.memoryRequirements.size,
            .memoryTypeIndex = find_memory_type(pd, req2.memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
        };

        out.memory = vk::raii::DeviceMemory{device, alloc_ci};
        out.image.bindMemory(out.memory, 0);

        const vk::ImageViewCreateInfo view_ci{
            .image            = *out.image,
            .viewType         = vk::ImageViewType::e2D,
            .format           = format,
            .subresourceRange = vk::ImageSubresourceRange{aspect, 0, 1, 0, 1},
        };

        out.view = vk::raii::ImageView{device, view_ci};

        return out;
    }

    [[nodiscard]] vk::ImageUsageFlags choose_swapchain_usage(const vk::SurfaceCapabilitiesKHR& caps) {
        const vk::ImageUsageFlags desired = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;

        const vk::ImageUsageFlags supported = caps.supportedUsageFlags;
        const vk::ImageUsageFlags result    = desired & supported;

        if ((result & vk::ImageUsageFlagBits::eColorAttachment) != vk::ImageUsageFlagBits::eColorAttachment) {
            throw std::runtime_error("Swapchain does not support required eColorAttachment usage");
        }
        if ((result & vk::ImageUsageFlagBits::eTransferDst) != vk::ImageUsageFlagBits{}) {
            return result;
        }
        return vk::ImageUsageFlagBits::eColorAttachment;
    }

} // namespace

vk::swapchain::Swapchain vk::swapchain::setup_swapchain(const context::VulkanContext& vkctx, const context::SurfaceContext& sctx, const Swapchain* old) {
    const auto caps  = vkctx.physical_device.getSurfaceCapabilitiesKHR(*sctx.surface);
    const auto fmts  = vkctx.physical_device.getSurfaceFormatsKHR(*sctx.surface);
    const auto modes = vkctx.physical_device.getSurfacePresentModesKHR(*sctx.surface);

    if (fmts.empty()) throw std::runtime_error("Surface has no formats");
    if (modes.empty()) throw std::runtime_error("Surface has no present modes");

    const auto chosen_fmt   = choose_surface_format(fmts);
    const auto present_mode = choose_present_mode(modes);
    const auto extent       = choose_extent(caps, sctx.extent);

    if (extent.width == 0 || extent.height == 0) throw std::runtime_error("Cannot create swapchain with zero extent");

    const auto image_count = choose_image_count(caps);
    const auto usage       = choose_swapchain_usage(caps);

    const SwapchainCreateInfoKHR ci{
        .surface          = *sctx.surface,
        .minImageCount    = image_count,
        .imageFormat      = chosen_fmt.format,
        .imageColorSpace  = chosen_fmt.colorSpace,
        .imageExtent      = extent,
        .imageArrayLayers = 1,
        .imageUsage       = usage,
        .imageSharingMode = SharingMode::eExclusive,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode      = present_mode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = old ? *old->handle : VK_NULL_HANDLE,
    };

    Swapchain sc{};

    sc.handle      = raii::SwapchainKHR{vkctx.device, ci};
    sc.images      = sc.handle.getImages();
    sc.format      = chosen_fmt.format;
    sc.color_space = chosen_fmt.colorSpace;
    sc.extent      = extent;

    sc.image_views.clear();
    sc.image_views.reserve(sc.images.size());

    ImageViewCreateInfo ivci{
        .viewType         = ImageViewType::e2D,
        .format           = sc.format,
        .subresourceRange = ImageSubresourceRange{ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };

    for (auto img : sc.images) {
        ivci.image = img;
        sc.image_views.emplace_back(vkctx.device, ivci);
    }

    sc.depth_format = choose_depth_format(vkctx.physical_device);
    sc.depth_aspect = depth_aspect(sc.depth_format);

    {
        auto d          = create_depth_resources(vkctx.device, vkctx.physical_device, sc.extent, sc.depth_format, sc.depth_aspect);
        sc.depth_image  = std::move(d.image);
        sc.depth_memory = std::move(d.memory);
        sc.depth_view   = std::move(d.view);
        sc.depth_layout = ImageLayout::eUndefined;
    }

    return sc;
}

void vk::swapchain::recreate_swapchain(const context::VulkanContext& vkctx, context::SurfaceContext& sctx, Swapchain& sc) {
    int w = 0, h = 0;
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(sctx.window.get(), &w, &h);
        glfwWaitEvents();
    }

    vkctx.device.waitIdle();

    sctx.extent = Extent2D{static_cast<uint32_t>(w), static_cast<uint32_t>(h)};

    Swapchain old         = std::move(sc);
    sc                    = setup_swapchain(vkctx, sctx, &old);
    sctx.resize_requested = false;
}
