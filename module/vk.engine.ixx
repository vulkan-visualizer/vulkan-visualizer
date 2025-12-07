module;
#include "VkBootstrap.h"
#include <SDL3/SDL.h>
#include <functional>
#include <print>
#include <ranges>
#include <string>
export module vk.engine;
import vk.context;

namespace vk::engine {
    export template <typename T>
    concept CRenderer = requires(T r, context::EngineContext& eng, context::FrameContext& frm, context::RendererCaps& caps, VkCommandBuffer& cmd) {
        { r.query_required_device_caps(caps) };
        { r.get_capabilities(caps) };
        { r.initialize(eng, caps) };
        { r.destroy(eng) };
        { r.record_graphics(cmd, eng, frm) };
    };

    export template <typename T>
    concept CUiSystem = requires(T r, context::EngineContext& eng, context::FrameContext& frm, VkCommandBuffer& cmd, const SDL_Event& event) {
        { r.create_imgui(eng, frm) };
        { r.destroy_imgui(eng) };
        { r.process_event(event) };
        { r.record_imgui(cmd, frm) };
    };

    export template <typename T>
    concept CPlugin = requires(T r) {
        { r.initialize() };
        { r.update() };
        { r.shutdown() };
    };

    export class VulkanEngine {
    public:
        void init(CRenderer auto& renderer, CUiSystem auto& ui_system, CPlugin auto&... plugins);
        void run(CRenderer auto& renderer, CUiSystem auto& ui_system, CPlugin auto&... plugins);
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

        void begin_frame(uint32_t& image_index, VkCommandBuffer& cmd);
        void end_frame(uint32_t image_index, VkCommandBuffer cmd);

        void queue_swapchain_screenshot(uint32_t image_index, VkCommandBuffer cmd);
        void blit_offscreen_to_swapchain(uint32_t image_index, VkCommandBuffer cmd, VkExtent2D extent);

    private:
        context::FrameContext make_frame_context(uint64_t frame_index, uint32_t image_index, VkExtent2D extent);
        struct EngineState {
            uint32_t width{1920};
            uint32_t height{1280};
            std::string name{"Vulkan Visualizer"};
            bool running{false};
            bool initialized{false};
            bool should_rendering{false};
            bool resize_requested{false};
            bool minimized{false};
            bool focused{true};
            bool screenshot{false};
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

    void VulkanEngine::init(CRenderer auto& renderer, CUiSystem auto& ui_system, CPlugin auto&... plugins) {
        renderer.query_required_device_caps(renderer_caps_);
        renderer.get_capabilities(renderer_caps_);

        this->process_capacity();
        this->create_context();
        this->create_swapchain();
        this->create_renderer_targets();
        this->create_command_buffers();

        renderer.initialize(this->ctx_, this->renderer_caps_);
        mdq_.emplace_back([&] { renderer.destroy(this->ctx_); });

        ui_system.create_imgui(this->ctx_, make_frame_context(state_.frame_number, 0u, swapchain_.swapchain_extent));
        mdq_.emplace_back([&] {
            vkDeviceWaitIdle(this->ctx_.device);
            ui_system.destroy_imgui(this->ctx_);
        });

        (plugins.initialize(), ...);
        mdq_.emplace_back([&] {
            vkDeviceWaitIdle(this->ctx_.device);
            (plugins.shutdown(), ...);
        });

        state_.initialized      = true;
        state_.should_rendering = true;
    }
    void VulkanEngine::run(CRenderer auto& renderer, CUiSystem auto& ui_system, CPlugin auto&... plugins) {
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
                case SDL_EVENT_KEY_DOWN:
                    {
                        if (e.key.key == SDLK_F1) {
                            this->state_.screenshot = true;
                        }
                    }
                    break;
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

            uint32_t image_index = 0;
            VkCommandBuffer cmd  = VK_NULL_HANDLE;
            this->begin_frame(image_index, cmd);
            if (cmd == VK_NULL_HANDLE) continue;
            context::FrameContext frm    = make_frame_context(state_.frame_number, image_index, swapchain_.swapchain_extent);
            context::FrameData& frData   = frames_[state_.frame_number % context::FRAME_OVERLAP];
            frData.asyncComputeSubmitted = false;
            renderer.record_graphics(cmd, this->ctx_, frm);
            ui_system.record_imgui(cmd, frm);
            (plugins.update(), ...);
            switch (renderer_caps_.presentation_mode) {
            case context::PresentationMode::EngineBlit:
                this->blit_offscreen_to_swapchain(image_index, cmd, frm.extent);
                break;
            case context::PresentationMode::RendererComposite:
            case context::PresentationMode::DirectToSwapchain:
            default: break;
            }
            if (state_.screenshot) queue_swapchain_screenshot(image_index, cmd);
            end_frame(image_index, cmd);
            if (state_.screenshot) {
                if (!frData.dq.empty()) {
                    vkQueueWaitIdle(ctx_.graphics_queue);
                    for (auto& fn : frData.dq) fn();
                    frData.dq.clear();
                    state_.screenshot = false;
                }
            }
            state_.frame_number++;
        }
    }
    void VulkanEngine::cleanup() {
        for (auto& f : std::ranges::reverse_view(mdq_)) f();
        mdq_.clear();
        destroy_context();
    }
} // namespace vk::engine
