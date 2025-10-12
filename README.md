# VulkanVisualizer

[![Windows Build](https://github.com/vulkan-visualizer/vulkan-visualizer/actions/workflows/windows-build.yml/badge.svg)](https://github.com/vulkan-visualizer/vulkan-visualizer/actions/workflows/windows-build.yml)
[![Linux Build](https://github.com/vulkan-visualizer/vulkan-visualizer/actions/workflows/linux-build.yml/badge.svg)](https://github.com/vulkan-visualizer/vulkan-visualizer/actions/workflows/linux-build.yml)
[![macOS Build](https://github.com/vulkan-visualizer/vulkan-visualizer/actions/workflows/macos-build.yml/badge.svg)](https://github.com/vulkan-visualizer/vulkan-visualizer/actions/workflows/macos-build.yml)

> A modern, lightweight Vulkan 1.3 rendering library with ImGui integration for rapid graphics prototyping and visualization.

**VulkanVisualizer** is a cross-platform C++23 library that provides a clean, high-level abstraction over Vulkan 1.3's dynamic rendering pipeline. Built for graphics researchers, game developers, and visualization engineers who need a production-ready foundation without the boilerplate.

## ✨ Features

### Core Rendering
- **Vulkan 1.3** with dynamic rendering (no render pass objects)
- **Modern synchronization** via timeline semaphores and synchronization2
- **Efficient memory management** using Vulkan Memory Allocator (VMA)
- **Flexible presentation modes**: engine blit, renderer composite, or direct-to-swapchain
- **HDR rendering** with configurable offscreen targets (default R16G16B16A16)
- **Multi-sample anti-aliasing** support

### Developer Experience
- **ImGui integration** with docking and multi-viewport support
- **Real-time debugging** with optional GPU timestamp queries
- **Screenshot capture** to PNG format
- **Shader hot-reload** hooks for rapid iteration
- **Clean C++23 API** with STL-style naming conventions

### Cross-Platform
- **Windows** (MSVC 19.36+)
- **Linux** (GCC 12+, Clang 16+)
- **macOS** (via MoltenVK)

### Production Ready
- **Warnings as errors** enforced in CI/CD
- **Automated testing** across all platforms
- **CMake integration** with find_package support
- **Self-contained releases** with all dependencies included

---

## 📦 Installation

### Option 1: Download Pre-built Release (Recommended)

Download the latest release for your platform from the [Releases page](https://github.com/vulkan-visualizer/vulkan-visualizer/releases):

- **Windows**: `vulkan-visualizer-windows-x64-Release.zip`
- **Linux**: `vulkan-visualizer-linux-x64-Release.tar.gz`
- **macOS**: `vulkan-visualizer-macos-universal-Release.tar.gz`

Extract and use with CMake:

```cmake
# In your CMakeLists.txt
list(APPEND CMAKE_PREFIX_PATH "/path/to/extracted/vulkan-visualizer")
find_package(VulkanVisualizer REQUIRED)
target_link_libraries(your_target PRIVATE VulkanVisualizer::vulkan_visualizer)
```

### Option 2: Build from Source

#### Prerequisites
- **CMake** 3.26 or later
- **Vulkan SDK** 1.3 or later
- **C++23 compiler**:
  - Windows: Visual Studio 2022 (MSVC 19.36+)
  - Linux: GCC 12+ or Clang 16+
  - macOS: Xcode 15+ with command line tools

#### Dependencies (Auto-fetched)
All dependencies are automatically downloaded and configured via CMake FetchContent:
- [SDL3](https://github.com/libsdl-org/SDL) (windowing and input)
- [ImGui](https://github.com/ocornut/imgui) (debug UI)
- [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap) (Vulkan initialization)
- [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) (memory allocation)
- [stb](https://github.com/nothings/stb) (image writing)

#### Build Steps

```bash
# Clone the repository
git clone https://github.com/vulkan-visualizer/vulkan-visualizer.git
cd vulkan-visualizer

# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release --parallel

# Install (optional)
cmake --install build --prefix /path/to/install
```

#### Build Options

```bash
# Configure with options
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVV_WITH_LOGGING=ON \
  -DVV_WITH_GPU_TIMESTAMPS=ON \
  -DVV_WITH_TONEMAP=ON \
  -DVV_WITH_SCREENSHOT=ON \
  -DVV_WITH_HOTRELOAD=ON \
  -DVV_WARNINGS_AS_ERRORS=ON
```

| Option | Default | Description |
|--------|---------|-------------|
| `VV_WITH_LOGGING` | ON | Enable logging system |
| `VV_WITH_GPU_TIMESTAMPS` | ON | Enable GPU timestamp queries |
| `VV_WITH_TONEMAP` | ON | Enable tonemapping pass |
| `VV_WITH_SCREENSHOT` | ON | Enable screenshot utilities |
| `VV_WITH_HOTRELOAD` | ON | Enable shader hot-reload hooks |
| `VV_WARNINGS_AS_ERRORS` | ON | Treat compiler warnings as errors |

---

## 🚀 Quick Start

### Minimal Example

```cpp
#include <vk_engine.h>
#include <memory>

class MyRenderer : public IRenderer {
public:
    // Configure required capabilities
    void query_required_device_caps(RendererCaps& caps) override {
        caps.enable_imgui = true;
        caps.color_attachments = 1;
        caps.depth_attachment = true;
    }

    // Initialize resources
    void initialize(const EngineContext& eng, 
                   const RendererCaps& caps, 
                   const FrameContext& frm) override {
        // Create pipelines, buffers, etc.
    }

    // Record rendering commands
    void record_graphics(VkCommandBuffer cmd, 
                        const EngineContext& eng, 
                        const FrameContext& frm) override {
        // Record your draw calls
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // Optional: ImGui debug UI
    void on_imgui(const EngineContext& eng, const FrameContext& frm) override {
        if (auto* tabs = static_cast<vv_ui::TabsHost*>(eng.services)) {
            tabs->add_tab("My Panel", [&] {
                ImGui::Text("Frame: %zu", frm.frame_number);
                ImGui::Text("FPS: %.1f", 1.0 / frm.dt);
            });
        }
    }

    // Cleanup
    void destroy(const EngineContext& eng, const RendererCaps& caps) override {
        // Release resources
    }
};

int main() {
    VulkanEngine engine;
    
    // Configure window
    engine.configure_window(1920, 1080, "VulkanVisualizer Demo");
    
    // Set custom renderer
    engine.set_renderer(std::make_unique<MyRenderer>());
    
    // Initialize and run
    engine.init();
    engine.run();
    engine.cleanup();
    
    return 0;
}
```

### With Camera Control

```cpp
#include <vk_engine.h>
#include <vv_camera.h>

class CameraRenderer : public IRenderer {
private:
    vv::CameraService camera;

public:
    void query_required_device_caps(RendererCaps& caps) override {
        caps.enable_imgui = true;
        caps.color_attachments = 1;
        caps.depth_attachment = true;
    }

    void initialize(const EngineContext& eng, 
                   const RendererCaps& caps, 
                   const FrameContext& frm) override {
        camera.position = {0.0f, 2.0f, 5.0f};
        camera.look_at({0.0f, 0.0f, 0.0f});
    }

    void record_graphics(VkCommandBuffer cmd, 
                        const EngineContext& eng, 
                        const FrameContext& frm) override {
        // Update camera
        camera.update(frm.dt);
        
        // Use camera matrices
        glm::mat4 view = camera.get_view_matrix();
        glm::mat4 proj = camera.get_projection_matrix(
            frm.swapchain_extent.width, 
            frm.swapchain_extent.height
        );
        
        // Push constants or uniform buffers
        // ... render with view/proj matrices
    }

    void on_imgui(const EngineContext& eng, const FrameContext& frm) override {
        if (auto* tabs = static_cast<vv_ui::TabsHost*>(eng.services)) {
            // Add camera controls panel
            tabs->add_tab("Camera", [&] {
                camera.imgui_draw_controls();
            });
            
            // Add mini axis gizmo overlay
            tabs->add_overlay([&] {
                camera.imgui_draw_mini_axis_gizmo(12, 96, 2.0f);
            });
        }
    }

    void destroy(const EngineContext& eng, const RendererCaps& caps) override {}
};
```

---

## 📚 Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      VulkanEngine                            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │   Instance   │  │   Physical   │  │    Logical   │      │
│  │   Creation   │→ │    Device    │→ │    Device    │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  Swapchain   │  │   Queues     │  │  Descriptor  │      │
│  │   Manager    │  │  (Graphics)  │  │     Pool     │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      IRenderer (Your Code)                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │   Pipeline   │  │   Buffers    │  │   Textures   │      │
│  │   Creation   │  │   Creation   │  │   Creation   │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│  ┌────────────────────────────────────────────────────┐     │
│  │           Command Recording                         │     │
│  │  record_graphics() / record_compute()              │     │
│  └────────────────────────────────────────────────────┘     │
└─────────────────────────────────────────────────────────────┘
```

### Frame Loop

```
1. Poll SDL Events ──→ 2. Acquire Swapchain Image
                              │
                              ▼
6. Present ←───── 5. Submit ←──── 4. Record Commands
                                          │
                                          ▼
                              3. Update Camera & Logic
```

### Presentation Modes

#### EngineBlit (Default)
```
Renderer → Offscreen Target → Engine Blits → Swapchain
```
The engine automatically blits your selected color attachment to the swapchain.

#### RendererComposite
```
Renderer → Offscreen Target + Swapchain Composition → Swapchain
```
You composite your offscreen render with the swapchain in your renderer.

#### DirectToSwapchain
```
Renderer → Direct Rendering → Swapchain
```
You render directly to the swapchain image (no offscreen targets).

---

## 🎨 ImGui Integration

### Tabs and Overlays

```cpp
void on_imgui(const EngineContext& eng, const FrameContext& frm) override {
    auto* tabs = static_cast<vv_ui::TabsHost*>(eng.services);
    if (!tabs) return;
    
    // Add a dockable tab
    tabs->add_tab("Statistics", [&] {
        ImGui::Text("Frame: %zu", frm.frame_number);
        ImGui::Text("Delta Time: %.3f ms", frm.dt * 1000.0);
        ImGui::Text("Resolution: %ux%u", 
            frm.swapchain_extent.width, 
            frm.swapchain_extent.height);
    });
    
    // Add an overlay (HUD)
    tabs->add_overlay([&] {
        ImGui::SetNextWindowPos({10, 10});
        ImGui::Begin("HUD", nullptr, 
            ImGuiWindowFlags_NoTitleBar | 
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", 1.0 / frm.dt);
        ImGui::End();
    });
}
```

---

## 🛠️ Advanced Features

### GPU Timestamps

```cpp
#ifdef VV_ENABLE_GPU_TIMESTAMPS
// Timestamps are automatically collected if enabled
// Access via EngineContext in your renderer
#endif
```

### Screenshot Capture

```cpp
#ifdef VV_ENABLE_SCREENSHOT
// Screenshots saved to disk when triggered
// Configure via engine settings
#endif
```

### Shader Hot-Reload

```cpp
#ifdef VV_ENABLE_HOTRELOAD
// Implement hot-reload callback in your renderer
void on_shader_reload() override {
    // Recreate pipelines
}
#endif
```

---

## 📊 Performance

### Optimizations
- **Descriptor management**: Pooled allocator with configurable ratios
- **Memory efficiency**: VMA for optimal allocation strategies
- **Pipeline cache**: Automatic caching of pipeline state objects
- **Command buffer reuse**: Per-frame command buffer recycling
- **Parallel compilation**: Multi-threaded shader compilation

### Typical Performance
- **Initialization**: ~100-200ms (with shader compilation)
- **Frame overhead**: <0.1ms (excluding user rendering)
- **Memory footprint**: ~50MB base + your resources

---

## 🧪 Examples

Coming soon! We're working on example projects to demonstrate:
- Basic triangle rendering
- Textured cube with camera controls
- PBR material system
- Compute shader particle system
- Post-processing effects

---

## 🤝 Contributing

We welcome contributions! Please follow these guidelines:

### Code Style
- Use **C++23** features where appropriate
- Follow **STL-style naming** (snake_case for functions/variables)
- Keep **public APIs minimal** and well-documented
- Write **clear commit messages**

### Testing
- Test on at least **one Windows and one Unix platform**
- Ensure all **CI checks pass**
- Add tests for new features

### Pull Request Process
1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## 📖 Documentation

- **API Reference**: See header files in `include/`
- **Build Guide**: See `.github/WORKFLOWS_UPDATE.md`
- **Architecture**: See `docs/` (coming soon)

---

## 🐛 Troubleshooting

### Common Issues

**Q: Build fails with "Vulkan SDK not found"**
```bash
# Set VULKAN_SDK environment variable
export VULKAN_SDK=/path/to/vulkan/sdk  # Linux/macOS
set VULKAN_SDK=C:\VulkanSDK\1.3.xxx    # Windows
```

**Q: Runtime error "Failed to create instance"**
- Ensure Vulkan drivers are up to date
- Check that your GPU supports Vulkan 1.3

**Q: ImGui windows not appearing**
- Verify `caps.enable_imgui = true` in `query_required_device_caps()`
- Check that you're calling `on_imgui()` override

**Q: Linker errors on Windows**
- Use the same C++ runtime library (MT vs MD)
- Ensure all dependencies are built with the same configuration

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## 🙏 Acknowledgments

Built with these excellent libraries:
- [Vulkan](https://www.vulkan.org/) - Modern graphics API
- [SDL3](https://www.libsdl.org/) - Cross-platform windowing
- [ImGui](https://github.com/ocornut/imgui) - Immediate mode GUI
- [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap) - Vulkan initialization
- [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) - Memory management
- [stb](https://github.com/nothings/stb) - Image utilities

---

## 📮 Contact

- **Issues**: [GitHub Issues](https://github.com/vulkan-visualizer/vulkan-visualizer/issues)
- **Discussions**: [GitHub Discussions](https://github.com/vulkan-visualizer/vulkan-visualizer/discussions)

---

<div align="center">

**⭐ Star this repository if you find it helpful! ⭐**

Made with ❤️ by the [Xayah Hina](https://github.com/Xayah-Hina)

</div>
