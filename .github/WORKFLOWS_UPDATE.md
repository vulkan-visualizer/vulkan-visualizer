# GitHub Actions Workflows - Update Summary

## Overview
All GitHub Actions workflows have been updated to match the current project structure (library-only, no examples) and to support automatic release creation with portable packages.

## Updated Workflows

### 1. **linux-build.yml** ✅
**Changes:**
- Removed outdated example executable packaging
- Added proper CMake install step
- Enabled `VV_WARNINGS_AS_ERRORS=ON` for stricter compilation
- Creates portable tar.gz package from installed files
- Package name: `vulkan-visualizer-linux-x64-{Release|Debug}.tar.gz`

### 2. **macos-build.yml** ✅
**Changes:**
- Removed outdated example executable packaging
- Added proper CMake install step
- Enabled `VV_WARNINGS_AS_ERRORS=ON` for stricter compilation
- Creates portable tar.gz package from installed files
- Package name: `vulkan-visualizer-macos-universal-{Release|Debug}.tar.gz`

### 3. **windows-build.yml** ✅
**Changes:**
- Removed outdated example executable packaging
- Added proper CMake install step with `--config` flag
- Enabled `VV_WARNINGS_AS_ERRORS=ON` for stricter compilation
- Creates portable ZIP package from installed files
- Package name: `vulkan-visualizer-windows-x64-{Release|Debug}.zip`

### 4. **release.yml** ✨ NEW
**Purpose:** Automatically create GitHub releases when version tags are pushed

**Trigger:**
- When pushing tags matching `v*.*.*` (e.g., `v1.0.0`, `v2.1.3`)
- Manual workflow dispatch with custom tag input

**Features:**
- Creates a GitHub release with auto-generated description
- Builds Release configuration for all three platforms in parallel
- Automatically uploads portable packages as release assets:
  - `vulkan-visualizer-windows-x64-Release.zip`
  - `vulkan-visualizer-linux-x64-Release.tar.gz`
  - `vulkan-visualizer-macos-universal-Release.tar.gz`

## Package Contents

Each portable package includes:
- **Static library** (`vulkan_visualizer.lib` on Windows, `libvulkan_visualizer.a` on Unix)
- **Public headers** (from `include/` directory)
- **Runtime dependencies** (SDL3.dll on Windows, SDL3 library on other platforms)
- **CMake configuration files** for easy integration into other projects
  - `VulkanVisualizerConfig.cmake`
  - `VulkanVisualizerConfigVersion.cmake`
  - `VulkanVisualizerTargets.cmake`

## How to Create a Release

1. **Tag your commit:**
   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```

2. **Automatic process:**
   - GitHub Actions will trigger the `release` workflow
   - All three platforms will build in parallel
   - A new GitHub release will be created
   - All portable packages will be uploaded as release assets

3. **Manual trigger (optional):**
   - Go to Actions → release → Run workflow
   - Enter the tag name
   - Click "Run workflow"

## CI/CD Pipeline Flow

### Regular Builds (Push/PR to main/master)
```
Push/PR → [Linux Build] → Test & Package
       → [macOS Build] → Test & Package  
       → [Windows Build] → Test & Package
```

### Release Builds (Tag push)
```
Tag Push v*.*.* → Create Release
                → [Windows Build] → Upload to Release
                → [Linux Build] → Upload to Release
                → [macOS Build] → Upload to Release
```

## Key Improvements

1. ✅ **Warnings as Errors**: All builds now use `-DVV_WARNINGS_AS_ERRORS=ON`
2. ✅ **Proper Install**: Uses CMake's install feature instead of manual file copying
3. ✅ **Consistent Naming**: Standardized package names across platforms
4. ✅ **Release Automation**: No manual steps needed for releases
5. ✅ **Portable Packages**: Self-contained packages ready for distribution
6. ✅ **Dependency Caching**: Third-party dependencies are cached for faster builds
7. ✅ **Parallel Builds**: Release workflow builds all platforms simultaneously

## Testing

To test locally before pushing:
```bash
# Windows
cmake -S . -B build -G "Ninja Multi-Config" -DVV_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
cmake --install build --config Release --prefix install

# Linux/macOS
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DVV_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
cmake --install build --prefix install
```

## Notes

- All workflows use the latest Vulkan SDK
- Dependencies are fetched via CMake FetchContent (ImGui, SDL3, vk-bootstrap, VMA, stb)
- Build artifacts are retained for 7 days for debugging
- Release assets are permanent once uploaded to GitHub releases

