# Vulkan Visualizer 1.2.0

A modular C++23 + Vulkan 1.3 framework for rapid prototyping of graphics applications and renderers using a pure C++ module interface.

This repository provides a lightweight, module-first graphics toolkit (no public header surface) built on `vulkan-hpp` RAII wrappers. It handles Vulkan context creation, swapchain management, frame synchronization, shader compilation via Slang, and ImGui integration, allowing you to focus on experimenting with rendering techniques.

## Highlights
- **Pure C++ module API** (9 modules under `modules/`) — no traditional headers, export-based interface.
- **Vulkan 1.3 core features** — dynamic rendering, pipeline barriers 2, RAII resource management via `vulkan-hpp`.
- **Slang shader compilation** — uses Slang (from Vulkan SDK) for cross-platform shader authoring and SPIR-V generation.
- **Camera system** — built-in orbit and fly camera with mouse/keyboard input handling.
- **ImGui integration** — includes docking, viewports, and a mini axis gizmo for visualizing camera orientation.
- **Geometry toolkit** — procedural mesh generation (sphere, cube) and vertex upload utilities.
- **Platform windowing** — GLFW 3.4 for window creation and input events.

## What's new in v1.2.0
- **Architectural overhaul:** Refactored from monolithic engine to modular toolkit approach with 9 independent modules.
- **Replaced SDL3 with GLFW 3.4** for simplified windowing and broader platform support.
- **Slang shader compiler integration** — CMake function `add_slang_shader_target()` automates SPIR-V compilation from `.slang` sources.
- **New camera module (`vk.camera`)** — orbit and fly modes with configurable sensitivity and projection (perspective/orthographic).
- **Enhanced geometry module (`vk.geometry`)** — typed vertex structures (`VertexP2C4`, `VertexP3C4`, `Vertex`) and procedural mesh generators.
- **ImGui module (`vk.imgui`)** — streamlined setup with docking/viewports support and mini axis gizmo rendering.
- **Frame synchronization module (`vk.frame`)** — explicit frames-in-flight management with semaphore/fence tracking.
- **Math module (`vk.math`)** — shader-compatible vector/matrix types with standard layout guarantees.
- **Removed VMA** — simplified to manual Vulkan memory allocation for educational clarity.

## Repository layout
- `modules/` — Public C++ module interfaces (9 modules):
  - `vk.camera` — Orbit/fly camera with input handling
  - `vk.context` — Vulkan instance/device/queue setup
  - `vk.frame` — Frame-in-flight synchronization system
  - `vk.geometry` — Vertex types and procedural mesh generation
  - `vk.imgui` — ImGui initialization and rendering
  - `vk.math` — Shader-compatible vec2/vec3/vec4/mat4 types
  - `vk.memory` — Buffer creation and mesh upload utilities
  - `vk.pipeline` — Graphics pipeline and shader module helpers
  - `vk.swapchain` — Swapchain creation and depth buffer management
- `src/` — Implementation translation units (`.cpp`) for each module.
- `test/` — Example applications demonstrating framework usage.
- `test/shaders/` — Slang shader sources (`.slang`) compiled to SPIR-V at build time.
- `CMakeLists.txt` — Top-level build configuration with FetchContent for GLFW and ImGui.

## Requirements
- **CMake 3.28+** — Required for C++23 module support and experimental features.
- **C++23 compiler** with module support:
  - MSVC (Visual Studio 2022 17.10+)
  - Clang 17+ with module support
  - GCC 14+ (experimental module support)
- **Vulkan SDK 1.3+** — Must include `slangc` compiler (typically in `$VULKAN_SDK/bin`).
- **Git** — For CMake FetchContent to download dependencies (GLFW, ImGui).

## Quick build

### Windows (cmd.exe)
```cmd
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVK_XAYAH_BUILD_TESTS=ON
cmake --build build --config Release --parallel
```

### Linux/macOS
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVK_XAYAH_BUILD_TESTS=ON
cmake --build build --config Release -j
```

This builds the static library `vk-core` (aliased as `vk-core::vk-core`). The build automatically downloads and compiles GLFW and ImGui via FetchContent. When `VK_XAYAH_BUILD_TESTS=ON`, it also compiles Slang shaders and builds test executables.

### Using in your project
Add this repository as a subdirectory and link against the exported target:
```cmake
add_subdirectory(vulkan-visualizer)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE vk-core::vk-core)
```

## Notes and tips
- **Module support:** If your compiler reports module-related warnings or errors, try a newer toolchain or adjust the CMake generator (Ninja vs Visual Studio). The project is tested primarily with MSVC 2022 and Ninja.
- **Slang compiler:** The build requires `slangc` from the Vulkan SDK. Ensure your `VULKAN_SDK` environment variable is set correctly or that `slangc` is in your PATH.
- **Shader compilation:** Slang shaders in `test/shaders/*.slang` are automatically compiled to SPIR-V at build time and placed in `build/shaders/`.
- **Multiple Vulkan SDKs:** If you have multiple SDKs installed, explicitly set the `VULKAN_SDK` environment variable before running CMake to select the correct version.

## Quick start example

This minimal example demonstrates how to set up a basic Vulkan application with ImGui. It creates a window, initializes Vulkan, and renders a spinning colored sphere using the framework's camera system.

```cpp
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>

import vk.context;
import vk.swapchain;
import vk.frame;
import vk.pipeline;
import vk.memory;
import vk.geometry;
import vk.imgui;
import vk.camera;
import vk.math;

using namespace vk;

int main() {
    // Setup Vulkan context and window
    auto [vkctx, surface] = context::setup_vk_context_glfw();
    
    // Create swapchain and frame synchronization
    auto sc = swapchain::setup_swapchain(vkctx, surface);
    auto frames = frame::create_frame_system(vkctx, sc, 2);
    
    // Initialize ImGui
    auto imgui_sys = imgui::create(vkctx, surface.window.get(), sc.format, 2, sc.images.size());
    
    // Create camera
    camera::Camera cam;
    
    // Main loop
    uint32_t frame_index = 0;
    while (!glfwWindowShouldClose(surface.window.get())) {
        glfwPollEvents();
        
        auto ar = frame::begin_frame(vkctx, sc, frames, frame_index);
        if (!ar.ok) continue;
        
        frame::begin_commands(frames, frame_index);
        imgui::begin_frame();
        
        // Your ImGui UI here
        ImGui::Begin("Hello");
        ImGui::Text("Vulkan Visualizer 1.2.0");
        ImGui::End();
        
        // Record rendering commands...
        auto& cmd = frame::cmd(frames, frame_index);
        // ... your drawing code here ...
        
        frame::end_frame(vkctx, sc, frames, frame_index, ar.image_index);
        frame_index = (frame_index + 1) % frames.frames_in_flight;
    }
    
    vkctx.device.waitIdle();
    imgui::shutdown(imgui_sys);
    return 0;
}
```

For a complete working example with mesh rendering, shaders, and camera controls, see `test/test_vk.cpp`.

## Contributing
- The project prefers small, focused PRs. If you want to add a renderer plugin or a toolkit helper, keep the module interface stable and provide accompanying tests/examples under `test/`.
- When contributing shader code, please use Slang format (`.slang` files) and ensure they compile with `slangc` from the Vulkan SDK.

## License
This project is released under the **Mozilla Public License Version 2.0** (see the `LICENSE` file in the repository root).

## Acknowledgements
- **Vulkan-Hpp** — Provides modern C++ RAII wrappers for Vulkan.
- **GLFW** — Cross-platform windowing and input library.
- **ImGui** — Immediate mode GUI framework with docking and viewports support.
- **Slang** — Shader compilation infrastructure bundled with the Vulkan SDK.

Dependencies are automatically downloaded and built via CMake FetchContent at configure time.
