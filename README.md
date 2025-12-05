# Vulkan Visualizer 1.1.0

C++23, Vulkan 1.3 micro-framework built around C++ modules (`vk.engine`, `vk.context`). It sets up a swapchain, memory allocator, descriptor pools, and a minimal run loop so you can focus on recording commands and composing UI.

## Highlights
- C++ module-first API (no headers) with two concepts: `CRenderer` for your rendering code and `CUiSystem` for your UI/event layer.
- Vulkan 1.3 defaults: dynamic rendering-ready context, timeline semaphores, descriptor indexing, buffer device address.
- Presentation choices: offscreen HDR color targets with engine blit, renderer-driven compositing, or direct-to-swapchain rendering.
- Memory and descriptors: VMA-backed images for attachments and a lightweight descriptor allocator with ratio-based pool sizing.
- Platform layer: SDL3 for window creation/events and vk-bootstrap for instance/device selection; no platform-specific code required.
- Build target: static library `vulkan_visualizer::vk_vis` with dependencies fetched automatically through CMake.

## What changed in 1.1.0
- Rewritten around standard C++23 modules and concepts instead of header-based classes.
- Simplified build: one static library target, dependencies pulled via `FetchContent` (SDL3, vk-bootstrap, VMA).
- Defaults aligned to Vulkan 1.3 dynamic rendering and timeline synchronization.
- UI is now user-provided through the `CUiSystem` concept (ship your own ImGui glue or a no-op stub).

## Repository layout
- `module/vk.engine.ixx`: public `VulkanEngine`, renderer/UI concepts, and frame loop.
- `module/vk.context.ixx`: shared types (caps, frame context, attachments, descriptor allocator).
- `src/vk.engine.cpp`: engine implementation (swapchain, attachments, command buffers, submission).
- `src/vk.context.cpp`: descriptor allocator implementation.
- `CMakeLists.txt`: single-target build with module support enabled.

## Build requirements
- CMake 4.0+ (uses the experimental C++ module API flags).
- A C++23 compiler with module support (VS 2022 17.10+/MSVC, Clang 17+, recent GCC with module TS support).
- Vulkan SDK 1.3+ installed and discoverable (`VULKAN_SDK` or system package).
- Git/network access so CMake can fetch SDL3, vk-bootstrap (matching your SDK version), and VMA.

## Building the library
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```
The build produces a static library `vk_vis` aliased as `vulkan_visualizer::vk_vis`. All third-party sources are downloaded at configure time; no separate install step is required.

### Using it from another project
Add this repository as a subdirectory and link against the exported target:
```cmake
add_subdirectory(vulkan-visualizer)
add_executable(demo main.cpp)
target_link_libraries(demo PRIVATE vulkan_visualizer::vk_vis)
```

## Quick start (clear the swapchain)
This minimal example renders a solid color directly to the swapchain. It uses a no-op UI system and a renderer that only issues a clear.

```cpp
import vk.engine;
import vk.context;

struct NullUi {
    void set_main_window_title(const char*) {}
    void create_imgui(vk::context::EngineContext&, VkFormat, uint32_t) {}
    void destroy_imgui(vk::context::EngineContext&) {}
    void process_event(const SDL_Event&) {}
};

struct ClearRenderer {
    void query_required_device_caps(vk::context::RendererCaps& caps) {
        caps.presentation_mode = vk::context::PresentationMode::DirectToSwapchain;
        caps.color_attachments.clear();
        caps.swapchain_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        caps.enable_imgui = false;
    }
    void get_capabilities(vk::context::RendererCaps&) {}
    void initialize(vk::context::EngineContext&, const vk::context::RendererCaps&, const vk::context::FrameContext&) {}
    void destroy(vk::context::EngineContext&, const vk::context::RendererCaps&) {}

    void record_graphics(VkCommandBuffer cmd, vk::context::EngineContext&, vk::context::FrameContext& frm) {
        VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = frm.swapchain_image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
        vkCmdPipelineBarrier2(cmd, &dep);

        VkClearColorValue clear{{0.05f, 0.07f, 0.12f, 1.0f}};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, frm.swapchain_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    }
};

int main() {
    vk::engine::VulkanEngine engine;
    ClearRenderer renderer;
    NullUi ui;
    engine.init(renderer, ui);
    engine.run(renderer, ui);
    engine.cleanup();
    return 0;
}
```
