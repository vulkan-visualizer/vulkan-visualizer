module;
#include "VkBootstrap.h"
#include <SDL3/SDL.h>
#include <functional>
#include <ranges>
#include <string>

#include <shaderc/shaderc.hpp>
export module vk.engine;
import vk.context;

namespace vk::engine {
    export template <typename T>
    concept CRenderer = requires(T r, context::EngineContext& eng, context::FrameContext& frm, context::RendererCaps& caps, VkCommandBuffer &cmd) {
        { r.query_required_device_caps(caps) };
        { r.get_capabilities(caps) };
        { r.initialize(eng, caps, frm) };
        { r.destroy(eng) };
        { r.record_graphics(cmd, eng, frm) };
    };

    export template <typename T>
    concept CUiSystem = requires(T r, const char* title, context::EngineContext& eng, context::FrameContext& frm, VkCommandBuffer &cmd, const SDL_Event& event, VkFormat format, uint32_t n_swapchain_image) {
        { r.create_imgui(eng, format, n_swapchain_image) };
        { r.destroy_imgui(eng) };
        { r.process_event(event) };
        { r.record_imgui(cmd, frm) };
    };

    export class VulkanEngine {
    public:
        void init(CRenderer auto& renderer, CUiSystem auto& ui_system);
        void run(CRenderer auto& renderer, CUiSystem auto& ui_system);
        void cleanup();

        VulkanEngine()                                   = default;
        ~VulkanEngine()                                  = default;
        VulkanEngine(const VulkanEngine&)                = delete;
        VulkanEngine& operator=(const VulkanEngine&)     = delete;
        VulkanEngine(VulkanEngine&&) noexcept            = default;
        VulkanEngine& operator=(VulkanEngine&&) noexcept = default;

    protected:
        void process_capacity();
        void create_context();
        void destroy_context();
        void create_swapchain();
        void destroy_swapchain();
        void recreate_swapchain();
        void create_renderer_targets();
        void destroy_renderer_targets();
        void create_command_buffers();
        void destroy_command_buffers();

        void begin_frame(uint32_t& imageIndex, VkCommandBuffer& cmd);
        void end_frame(uint32_t imageIndex, VkCommandBuffer cmd);

    private:
        context::FrameContext make_frame_context(uint64_t frame_index, uint32_t image_index, VkExtent2D extent);
        struct EngineState {
            uint32_t width{1280};
            uint32_t height{720};
            std::string name{"Vulkan Visualizer"};
            bool running{false};
            bool initialized{false};
            bool should_rendering{false};
            bool resize_requested{false};
            bool minimized{false};
            bool focused{true};
            uint64_t frame_number{0};
            float time_sec{0.0};
            float dt_sec{0.0};
        } state_;
        std::vector<std::function<void()>> mdq_;
        context::EngineContext ctx_{};
        context::RendererCaps renderer_caps_{};
        context::SwapchainSystem swapchain_{};
        std::vector<context::AttachmentView> frame_attachment_views_{};
        context::AttachmentView depth_attachment_view_{};
        context::FrameData frames_[context::FRAME_OVERLAP]{};

        int presentation_attachment_index_{0};
        VkSemaphore render_timeline_{};
        uint64_t timeline_value_{0};
    };

    void VulkanEngine::init(CRenderer auto& renderer, CUiSystem auto& ui_system) {
        renderer.query_required_device_caps(renderer_caps_);
        renderer.get_capabilities(renderer_caps_);

        this->process_capacity();
        this->create_context();
        this->create_swapchain();
        this->create_renderer_targets();
        this->create_command_buffers();
        renderer.initialize(this->ctx_, this->renderer_caps_, this->make_frame_context(state_.frame_number, 0u, swapchain_.swapchain_extent));
        mdq_.emplace_back([&] { renderer.destroy(this->ctx_); });
        ui_system.create_imgui(this->ctx_, swapchain_.swapchain_image_format, static_cast<uint32_t>(swapchain_.swapchain_images.size()));
        mdq_.emplace_back([&] {
            vkDeviceWaitIdle(this->ctx_.device);
            ui_system.destroy_imgui(this->ctx_);
        });

        state_.initialized      = true;
        state_.should_rendering = true;
    }
    void VulkanEngine::run(CRenderer auto& renderer, CUiSystem auto& ui_system) {
        if (!state_.running) state_.running = true;
        if (!state_.should_rendering) state_.should_rendering = true;
        SDL_Event e{};
        context::FrameContext last_frm = make_frame_context(state_.frame_number, 0u, swapchain_.swapchain_extent);
        last_frm.swapchain_image       = VK_NULL_HANDLE;
        last_frm.swapchain_image_view  = VK_NULL_HANDLE;
        while (state_.running) {
            while (SDL_PollEvent(&e)) {
                switch (e.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: state_.running = false; break;
                case SDL_EVENT_WINDOW_MINIMIZED:
                    state_.minimized        = true;
                    state_.should_rendering = false;
                    break;
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    state_.minimized        = false;
                    state_.should_rendering = true;
                    break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED: state_.focused = true; break;
                case SDL_EVENT_WINDOW_FOCUS_LOST: state_.focused = false; break;
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: state_.resize_requested = true; break;
                default: break;
                }
                ui_system.process_event(e);
            }
            if (state_.resize_requested) {
                recreate_swapchain();
                last_frm                      = make_frame_context(state_.frame_number, 0u, swapchain_.swapchain_extent);
                last_frm.swapchain_image      = VK_NULL_HANDLE;
                last_frm.swapchain_image_view = VK_NULL_HANDLE;
                continue;
            }

            uint32_t imageIndex = 0;
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            this->begin_frame(imageIndex, cmd);
            if (cmd == VK_NULL_HANDLE) continue;
            context::FrameContext frm    = make_frame_context(state_.frame_number, imageIndex, swapchain_.swapchain_extent);
            context::FrameData& frData   = frames_[state_.frame_number % context::FRAME_OVERLAP];
            frData.asyncComputeSubmitted = false;
            renderer.record_graphics(cmd, ctx_, frm);
            ui_system.record_imgui(cmd, frm);
            switch (renderer_caps_.presentation_mode) {
            case context::PresentationMode::EngineBlit:
                {
                    if (renderer_caps_.presentation_mode != context::PresentationMode::EngineBlit) return;
                    if (imageIndex >= swapchain_.swapchain_images.size()) return;
                    if (presentation_attachment_index_ < 0 || presentation_attachment_index_ >= static_cast<int>(swapchain_.color_attachments.size())) return;
                    const auto& srcAtt = swapchain_.color_attachments[static_cast<size_t>(presentation_attachment_index_)];
                    VkImage src        = srcAtt.image.image;
                    if (src == VK_NULL_HANDLE) return;
                    VkImage dst = swapchain_.swapchain_images[imageIndex];
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
                    blit.dstOffsets[1]  = {static_cast<int32_t>(frm.extent.width), static_cast<int32_t>(frm.extent.height), 1};
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
                };
                break;
            case context::PresentationMode::RendererComposite:
            case context::PresentationMode::DirectToSwapchain:
            default: break;
            }
            end_frame(imageIndex, cmd);
            state_.frame_number++;
        }
    }
    void VulkanEngine::cleanup() {
        for (auto& f : std::ranges::reverse_view(mdq_)) f();
        mdq_.clear();
        destroy_context();
    }
} // namespace vk::engine
