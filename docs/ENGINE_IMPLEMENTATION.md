# Karma Engine — Implementation Notes

## Overview
Karma is a C++20 client/server 3D game engine with a layered architecture and a Diligent backend by default. It exposes a small public API for app control and an overlay abstraction, while keeping rendering, physics, audio, and networking pluggable via backend factories.

## Core Architecture
- **EngineApp** (`src/app/engine_app.cpp`) manages lifecycle, the main loop, and system updates.
- **ECS** holds world state and components (transforms, mesh, lights, physics, etc.).
- **Systems** update and render ECS data each frame.
- **Backends** are selected at build time (GLFW/SDL, Diligent, Jolt/Bullet, ENet, etc.).

## Rendering
- **Renderer entry**: `src/renderer/render_system.cpp` + `src/renderer/device.cpp`.
- **Backend abstraction**: `include/karma/renderer/backend.hpp`.
- **Diligent backend**: `src/renderer/backends/diligent/*`.
  - Handles swapchain creation, pipelines, texture uploads, shadow maps, etc.

### Shadows
- Directional light and shadow pipeline live in the Diligent backend.
- Shadow settings are controlled via engine config (bias, map size, pcf radius).
- Cascaded shadow maps (CSM) are integrated in the renderer.

## UI / Overlay Integration
- Core interface: `include/karma/ui/overlay.h`.
- **ImGui backend**: `src/ui/imgui_overlay.cpp` (optional build).
- **RmlUi backend**: `src/ui/rmlui_overlay.cpp` (optional build).
- The engine always interacts with `Overlay` only; UI specifics are isolated in backends.

### RmlUi Backend
- Uses RmlUi RenderInterface (not compatibility mode).
- Supports:
  - Geometry compilation, rendering, scissor
  - Texture load (via stb_image)
  - SVG plugin (optional)
  - Clip masks (stencil-based)
  - File interface for `link` and `template`
  - Hot reload of RML files
- Uses orthographic projection matching ImGui conventions.

### ImGui Backend
- Standard ImGui integration in a Diligent pipeline.
- Uses engine input events for mouse/keyboard.

## Optional Dependencies
These are disabled by default and enabled by CMake options:
- `KARMA_WITH_IMGUI`
- `KARMA_WITH_RMLUI`

If enabled, the build expects the user to provide the corresponding libraries, unless `KARMA_FETCH_DEPS=ON`.

## Build System Highlights
- Uses `FetchContent` for dependencies when `KARMA_FETCH_DEPS=ON`.
- Optional libs are only compiled when enabled.
- Demos only build if their UI backend is enabled.

## Backends
- **Window**: GLFW or SDL
- **Rendering**: Diligent (Vulkan default)
- **Physics**: Jolt or Bullet
- **Audio**: miniaudio or SDL
- **Networking**: ENet

## Notes
- Many paths in examples are absolute (for local data). Users should replace with relative or project-specific paths.
- Clip masks require a stencil-capable depth buffer (D24S8).

