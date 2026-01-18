cmake_minimum_required(VERSION 4.1.2)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
project(vulkan_visualizer VERSION 1.4.0 LANGUAGES CXX)

# ============================================================================
# C++ Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)


# ============================================================================
# Dependencies
# ============================================================================
find_package(Vulkan REQUIRED)
set(_VULKAN_HPP_IMPL "${CMAKE_CURRENT_BINARY_DIR}/_vulkan_hpp_impl.cpp")
file(WRITE "${_VULKAN_HPP_IMPL}" [[
#include <vulkan/vulkan_raii.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
]])
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(dependencies/glfw)
set(imgui_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/imgui")
set(_IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

add_library(vk-core STATIC)
add_library(vk-core::vk-core ALIAS vk-core)
target_sources(vk-core
        PRIVATE
        src/vk.camera.cpp
        src/vk.context.cpp
        src/vk.frame.cpp
        src/vk.geometry.cpp
        src/vk.imgui.cpp
        src/vk.math.cpp
        src/vk.memory.cpp
        src/vk.pipeline.cpp
        src/vk.swapchain.cpp
        src/vk.texture.cpp
        ${_IMGUI_SOURCES}
        PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        modules/vk.camera.ixx
        modules/vk.context.ixx
        modules/vk.frame.ixx
        modules/vk.geometry.ixx
        modules/vk.imgui.ixx
        modules/vk.math.ixx
        modules/vk.memory.ixx
        modules/vk.pipeline.ixx
        modules/vk.swapchain.ixx
        modules/vk.texture.ixx
)
target_link_libraries(vk-core PUBLIC Vulkan::Vulkan glfw)
target_compile_definitions(vk-core PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1)
target_include_directories(vk-core PUBLIC
        $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
)

# Linux/WSL (GCC+libstdc++): tell CMake this target uses the std module
# Do NOT set CMAKE_CXX_MODULE_STD globally, or it will affect glfw.
set_property(TARGET vk-core PROPERTY CXX_MODULE_STD ON)