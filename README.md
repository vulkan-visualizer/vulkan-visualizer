# Vulkan Visualizer (v0.9.0)

[![Windows CI](https://github.com/HinaPE/vulkan-visualizer/actions/workflows/windows-build.yml/badge.svg?branch=master)](https://github.com/HinaPE/vulkan-visualizer/actions/workflows/windows-build.yml)
[![Linux CI](https://github.com/HinaPE/vulkan-visualizer/actions/workflows/linux-build.yml/badge.svg?branch=master)](https://github.com/HinaPE/vulkan-visualizer/actions/workflows/linux-build.yml)
[![macOS CI](https://github.com/HinaPE/vulkan-visualizer/actions/workflows/macos-build.yml/badge.svg?branch=master)](https://github.com/HinaPE/vulkan-visualizer/actions/workflows/macos-build.yml)

Lightweight, modular Vulkan 1.3 mini-engine with dynamic rendering, offscreen targets, and an optional ImGui HUD. Clean C++23 API with STL-style naming.

---

## Table of Contents
- Highlights
- Repository Layout
- Build
- Quick Start (C++ API)
- ImGui Tabs & Overlays
- Mini Axis Gizmo
- Frame Context & Presentation Modes
- Architecture
- Contributing
- License

---

## Highlights
- Vulkan: 1.3 via vk-bootstrap (dynamic rendering, synchronization2)
- Windowing: SDL3
- Memory: Vulkan Memory Allocator (VMA)
- Descriptors: single pooled allocator with ratio configuration
- Frames In Flight: 2
- Sync: timeline semaphore + per-frame binary semaphores
- Rendering: fully dynamic (no render pass objects)
- Offscreen: configurable color attachments (default HDR R16G16B16A16) + optional depth
- Presentation: EngineBlit / RendererComposite / DirectToSwapchain
- ImGui: docking + multi-viewport; tabs host + per-frame overlays (HUD)
- Utilities: screenshot (PNG), optional GPU timestamps, simple hot-reload hook
- C++23, STL-style API

---

## Repository Layout
```
cmake/
  setup_imgui.cmake
  setup_sdl3.cmake
  setup_vkbootstrap.cmake
  setup_vma.cmake
  setup_stb.cmake
include/
  vk_engine.h
  vv_camera.h
src/
  vk_engine.cpp
  vv_camera.cpp
CMakeLists.txt
LICENSE
README.md
```

---

## Build

Requirements
- CMake 3.26+
- Compiler: MSVC 19.36+ / Clang 16+ / GCC 12+
- Vulkan SDK 1.3+

SDL3, ImGui, vk-bootstrap, VMA, and stb are fetched automatically via the helper CMake scripts.

Configure and build
```cmd
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

---

## Quick Start (C++ API)
```cpp
#include <vk_engine.h>

class MyRenderer : public IRenderer {
public:
    void query_required_device_caps(RendererCaps& caps) override {
        caps.enable_imgui = true;
    }
    void initialize(const EngineContext& eng, const RendererCaps& caps, const FrameContext& frm) override {
        (void)eng; (void)caps; (void)frm;
    }
    void destroy(const EngineContext& eng, const RendererCaps& caps) override {
        (void)eng; (void)caps;
    }
    void record_graphics(VkCommandBuffer cmd, const EngineContext& eng, const FrameContext& frm) override {
        (void)cmd; (void)eng; (void)frm;
    }
};

int main() {
    VulkanEngine engine;
    engine.configure_window(1280, 720, "Visualizer");
    engine.set_renderer(std::make_unique<MyRenderer>());
    engine.init();
    engine.run();
    engine.cleanup();
}
```

---

## ImGui Tabs & Overlays
When `RendererCaps::enable_imgui = true`, the engine creates a dockable ImGui window and exposes a minimal tabs host through `EngineContext.services`:

```cpp
auto* tabs = static_cast<vv_ui::TabsHost*>(eng.services);
if (tabs) {
    tabs->add_tab("My Panel", [] { /* ImGui widgets */ });
    tabs->add_overlay([] { /* HUD overlay on main viewport */ });
}
```

- `add_tab(name, fn)`: per-frame ephemeral tab
- `add_overlay(fn)`: per-frame overlay; drawn before `ImGui::Render()` on the foreground draw list

---

## Mini Axis Gizmo
`vv::CameraService` can draw a small XYZ gizmo anchored to the top-right of the ImGui main viewport:

```cpp
auto* tabs = static_cast<vv_ui::TabsHost*>(eng.services);
if (tabs) tabs->add_overlay([&]{ camera.imgui_draw_mini_axis_gizmo(12, 96, 2.0f); });
```

---

## Frame Context & Presentation Modes
`FrameContext` includes swapchain extent, image/view, time/dt, color/depth attachments, and presentation mode.

Presentation modes
- EngineBlit: engine blits a selected offscreen attachment to the swapchain
- RendererComposite: renderer composes directly into the swapchain
- DirectToSwapchain: renderer records directly to the swapchain

Select resources in `RendererCaps` via `color_attachments`, `depth_attachment`, and `presentation_attachment`.

---

## Architecture
1. VulkanEngine
   - Instance/device, queues, swapchain
   - Attachment allocation via VMA
   - Descriptor pool (ratio-based)
   - Frame loop: events → update → record → present
   - Optional GPU timestamps, screenshot, hot-reload
   - ImGui lifecycle and rendering
2. IRenderer (user)
   - Capability negotiation
   - Init/destroy; record graphics/compute; optional async compute
   - UI: `on_imgui` to register tabs/overlays
3. Flow
   - Poll SDL events → acquire → record → submit → present

---

## Contributing
- Prefer C++23 and STL-style naming
- Keep public interfaces minimal
- Test on at least one Windows and one Unix toolchain when possible

---

## License
See [LICENSE](./LICENSE).
