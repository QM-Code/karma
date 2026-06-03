# Karma Engine — Implementation Notes

## Overview
Karma is a C++20 client/server 3D game engine with a layered architecture and a Diligent backend by default. It exposes a small public API for app control and keeps rendering, physics, audio, and networking pluggable via backend factories.

## Core Architecture
- **EngineApp** (`src/runtime/app/engine_app.cpp`) manages lifecycle, the main loop, and system updates.
- **ECS** holds world state and components (transforms, mesh, lights, physics, etc.).
- **Systems** update and render ECS data each frame.
- **Backends** are selected at build time (GLFW/SDL, Diligent, Jolt/Bullet, ENet, etc.).

## Rendering
- **Renderer entry**: `src/rendering/renderer/render_system.cpp` + `src/rendering/renderer/device.cpp`.
- **Backend abstraction**: `include/karma/rendering/renderer/backend.hpp`.
- **Diligent backend**: `src/rendering/renderer/backends/diligent/*`.
  - Handles swapchain creation, pipelines, texture uploads, shadow maps, etc.

### Shadows
- Directional light and shadow pipeline live in the Diligent backend.
- Shadow settings are controlled via engine config (bias, map size, pcf radius).
- Cascaded shadow maps (CSM) are integrated in the renderer.

## UI / Draw Data Integration
- Core types: `include/karma/runtime/app/ui_draw_data.h` + `include/karma/runtime/app/ui_context.h`.
- Engine owns a `UIContext` and calls a user-provided `UiLayer` each frame.
- The renderer consumes `UIDrawData` and composites it over the 3D frame.
- UI provider adapters live under `include/karma/features/ui/<provider>` and
  `src/features/ui/<provider>`. They translate provider draw lists into
  `UIDrawData` behind factories returning `app::UiLayer`.

## Optional Dependencies
Optional dependencies are controlled via CMake (window/audio/physics/network backends). When `KARMA_FETCH_DEPS=ON`,
missing dependencies are fetched automatically. The ImGui demo is optional via `KARMA_BUILD_IMGUI_DEMO`.

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
