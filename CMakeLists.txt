cmake_minimum_required(VERSION 3.28)
project(vulkan_visualizer VERSION 1.1.0 LANGUAGES CXX)

# ============================================================================
# C++ Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)


# ============================================================================
# Dependencies
# ============================================================================
find_package(Vulkan REQUIRED)
include(FetchContent)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.2
        GIT_SHALLOW TRUE
)

set(VK_BOOTSTRAP_WERROR OFF CACHE BOOL "" FORCE)
set(VK_BOOTSTRAP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(vk_bootstrap
        GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
        GIT_TAG v${Vulkan_VERSION}
        GIT_SHALLOW TRUE
)

FetchContent_Declare(VMA_src
        URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.3.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(_VMA_IMPL "${CMAKE_CURRENT_BINARY_DIR}/vma_impl.cpp")
file(WRITE "${_VMA_IMPL}" [[
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100 4189 4127 4324)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wconstant-conversion"
#pragma clang diagnostic ignored "-Wpadding"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
]])

FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        GIT_SHALLOW TRUE
)

set(_STB_IMPL "${CMAKE_CURRENT_BINARY_DIR}/stb_impl.cpp")
file(WRITE "${_STB_IMPL}" [[
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4100 4127 4189 4244 4365 4514 4820 4996)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
]])

FetchContent_MakeAvailable(SDL3 vk_bootstrap VMA_src stb)


# ============================================================================
# Library Target
# ============================================================================
add_library(vk_vis STATIC)
add_library(vulkan_visualizer::vk_vis ALIAS vk_vis)
target_sources(vk_vis
        PRIVATE
        src/vk.engine.cpp
        src/vk.context.cpp
        ${vk_bootstrap_SOURCE_DIR}/src/VkBootstrap.cpp
        ${_VMA_IMPL}
        ${_STB_IMPL}
        PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        module/vk.engine.ixx
        module/vk.context.ixx
)
target_link_libraries(vk_vis PUBLIC Vulkan::Vulkan)
if (TARGET SDL3-shared)
    target_link_libraries(vk_vis PUBLIC $<BUILD_INTERFACE:SDL3-shared>)
elseif (TARGET SDL3::SDL3)
    target_link_libraries(vk_vis PUBLIC $<BUILD_INTERFACE:SDL3::SDL3>)
endif ()
target_include_directories(vk_vis PRIVATE
        $<BUILD_INTERFACE:${sdl3_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${vk_bootstrap_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${vma_src_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${stb_SOURCE_DIR}>
)
