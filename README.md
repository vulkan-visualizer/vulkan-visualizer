# Vulkan Visualizer 1.1.1

A small C++23 + Vulkan 1.3 micro-framework focused on making it easy to prototype renderers using C++ modules and a minimal engine loop.

This repository provides a compact, module-first engine (no public header surface) that sets up a Vulkan device, swapchain (or offscreen targets), a VMA-backed allocator, descriptor pools, and a tiny run loop so you can focus on recording command buffers and composing a UI layer.

## Highlights
- C++ module-first API (public modules under `module/`) exposing `VulkanEngine`, renderer and UI concepts.
- Vulkan 1.3 friendly defaults: dynamic rendering, timeline semaphores, descriptor indexing, buffer device address.
- Flexible presentation: offscreen HDR targets with engine blit, renderer-driven compositing, or direct-to-swapchain rendering.
- Lightweight descriptor allocator and VMA-backed image allocations.
- Platform integration via SDL3 for window/events and vk-bootstrap for instance/device selection.

## What's in this release (v1.1.1)
- Patch release: documentation refresh and small build compatibility fixes across common CMake/MSVC configurations.
- Clarified module usage and example in the README.
- Minor robustness fixes to build scripts and CMake configuration to reduce issues when the system has multiple Vulkan SDK versions installed.

## Repository layout (important files)
- `module/` — public C++ module interfaces (engine, context, plugins, toolkit).
- `src/` — implementation translation units for the engine and context.
- `shader/` — GLSL shader sources and compiled *.spv artifacts used by tests and examples.
- `test/` — small integration/unit tests and example applications.
- `CMakeLists.txt` — top-level build and dependency fetching (FetchContent) for SDL3, vk-bootstrap, VMA.

## Minimum requirements
- CMake 3.26+ recommended (the project uses the experimental module flags; newer CMake versions handle these better).
- A C++23-capable compiler with basic module support (MSVC in >= VS 2022 17.10+, recent Clang or GCC builds with module TS support).
- Vulkan SDK 1.3+ available and discoverable (set `VULKAN_SDK` or ensure the SDK is on your PATH).
- Git/network access for CMake to fetch the third-party sources at configure time.

## Quick build (out-of-tree, default Release)

On Windows (cmd.exe):

```cmd
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

This creates the static library `vk_vis` (aliased as `vulkan_visualizer::vk_vis`). The build downloads and builds third-party dependencies automatically.

### Using it from another project
Add this repository as a subdirectory and link against the exported target:
```cmake
add_subdirectory(vulkan-visualizer)
add_executable(demo main.cpp)
target_link_libraries(demo PRIVATE vulkan_visualizer::vk_vis)
```

## Notes and tips
- If your compiler reports module-related warnings or errors, try a newer toolchain or adjust the CMake generator (Ninja vs Visual Studio) — the project is tested primarily with a modern MSVC toolchain and Ninja.
- Precompiled SPIR-V shaders are kept in `shader/`. If you modify GLSL sources, regenerate SPV files using glslangValidator or your preferred tool.
- The CMake configuration includes small helpers for selecting the Vulkan SDK; if you have multiple SDKs installed, explicitly set `VULKAN_SDK` in your environment before running CMake.

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

## Contributing
- The project prefers small, focused PRs. If you want to add a renderer plugin or a toolkit helper, keep the module interface stable and provide accompanying tests/examples under `test/`.

## License
- This project is released under the repository's `LICENSE` file (see top-level `LICENSE`).

## Acknowledgements
- Uses SDL3 for platform windowing, vk-bootstrap for instance/device setup, and VMA for memory management. Third-party code is pulled via CMake FetchContent at configure time.

If you'd like, I can also:
- Add a short section describing how to run the small test executables in `cmake-build-debug`.
- Add a troubleshooting section for common build errors on MSVC/Clang.

(Assumptions: v1.1.1 is a patch release—no major API changes from v1.1.0; this README update focuses on documentation, build guidance, and a concise changelog.)
