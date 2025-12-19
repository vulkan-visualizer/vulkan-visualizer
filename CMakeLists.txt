cmake_minimum_required(VERSION 3.28)
project(vulkan_visualizer VERSION 1.2.0 LANGUAGES CXX)

option(VK_XAYAH_BUILD_TESTS "Build the Vulkan Visualizer tests" OFF)

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
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
)
FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.2b-docking
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw imgui)

set(_VULKAN_HPP_IMPL "${CMAKE_CURRENT_BINARY_DIR}/_vulkan_hpp_impl.cpp")
file(WRITE "${_VULKAN_HPP_IMPL}" [[
#include <vulkan/vulkan_raii.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
]])
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
        # ${_VULKAN_HPP_IMPL}
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


# ============================================================================
# Test
# ============================================================================
if (VK_XAYAH_BUILD_TESTS)

    # ============================================================================
    # Shader Compilation
    # ============================================================================
    include(CMakeParseArguments)

    function(add_slang_shader_target)
        find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

        cmake_parse_arguments(
                SLANG
                ""
                "TARGET;OUTPUT_DIR"
                "SOURCES"
                ${ARGN}
        )

        if (NOT SLANG_TARGET)
            message(FATAL_ERROR "add_slang_shader_target: TARGET is required")
        endif ()

        if (NOT SLANG_SOURCES)
            message(FATAL_ERROR "add_slang_shader_target: SOURCES is required")
        endif ()

        if (NOT SLANG_OUTPUT_DIR)
            set(SLANG_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
        endif ()

        set(OUTPUTS)

        foreach (SRC ${SLANG_SOURCES})
            get_filename_component(SHADER_NAME ${SRC} NAME_WE)
            set(SPV ${SLANG_OUTPUT_DIR}/${SHADER_NAME}.spv)
            set(ENTRY_POINTS -entry vertMain -entry fragMain)
            add_custom_command(
                    OUTPUT ${SPV}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${SLANG_OUTPUT_DIR}
                    COMMAND ${SLANGC_EXECUTABLE}
                    ${SRC}
                    -target spirv
                    -profile spirv_1_4
                    -emit-spirv-directly
                    -fvk-use-entrypoint-name
                    ${ENTRY_POINTS}
                    -o ${SPV}
                    DEPENDS ${SRC}
                    COMMENT "Compiling Slang shader: ${SRC}"
                    VERBATIM
            )

            list(APPEND OUTPUTS ${SPV})
        endforeach ()

        add_custom_target(${SLANG_TARGET} ALL
                DEPENDS ${OUTPUTS}
        )

        set_target_properties(${SLANG_TARGET}
                PROPERTIES
                FOLDER "Shaders"
        )
    endfunction()

    file(GLOB SHADER_SLANG_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/test/shaders/*.slang
    )

    add_slang_shader_target(
            TARGET vk_slang_shaders
            SOURCES ${SHADER_SLANG_SOURCES}
    )


    enable_testing()
    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")
    foreach (test_src ${TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name} PRIVATE vk-core::vk-core)
        add_dependencies(${test_name} vk_slang_shaders)
        add_test(NAME run_${test_name} COMMAND ${test_name})
    endforeach ()
endif ()