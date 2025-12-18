# AGENTS.md

This repository is a Vulkan-based rendering engine / visualizer written in modern C++ (C++23), using C++ modules and
Vulkan-Hpp RAII. This file tells AI agents how to work in this codebase.

## Goals

- Build a clean, modular Vulkan renderer suitable for tools/visualizers and real-time debugging.
- Prefer correctness and explicit synchronization over cleverness.
- Keep the engine easy to extend with new geometry, pipelines, textures, and debug UI.
- Use Vulkan 1.3+ style APIs where appropriate (dynamic rendering, synchronization2, etc.).
- Keep code modern, minimal, and maintainable.

Non-goals:

- Implementing every feature at once.
- Premature micro-optimizations that obscure correctness.
- Large frameworks or heavy abstractions that hide Vulkan behavior.

## Architectural overview

- vk.context:
    - Owns instance/device/queues/command pool and feature enabling.
    - Central place where device features must be enabled (e.g. samplerAnisotropy).

- vk.swapchain:
    - Owns swapchain images/views and depth image/view.
    - Tracks depth layout state (e.g. sc.depth_layout).

- vk.frame:
    - Owns per-frame command buffers, semaphores, fences, and per-swapchain-image fence tracking.
    - Exposes helpers to begin frame, begin commands, end frame, and access command buffers.
    - Tracks swapchain image layouts per image (frames.swapchain_image_layout[]).

- vk.pipeline:
    - Owns GraphicsPipeline (pipeline + layout).
    - Pipeline layout must match shader resources exactly (descriptor sets and push constants).
    - Uses dynamic rendering via VkPipelineRenderingCreateInfo (no render pass objects).

- vk.geometry:
    - Provides CPU mesh generation (e.g. sphere) and GPU upload (vertex/index buffers).
    - Vertex types define vertex input formats (e.g. VertexP3C4T2).

- vk.texture:
    - Creates Texture2D images, views, samplers, and uploads RGBA8 data with optional mip gen.
    - Provides descriptor set layout helper for textures.
    - Important: texture creation does not enable device features; vk.context must.

- vk.imgui:
    - ImGui integration and render pass for UI overlay.

- vk.camera:
    - Camera controller (fly/orbit), produces matrices and input handling.

## Vulkan design rules (must follow)

### 1) RAII ownership and lifetimes

- Use Vulkan-Hpp RAII objects (vk::raii::*) for Vulkan handles.
- Do not store raw Vk* handles as primary ownership (raw handles are ok for transient calls).
- Ensure destruction order is valid (device waits idle before destroying dependent objects when needed).

### 2) Synchronization and layout transitions

- Prefer vkCmdPipelineBarrier2 (Synchronization2) and vk::ImageMemoryBarrier2.
- Swapchain color image layout must be tracked per-image (frames.swapchain_image_layout).
- Depth image layout must be tracked (sc.depth_layout) and transitioned as needed.
- Rendering flow should be:
    - Transition swapchain image -> ColorAttachmentOptimal
    - Transition depth -> DepthStencilAttachmentOptimal
    - beginRendering / draw / endRendering
    - ImGui render into the same swapchain image
    - Transition swapchain image -> PresentSrcKHR

### 3) Descriptor sets must match shaders exactly

- If the shader declares:
    - [[vk::binding(0,0)]] Texture2D<float4> g_texture; -> VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
    - [[vk::binding(1,0)]] SamplerState g_sampler; -> VK_DESCRIPTOR_TYPE_SAMPLER
- Then the descriptor set layout must have:
    - binding 0: SAMPLED_IMAGE, stage = Fragment
    - binding 1: SAMPLER, stage = Fragment
- Do not “approximate” with COMBINED_IMAGE_SAMPLER unless the shader uses it.
- The pipeline layout must include all set layouts used by shaders, in correct set order.

### 4) Descriptor pool sizing and flags

- Descriptor pool must include all descriptor types used by the set layout.
- If RAII descriptor sets are used, create pools with FREE_DESCRIPTOR_SET_BIT:
    - VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
    - Otherwise Vulkan validation will complain when RAII frees sets.

### 5) Device features must be enabled in vk.context

- Example: if sampler anisotropy is requested (anisotropyEnable = VK_TRUE),
  vk.context must enable:
    - VkPhysicalDeviceFeatures::samplerAnisotropy = VK_TRUE
- Agents must not “fix” anisotropy validation by silently disabling anisotropy unless requested.
  The correct fix is enabling the feature in vk.context, or configuring sampler desc to 1.0.

## Coding style requirements

### Language and standard library

- Use C++23.
- Prefer modules (export module / import) over textual includes for project code.
- Use the standard library heavily: std::vector, std::array, std::span, std::optional, std::string_view, std::chrono.
- Avoid macros except for platform or Vulkan-Hpp necessities.

### Modern, elegant code style

- Prefer value types, RAII, and explicit construction.
- Prefer `auto` when it improves readability and the type is obvious.
- Prefer `constexpr` for static configuration and tables.
- Avoid deep nesting. Use early returns and small helpers.
- Keep functions cohesive; extract helpers for repeated Vulkan boilerplate (barriers, descriptor writes, etc.).

### Error handling

- Throw exceptions for unrecoverable errors in setup helpers (e.g. missing memory type).
- For runtime loop errors that can be recovered (swapchain recreate), return a structured result.

### Naming

- Use consistent naming across modules:
    - Types: PascalCase (Texture2D, GraphicsPipelineDesc)
    - Functions: snake_case (create_texture_2d_rgba8, make_texture_set_layout)
    - Members: snake_case (frames_in_flight, swapchain_image_layout)

### Comments and documentation

- Prefer self-explanatory code and small helpers over large comment blocks.
- Use short comments only when a Vulkan rule is non-obvious and cannot be made obvious by code structure.

### ASCII-only source

- Keep source files ASCII-only to avoid Windows codepage issues.
- Do not introduce non-ASCII characters in strings, comments, or identifiers.

## How to implement changes safely

When adding or changing rendering features, follow this checklist:

1) Shader resources:
    - Decide descriptor bindings and push constants.
    - Ensure pipeline layout set layouts match the shader exactly.

2) Pipeline:
    - Update GraphicsPipelineDesc (formats, depth, cull, polygon mode).
    - Create pipeline with correct set layouts and push constant ranges.

3) Descriptors:
    - Create descriptor set layout(s).
    - Create pool with the correct types and counts.
    - Allocate and update descriptor sets with the correct descriptor types.

4) Render loop:
    - Ensure layout transitions are correct.
    - Bind pipeline, descriptor sets, push constants.
    - Bind vertex/index buffers, draw.

5) Validation:
    - Never ignore validation errors.
    - Fix the root cause:
        - feature not enabled -> enable in vk.context
        - descriptor mismatch -> fix layout/pool/update types
        - layout mismatch -> fix pipeline layout or shader declarations

## Agent workflow expectations

- Do not invent APIs. If you are unsure what a module exports, ask for the module file or search within the repo.
- Prefer minimal diffs that preserve design intent.
- If you change shader bindings, you must update:
    - descriptor set layout creation
    - descriptor pool sizes
    - descriptor writes
    - pipeline layout set layouts
- Keep improvements consistent with the existing style (modules, RAII, explicit Vulkan correctness).

## “Good defaults” the agent should use

- Dynamic rendering + synchronization2 for new passes.
- DescriptorSetLayout and PipelineLayout created from exact shader needs.
- Descriptor pools created with FREE_DESCRIPTOR_SET_BIT when using RAII sets.
- Anisotropy only when feature is enabled; otherwise clamp to 1.0.

End of file.
