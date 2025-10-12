cmake_minimum_required(VERSION 3.26)
project(VulkanVisualizer VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# Build Options
# ============================================================================
option(VV_WITH_LOGGING "Enable logging system" ON)
option(VV_WITH_GPU_TIMESTAMPS "Enable GPU timestamp queries" ON)
option(VV_WITH_TONEMAP "Enable tonemapping pass" ON)
option(VV_WITH_SCREENSHOT "Enable screenshot utilities" ON)
option(VV_WITH_HOTRELOAD "Enable shader hot-reload" ON)
option(VV_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# ============================================================================
# C++ Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# Dependencies
# ============================================================================
# Only PUBLIC dependency (exposed in headers)
find_package(Vulkan REQUIRED)

# Fetch all internal dependencies
include(FetchContent)

FetchContent_Declare(imgui_src
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.2b-docking
    GIT_SHALLOW TRUE
)

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

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(imgui_src SDL3 vk_bootstrap VMA_src stb)

# ============================================================================
# Library Target
# ============================================================================
# Collect all sources
set(_THIRD_PARTY_SOURCES
    ${imgui_src_SOURCE_DIR}/imgui.cpp
    ${imgui_src_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_src_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_src_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_src_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${vk_bootstrap_SOURCE_DIR}/src/VkBootstrap.cpp
)

# Generate STB implementation
set(_STB_IMPL "${CMAKE_CURRENT_BINARY_DIR}/stb_impl.cpp")
file(WRITE "${_STB_IMPL}"
"#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4100 4127 4189 4244 4365 4514 4820 4996)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
")

# Create library
add_library(vulkan_visualizer STATIC
    src/vk_engine.cpp
    src/vv_camera.cpp
    ${_THIRD_PARTY_SOURCES}
    ${_STB_IMPL}
)

add_library(VulkanVisualizer::vulkan_visualizer ALIAS vulkan_visualizer)

# ============================================================================
# Target Configuration
# ============================================================================
# Public interface
target_include_directories(vulkan_visualizer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(vulkan_visualizer PUBLIC
    $<$<BOOL:${VV_WITH_LOGGING}>:VV_ENABLE_LOGGING>
    $<$<BOOL:${VV_WITH_GPU_TIMESTAMPS}>:VV_ENABLE_GPU_TIMESTAMPS>
    $<$<BOOL:${VV_WITH_TONEMAP}>:VV_ENABLE_TONEMAP>
    $<$<BOOL:${VV_WITH_SCREENSHOT}>:VV_ENABLE_SCREENSHOT>
    $<$<BOOL:${VV_WITH_HOTRELOAD}>:VV_ENABLE_HOTRELOAD>
)

target_link_libraries(vulkan_visualizer PUBLIC Vulkan::Vulkan)

# Private include directories for embedded dependencies
target_include_directories(vulkan_visualizer PRIVATE
    ${imgui_src_SOURCE_DIR}
    ${imgui_src_SOURCE_DIR}/backends
    ${vk_bootstrap_SOURCE_DIR}/src
    ${sdl3_SOURCE_DIR}/include
    ${vma_src_SOURCE_DIR}/include
    ${stb_SOURCE_DIR}
)

# Link SDL3 (wrapped to avoid export)
if(TARGET SDL3-shared)
    target_link_libraries(vulkan_visualizer PRIVATE $<BUILD_INTERFACE:SDL3-shared>)
elseif(TARGET SDL3::SDL3)
    target_link_libraries(vulkan_visualizer PRIVATE $<BUILD_INTERFACE:SDL3::SDL3>)
endif()

# Disable specific warnings for third-party sources
if(MSVC)
    # Disable C4819 (encoding warning) for third-party files that include SDL3/vk-bootstrap headers
    set_source_files_properties(
        src/vk_engine.cpp
        src/vv_camera.cpp
        ${_THIRD_PARTY_SOURCES}
        PROPERTIES
        COMPILE_FLAGS "/wd4819"
    )
endif()

# Compiler optimizations
if(MSVC)
    target_compile_options(vulkan_visualizer PRIVATE
        $<$<CONFIG:Release>:/O2 /Ot /Oi /GL /fp:fast>
        # Warnings as errors
        $<$<BOOL:${VV_WARNINGS_AS_ERRORS}>:/WX>
        # Standard warning level
        /W4
    )
else()
    target_compile_options(vulkan_visualizer PRIVATE
        $<$<CONFIG:Release>:-O3 -ffast-math -fomit-frame-pointer>
        # Warnings as errors
        $<$<BOOL:${VV_WARNINGS_AS_ERRORS}>:-Werror>
        # Enable common warnings
        -Wall -Wextra -Wpedantic
    )
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|X86_64|AMD64")
        target_compile_options(vulkan_visualizer PRIVATE
            $<$<CONFIG:Release>:-march=native>
        )
    endif()
endif()

# ============================================================================
# Installation
# ============================================================================
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS vulkan_visualizer
    EXPORT VulkanVisualizerTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Install runtime dependencies (DLLs on Windows)
if(WIN32)
    # Install SDL3 DLL
    if(TARGET SDL3-shared)
        install(FILES $<TARGET_FILE:SDL3-shared>
            DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    # Install Vulkan DLL if present in build directory
    if(EXISTS "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/vulkan-1.dll")
        install(FILES "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/vulkan-1.dll"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    # Install runtime dependencies using CMake's file(GET_RUNTIME_DEPENDENCIES)
    # This will be executed during install, not during configure
    install(CODE "
        file(GET_RUNTIME_DEPENDENCIES
            RESOLVED_DEPENDENCIES_VAR _resolved_deps
            UNRESOLVED_DEPENDENCIES_VAR _unresolved_deps
            LIBRARIES \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/vulkan_visualizer.lib
            EXECUTABLES
            DIRECTORIES
                \$ENV{PATH}
                ${CMAKE_BINARY_DIR}/bin/\${CMAKE_INSTALL_CONFIG_NAME}
                ${CMAKE_BINARY_DIR}/bin
        )

        foreach(_dep \${_resolved_deps})
            get_filename_component(_dep_name \"\${_dep}\" NAME)
            # Only copy DLLs we explicitly depend on (SDL3, avoid system DLLs)
            if(_dep_name MATCHES \"^SDL3.*\\.dll$\" OR _dep_name MATCHES \"^vulkan.*\\.dll$\")
                file(INSTALL \"\${_dep}\"
                    DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\"
                    FOLLOW_SYMLINK_CHAIN
                )
                message(STATUS \"Installed runtime dependency: \${_dep_name}\")
            endif()
        endforeach()

        if(_unresolved_deps)
            message(WARNING \"Unresolved runtime dependencies: \${_unresolved_deps}\")
        endif()
    ")
endif()

set(_config_dir ${CMAKE_INSTALL_LIBDIR}/cmake/VulkanVisualizer)

install(EXPORT VulkanVisualizerTargets
    FILE VulkanVisualizerTargets.cmake
    NAMESPACE VulkanVisualizer::
    DESTINATION ${_config_dir}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/VulkanVisualizerConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VulkanVisualizerConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/VulkanVisualizerConfig.cmake
    INSTALL_DESTINATION ${_config_dir}
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/VulkanVisualizerConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/VulkanVisualizerConfigVersion.cmake
    DESTINATION ${_config_dir}
)

export(EXPORT VulkanVisualizerTargets
    FILE ${CMAKE_CURRENT_BINARY_DIR}/VulkanVisualizerTargets.cmake
    NAMESPACE VulkanVisualizer::
)
