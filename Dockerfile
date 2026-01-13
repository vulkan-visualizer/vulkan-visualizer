# syntax=docker/dockerfile:1.7
FROM archlinux:latest

# ---- system bootstrap (keyring + update) ----
RUN set -eux; \
    pacman-key --init; \
    pacman-key --populate archlinux; \
    pacman -Syu --noconfirm

# ---- toolchain + build tools (GCC 15, CMake >= 4.0, Ninja) ----
# base-devel: make, binutils, etc.
# pkgconf: for pkg-config checks used by GLFW (wayland/x11)
RUN set -eux; \
    pacman -S --noconfirm --needed \
      base-devel \
      git \
      ca-certificates \
      cmake \
      ninja \
      gcc \
      pkgconf

# ---- Vulkan SDK components (headers + loader + tools + validation layers) ----
# vulkan-devel: group (headers, loader, layers, tools, volk, etc.)
# shaderc: provides glslc
# glslang: provides glslangValidator
RUN set -eux; \
    pacman -S --noconfirm --needed \
      vulkan-devel \
      shaderc \
      glslang

# ---- GLFW platform deps (fix the exact missing headers you hit) ----
# X11: RandR / Xinerama / Xcursor / Xi are the classic ones GLFW checks for.
# xorgproto: common X11 protocol headers used by various X libs.
RUN set -eux; \
    pacman -S --noconfirm --needed \
      xorgproto \
      libx11 \
      libxrandr \
      libxinerama \
      libxcursor \
      libxi \
      libxext

# ---- Wayland deps (your log shows GLFW enabling Wayland too) ----
RUN set -eux; \
    pacman -S --noconfirm --needed \
      wayland \
      wayland-protocols \
      libxkbcommon

# (Optional but often helpful for diagnosing builds)
# RUN pacman -S --noconfirm --needed ccache gdb

WORKDIR /src
COPY . /src

# ---- configure & build ----
# Notes:
# - Use Ninja generator (CMake modules support requires Ninja/VS; Makefiles won't work for modules scanning)
# - Force GNU extensions ON to match your successful -std=gnu++23 build on Linux
# - If your CMakeLists already sets CMAKE_CXX_EXTENSIONS OFF and you rely on gnu++23 for import std,
#   passing -DCMAKE_CXX_EXTENSIONS=ON here makes it deterministic.
RUN set -eux; \
    cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_EXTENSIONS=ON; \
    cmake --build build --parallel

# Default command: show toolchain versions (useful when you `docker run` to sanity check)
CMD ["bash", "-lc", "g++ --version && cmake --version && ninja --version"]
