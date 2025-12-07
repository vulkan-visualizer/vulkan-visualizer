cmake_minimum_required(VERSION 3.28)
project(vulkan_visualizer VERSION 1.1.0 LANGUAGES CXX)

option(VK_VISUALIZER_BUILD_TESTS "Build the Vulkan Visualizer tests" ON)

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

FetchContent_Declare(imgui_src
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.2b-docking
        GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(SDL3 vk_bootstrap VMA_src stb imgui_src)

set(_VK_BOOTSTRAP_SOURCES
        ${vk_bootstrap_SOURCE_DIR}/src/VkBootstrap.cpp
)
set(_IMGUI_SOURCES
        ${imgui_src_SOURCE_DIR}/imgui.cpp
        ${imgui_src_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_src_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_src_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_src_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_src_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_src_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)


# ============================================================================
# Library Target
# ============================================================================
add_library(vk_vis STATIC)
add_library(vulkan_visualizer::vk_vis ALIAS vk_vis)
target_sources(vk_vis
        PRIVATE
        src/vk.engine.cpp
        src/vk.context.cpp
        src/vk.camera.cpp
        src/vk.plugins.cpp
        ${_VK_BOOTSTRAP_SOURCES}
        ${_VMA_IMPL}
        ${_STB_IMPL}
        ${_IMGUI_SOURCES}
        PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES
        module/vk.engine.ixx
        module/vk.context.ixx
        module/vk.camera.ixx
        module/vk.plugins.ixx
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
        $<BUILD_INTERFACE:${imgui_src_SOURCE_DIR}>
)
target_include_directories(vk_vis PUBLIC $<BUILD_INTERFACE:${imgui_src_SOURCE_DIR}>)


# ============================================================================
# Shader Compilation
# ============================================================================

function(compile_all_glsl_shaders)
    cmake_parse_arguments(ARG "" "SOURCE_ROOT" "" ${ARGN})

    if (NOT ARG_SOURCE_ROOT)
        message(FATAL_ERROR "compile_all_glsl_shaders: SOURCE_ROOT is required")
    endif ()

    find_program(GLSLC_EXECUTABLE glslc REQUIRED)
    message(STATUS "Using glslc: ${GLSLC_EXECUTABLE}")

    # Find shader directories but ignore _deps
    file(GLOB_RECURSE SHADER_DIRS
            LIST_DIRECTORIES true
            RELATIVE "${ARG_SOURCE_ROOT}"
            "${ARG_SOURCE_ROOT}/*/shader"
            "${ARG_SOURCE_ROOT}/*/shaders"
            "${ARG_SOURCE_ROOT}/shader"
            "${ARG_SOURCE_ROOT}/shaders"
    )

    set(SPIRV_OUTPUTS)

    foreach (dir IN LISTS SHADER_DIRS)
        set(ABS_DIR "${ARG_SOURCE_ROOT}/${dir}")

        file(GLOB SHADER_SOURCES
                RELATIVE "${ARG_SOURCE_ROOT}"
                "${ABS_DIR}/*.vert"
                "${ABS_DIR}/*.frag"
                "${ABS_DIR}/*.comp"
                "${ABS_DIR}/*.geom"
                "${ABS_DIR}/*.tesc"
                "${ABS_DIR}/*.tese"
        )

        foreach (src IN LISTS SHADER_SOURCES)
            set(ABS_SRC "${ARG_SOURCE_ROOT}/${src}")
            set(SPV "${CMAKE_BINARY_DIR}/${src}.spv")

            get_filename_component(SPV_DIR "${SPV}" DIRECTORY)
            file(MAKE_DIRECTORY "${SPV_DIR}")

            add_custom_command(
                    OUTPUT "${SPV}"
                    COMMAND "${GLSLC_EXECUTABLE}"
                    --target-env=vulkan1.3
                    -O
                    -c
                    "${ABS_SRC}"
                    -o "${SPV}"
                    DEPENDS "${ABS_SRC}"
                    COMMENT "Compiling GLSL ${src}"
                    VERBATIM
            )

            list(APPEND SPIRV_OUTPUTS "${SPV}")
        endforeach ()
    endforeach ()

    if (SPIRV_OUTPUTS)
        add_custom_target(shaders DEPENDS ${SPIRV_OUTPUTS})
        message(STATUS "Shader build target created: shaders")
    endif ()
endfunction()
compile_all_glsl_shaders(SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})


# ============================================================================
# Test
# ============================================================================
if (VK_VISUALIZER_BUILD_TESTS)
    enable_testing()

    file(GLOB TEST_SOURCES
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp"
    )

    foreach (test_src ${TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)

        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name} PRIVATE vulkan_visualizer::vk_vis)
        add_dependencies(${test_name} shaders)

        add_test(NAME run_${test_name} COMMAND ${test_name})

        if (WIN32)
            add_custom_command(
                    TARGET ${test_name}
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${test_name}>
                    $<TARGET_FILE_DIR:${test_name}>
                    COMMAND_EXPAND_LISTS
            )
        endif ()
    endforeach ()
endif ()
