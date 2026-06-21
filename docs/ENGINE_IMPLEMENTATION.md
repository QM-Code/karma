# Karma Engine — Implementation Notes

## Overview
Karma is a C++20 client/server 3D game engine with a layered architecture and a Diligent backend by default. It exposes a small public API for app control and keeps rendering, physics, audio, and networking pluggable via backend factories.

## Core Architecture
- **EngineApp** (`src/runtime/app/engine_app.cpp`) manages lifecycle, the main loop, and system updates.
- **ECS** holds world state and components (transforms, mesh, lights, physics, etc.).
- **Systems** update and render ECS data each frame.
- **Backends** are selected at build time (GLFW/SDL, Diligent, Jolt with experimental Bullet, ENet, etc.).

## Rendering
- **Renderer entry**: `src/rendering/renderer/render_system.cpp` +
  `src/rendering/renderer/render_system/*` + `src/rendering/renderer/device.cpp`.
- **Backend abstraction**: `src/private/rendering/backend.hpp`.
- **Diligent backend**: `src/rendering/renderer/backends/diligent/*`.
  - Handles swapchain creation, pipelines, texture uploads, shadow maps, etc.
- **Diligent post-process passes**:
  `src/rendering/renderer/backends/diligent/passes/post_process/*`.
  - Loads backend-owned shader assets from
    `src/rendering/renderer/backends/diligent/shaders/post_process/`.
  - Owns fullscreen composite, bloom mip-chain, temporal history, and fallback
    shader loading.

Post-processing is camera-resolved. `EngineApp` owns a
`AssetRegistry`, `CameraComponent::post_process_profile_key`
selects a named profile, and `RenderSystem` passes resolved settings into each
`GraphicsDevice::renderLayer` call. There is no global backend
`setPostProcessSettings` API.

### Shadows
- Directional light and shadow pipeline live in the Diligent backend.
- Shadow settings are controlled via engine config (bias, map size, pcf radius).
- Cascaded shadow maps (CSM) are integrated in the renderer.
- Point-light shadows are budgeted through `point_shadow_max_lights`.

## UI / Draw Data Integration
- Core types: `include/karma/app.h<provider>` and
  `src/features/ui/<provider>`. They translate provider draw lists into
  `rendering::UIDrawData` behind factories returning `app::UiLayer`.

## Optional Dependencies
Optional dependencies are controlled via CMake (window/audio/physics/network backends). When `KARMA_FETCH_DEPS=ON`,
missing dependencies are fetched automatically. The ImGui demo is optional via `KARMA_BUILD_IMGUI_DEMO`.
`KARMA_HEADLESS=ON` builds only the `karma::headless` non-visual profile: it
disables window/render backends, graphical UI providers, debug UI, audio
backends, graphics examples, and the rendered navmesh example. It does not
disable content import, physics, navigation, or networking by itself; those
remain controlled by their individual CMake switches.

## Build System Highlights
- Uses `FetchContent` for dependencies when `KARMA_FETCH_DEPS=ON`.
- Optional libs are only compiled when enabled.
- Demos only build if their UI backend is enabled.
- Generated public API docs are available through the `karma_docs_api` target
  when Doxygen is installed.

## Backends
- **Window**: GLFW or SDL
- **Rendering**: Diligent (Vulkan default)
- **Physics**: Jolt; Bullet is experimental
- **Audio**: miniaudio or SDL
- **Networking**: ENet

## Physics
- Public physics authoring lives in `include/karma/components.h`; runtime
  wrappers and backend-neutral descriptors live in `include/karma/physics.h`.
- `src/simulation/physics/physics_system.cpp` owns the high-level fixed-step
  phases. Neighboring `physics_system_*` files implement body, character,
  constraint, vehicle, soft-body, event, conversion, and cleanup details.
- ECS physics components are contracts, not backend object storage. The physics
  system owns native handles, contact caches, vehicle telemetry, and soft-body
  snapshots, then writes only small public status fields back to components.
- Each `CharacterControllerComponent` owns one backend character controller via
  the physics system. Controllers require `TransformComponent` and a box
  `ColliderComponent`; movement is driven through component command methods.

## Notes
- Clip masks require a stencil-capable depth buffer (D24S8).
