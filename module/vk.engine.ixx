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
    // ============================================================================
    // Plugin System Architecture (Concept-Based)
    // ============================================================================

    export enum class PluginPhase : uint32_t {
        None            = 0,
        Setup           = 1 << 0,  // Query capabilities, configure renderer
        Initialize      = 1 << 1,  // Create resources
        PreRender       = 1 << 2,  // Before rendering (update, logic)
        Render          = 1 << 3,  // Graphics/compute recording
        PostRender      = 1 << 4,  // After rendering (UI, overlays)
        Present         = 1 << 5,  // Before present (post-processing)
        Cleanup         = 1 << 6,  // Destroy resources
        All             = 0xFFFFFFFF
    };

    export constexpr PluginPhase operator|(PluginPhase a, PluginPhase b) {
        return static_cast<PluginPhase>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    export constexpr bool operator&(PluginPhase a, PluginPhase b) {
        return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
    }

    export struct PluginContext {
        context::EngineContext* engine{nullptr};
        context::FrameContext* frame{nullptr};
        context::RendererCaps* caps{nullptr};
        VkCommandBuffer* cmd{nullptr};
        const SDL_Event* event{nullptr};
        uint64_t frame_number{0};
        float delta_time{0.0f};
    };

    // Plugin concept - no interfaces, pure concept-based design
    export template <typename T>
    concept CPlugin = requires(T plugin, PluginContext& ctx, const SDL_Event& event, uint32_t w, uint32_t h) {
        { plugin.name() } -> std::convertible_to<const char*>;
        { plugin.phases() } -> std::convertible_to<PluginPhase>;
        { plugin.is_enabled() } -> std::convertible_to<bool>;
        { plugin.set_enabled(true) };
        { plugin.on_setup(ctx) };
        { plugin.on_initialize(ctx) };
        { plugin.on_pre_render(ctx) };
        { plugin.on_render(ctx) };
        { plugin.on_post_render(ctx) };
        { plugin.on_present(ctx) };
        { plugin.on_cleanup(ctx) };
        { plugin.on_event(event) };
        { plugin.on_resize(w, h) };
    };

    export class VulkanEngine {
    public:
        void init(CPlugin auto&... plugins);
        void run(CPlugin auto&... plugins);
        void cleanup();

        // Plugin management
        template<CPlugin T>
        void enable_plugin(T& plugin) { plugin.set_enabled(true); }

        template<CPlugin T>
        void disable_plugin(T& plugin) { plugin.set_enabled(false); }

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

    void VulkanEngine::init(CPlugin auto&... plugins) {
        // Phase 1: Setup - Collect capabilities from all plugins
        PluginContext ctx{&ctx_, nullptr, &renderer_caps_, nullptr, nullptr, 0, 0.0f};
        ([&] {
            if (plugins.is_enabled() && (plugins.phases() & PluginPhase::Setup)) {
                plugins.on_setup(ctx);
            }
        }(), ...);

        // Create Vulkan context
        this->process_capacity();
        this->create_context();
        this->create_swapchain();
        this->create_renderer_targets();
        this->create_command_buffers();

        // Phase 2: Initialize - Create plugin resources
        ctx.frame = new context::FrameContext(make_frame_context(state_.frame_number, 0u, swapchain_.swapchain_extent));
        ([&] {
            if (plugins.is_enabled() && (plugins.phases() & PluginPhase::Initialize)) {
                plugins.on_initialize(ctx);
                mdq_.emplace_back([&plugins] {
                    PluginContext cleanup_ctx{};
                    plugins.on_cleanup(cleanup_ctx);
                });
            }
        }(), ...);
        delete ctx.frame;

        state_.initialized      = true;
        state_.should_rendering = true;
    }
    void VulkanEngine::run(CPlugin auto&... plugins) {
        if (!state_.running) state_.running = true;
        if (!state_.should_rendering) state_.should_rendering = true;

        SDL_Event e{};
        while (state_.running) {
            // Event processing
            while (SDL_PollEvent(&e)) {
                switch (e.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    state_.running = false;
                    break;
                case SDL_EVENT_WINDOW_MINIMIZED:
                    state_.minimized        = true;
                    state_.should_rendering = false;
                    break;
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    state_.minimized        = false;
                    state_.should_rendering = true;
                    break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    state_.focused = true;
                    break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    state_.focused = false;
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    state_.resize_requested = true;
                    break;
                default: break;
                }

                // Dispatch events to plugins
                ([&] {
                    if (plugins.is_enabled()) {
                        plugins.on_event(e);
                    }
                }(), ...);
            }

            if (state_.resize_requested) {
                recreate_swapchain();
                ([&] {
                    if (plugins.is_enabled()) {
                        plugins.on_resize(swapchain_.swapchain_extent.width, swapchain_.swapchain_extent.height);
                    }
                }(), ...);
                state_.resize_requested = false;
                continue;
            }

            if (!state_.should_rendering) continue;

            // Begin frame
            uint32_t image_index = 0;
            VkCommandBuffer cmd  = VK_NULL_HANDLE;
            this->begin_frame(image_index, cmd);
            if (cmd == VK_NULL_HANDLE) continue;

            context::FrameContext frm = make_frame_context(state_.frame_number, image_index, swapchain_.swapchain_extent);
            context::FrameData& frData = frames_[state_.frame_number % context::FRAME_OVERLAP];
            frData.asyncComputeSubmitted = false;

            PluginContext plugin_ctx{
                &ctx_,
                &frm,
                &renderer_caps_,
                &cmd,
                nullptr,
                state_.frame_number,
                state_.dt_sec
            };

            // Phase: PreRender (update logic, prepare data)
            ([&] {
                if (plugins.is_enabled() && (plugins.phases() & PluginPhase::PreRender)) {
                    plugins.on_pre_render(plugin_ctx);
                }
            }(), ...);

            // Phase: Render (graphics commands)
            ([&] {
                if (plugins.is_enabled() && (plugins.phases() & PluginPhase::Render)) {
                    plugins.on_render(plugin_ctx);
                }
            }(), ...);

            // Phase: PostRender (UI, overlays)
            ([&] {
                if (plugins.is_enabled() && (plugins.phases() & PluginPhase::PostRender)) {
                    plugins.on_post_render(plugin_ctx);
                }
            }(), ...);

            // Handle presentation mode
            switch (renderer_caps_.presentation_mode) {
            case context::PresentationMode::EngineBlit:
                this->blit_offscreen_to_swapchain(image_index, cmd, frm.extent);
                break;
            case context::PresentationMode::RendererComposite:
            case context::PresentationMode::DirectToSwapchain:
            default: break;
            }

            // Phase: Present (post-processing before present)
            ([&] {
                if (plugins.is_enabled() && (plugins.phases() & PluginPhase::Present)) {
                    plugins.on_present(plugin_ctx);
                }
            }(), ...);

            end_frame(image_index, cmd);


            state_.frame_number++;
        }
    }
    void VulkanEngine::cleanup() {
        for (auto& f : std::ranges::reverse_view(mdq_)) f();
        mdq_.clear();
        destroy_context();
    }
} // namespace vk::engine
