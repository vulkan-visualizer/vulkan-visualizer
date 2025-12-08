module;
#include "vk_mem_alloc.h"
#include <SDL3/SDL.h>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>
export module vk.context;
import vk.toolkit.math;
import vk.toolkit.log;

namespace vk::context {
    export struct DescriptorAllocator {
        struct PoolSizeRatio {
            VkDescriptorType type;
            float ratio;
        };
        VkDescriptorPool pool{};
        void init_pool(VkDevice device, uint32_t maxSets, std::span<const PoolSizeRatio> ratios);
        void clear_descriptors(VkDevice device) const;
        void destroy_pool(VkDevice device) const;
        VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout) const;
    };

    export struct EngineContext {
        SDL_Window* window{};
        VkInstance instance{};
        VkDebugUtilsMessengerEXT debug_messenger{};
        VkSurfaceKHR surface{};
        VkPhysicalDevice physical{};
        VkDevice device{};
        VkQueue graphics_queue{};
        VkQueue compute_queue{};
        VkQueue transfer_queue{};
        VkQueue present_queue{};
        uint32_t graphics_queue_family{};
        uint32_t compute_queue_family{};
        uint32_t transfer_queue_family{};
        uint32_t present_queue_family{};
        VmaAllocator allocator{};
        DescriptorAllocator descriptor_allocator{};
        // void* services{};
    };

    export inline constexpr unsigned int FRAME_OVERLAP = 2;
    export enum class PresentationMode : uint8_t { EngineBlit, RendererComposite, DirectToSwapchain };


    export struct AttachmentRequest {
        std::string name;
        VkFormat format{VK_FORMAT_R16G16B16A16_SFLOAT};
        VkImageUsageFlags usage{VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
        VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
        VkImageLayout initial_layout{VK_IMAGE_LAYOUT_GENERAL};
    };

    export struct AttachmentResource {
        struct AllocatedImage {
            VkImage image{};
            VkImageView imageView{};
            VmaAllocation allocation{};
            VkExtent3D imageExtent{};
            VkFormat imageFormat{};
        };
        std::string name;
        VkImageUsageFlags usage{};
        VkImageAspectFlags aspect{};
        VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
        VkImageLayout initial_layout{VK_IMAGE_LAYOUT_GENERAL};
        AllocatedImage image;
    };

    export struct AttachmentView {
        std::string_view name{};
        VkImage image{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent3D extent{};
        VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
        VkImageUsageFlags usage{0};
        VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
        VkImageLayout current_layout{VK_IMAGE_LAYOUT_UNDEFINED};
    };

    export struct SwapchainSystem {
        VkSwapchainKHR swapchain{};
        VkFormat swapchain_image_format{};
        VkExtent2D swapchain_extent{};
        std::vector<VkImage> swapchain_images{};
        std::vector<VkImageView> swapchain_image_views{};
        std::vector<AttachmentResource> color_attachments{};
        std::optional<AttachmentResource> depth_attachment;
    };

    export struct RendererCaps {
        uint32_t api_version{};
        uint32_t frames_in_flight{FRAME_OVERLAP};
        VkBool32 dynamic_rendering{VK_TRUE};
        VkBool32 timeline_semaphore{VK_TRUE};
        VkBool32 descriptor_indexing{VK_TRUE};
        VkBool32 buffer_device_address{VK_TRUE};
        VkBool32 uses_depth{VK_FALSE};
        VkBool32 uses_offscreen{VK_TRUE};
        VkSampleCountFlagBits color_samples{VK_SAMPLE_COUNT_1_BIT};
        PresentationMode presentation_mode{PresentationMode::EngineBlit};
        std::string presentation_attachment{"hdr_color"};
        std::vector<AttachmentRequest> color_attachments{AttachmentRequest{.name = "hdr_color"}};
        std::optional<AttachmentRequest> depth_attachment{};
        VkFormat preferred_swapchain_format{VK_FORMAT_B8G8R8A8_UNORM};
        VkFormat preferred_depth_format{VK_FORMAT_D32_SFLOAT};
        VkImageUsageFlags swapchain_usage{VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        VkPresentModeKHR present_mode{VK_PRESENT_MODE_FIFO_KHR};
        bool enable_imgui{true};
        bool allow_async_compute{false};
        bool allow_async_transfer{false};
        bool need_ray_tracing_pipeline{false};
        bool need_acceleration_structure{false};
        bool need_ray_query{false};
        bool need_mesh_shader{false};
        bool need_shader_int64{false};
        bool need_shader_float16{false};
        std::vector<const char*> extra_instance_extensions{};
        std::vector<const char*> extra_device_extensions{};
    };

    export struct FrameContext {
        uint64_t frame_index{};
        uint32_t image_index{};
        VkExtent2D extent{};
        VkFormat swapchain_format{};
        double dt_sec{};
        double time_sec{};
        VkImage swapchain_image{};
        VkImageView swapchain_image_view{};
        VkImage offscreen_image{};
        VkImageView offscreen_image_view{};
        VkImage depth_image{VK_NULL_HANDLE};
        VkImageView depth_image_view{VK_NULL_HANDLE};
        std::span<const AttachmentView> color_attachments{};
        const AttachmentView* depth_attachment{nullptr};
        PresentationMode presentation_mode{PresentationMode::EngineBlit};
    };

    export struct FrameData {
        VkCommandPool commandPool{};
        VkCommandBuffer mainCommandBuffer{};
        VkSemaphore imageAcquired{};
        VkSemaphore renderComplete{};
        uint64_t submitted_timeline_value{0};
        std::vector<std::function<void()>> dq{};
        VkCommandBuffer asyncComputeCommandBuffer{};
        VkSemaphore asyncComputeFinished{};
        bool asyncComputeSubmitted{false};
        VkCommandPool computeCommandPool{};
    };

    export enum class PluginPhase : uint32_t {
        None       = 0,
        Setup      = 1 << 0,
        Initialize = 1 << 1,
        PreRender  = 1 << 2,
        Render     = 1 << 3,
        PostRender = 1 << 4,
        ImGUI      = 1 << 5,
        Present    = 1 << 6,
        Cleanup    = 1 << 7,
        All        = 0xFFFFFFFF,
    };

    export constexpr PluginPhase operator|(PluginPhase a, PluginPhase b) {
        return static_cast<PluginPhase>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    export constexpr PluginPhase& operator|=(PluginPhase& a, PluginPhase b) {
        a = static_cast<PluginPhase>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
        return a;
    }

    export constexpr bool operator&(PluginPhase a, PluginPhase b) {
        return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
    }

    export constexpr PluginPhase& operator&=(PluginPhase& a, PluginPhase b) {
        a = static_cast<PluginPhase>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
        return a;
    }

    export struct PluginContext {
        EngineContext* engine{nullptr};
        FrameContext* frame{nullptr};
        RendererCaps* caps{nullptr};
        VkCommandBuffer* cmd{nullptr};
        const SDL_Event* event{nullptr};
        uint64_t frame_number{0};
        float delta_time{0.0f};
    };

    export void transition_image_layout(const VkCommandBuffer& cmd, const AttachmentView& target, VkImageLayout old_layout, VkImageLayout new_layout);
    export void transition_to_color_attachment(const VkCommandBuffer& cmd, VkImage image, VkImageLayout old_layout);
} // namespace vk::context
