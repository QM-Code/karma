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

Rendering is frame-graph resolved per camera. `EngineApp` owns an
`AssetRegistry`, `CameraComponent::frame_graph_key` selects a named graph, and
`RenderSystem` passes the resolved `FrameGraphDesc` into each
`GraphicsDevice::renderLayer` call. There is no global backend graph state API.
Threaded latest-frame replacement carries durable renderer mutations, such as
instance retirement and environment changes, into the replacement packet so
latency control cannot leave stale backend state.

### Shadows
- Directional light and shadow pipeline live in the Diligent backend.
- Shadow settings are controlled via engine config (bias, map size, pcf radius).
- Cascaded shadow maps (CSM) are integrated in the renderer.
- Point-light shadows are budgeted through `point_shadow_max_lights`.
- Standard and terrain PBR apply material AO to indirect lighting, including
  environment specular occlusion, rather than directional direct light.
- Forward+ keeps a small direct path for up to eight local lights when compute
  culling is available; the non-compute fallback retains its 64-light budget.

## UI / Draw Data Integration
- `EngineApp` owns the first-party `ui::System` when native UI is enabled and
  renders it between the scene and optional `UiLayer` extension.
- Optional providers live in `src/features/ui/<provider>` and translate their
  draw lists into `rendering::UIDrawData` behind factories returning
  `app::UiLayer`.
- Non-empty UI providers are composed in visual order into one renderer
  submission, with a no-copy single-layer path. Stateful release events are
  broadcast down the reverse input stack even when a higher layer consumes
  them.
- The native runtime uses concrete private services for document/element
  lifetime, reconciliation dependencies, style/motion execution, layout,
  transients, presentation resources, retained draw assembly, accessibility,
  and hot reload under `src/features/ui/native/`. Six small System translation
  units retain only lifecycle, public delegation, reconciliation coordination,
  interaction, input routing, and frame sequencing. See
  [NATIVE_UI.md](NATIVE_UI.md) for behavior and
  [NATIVE_UI_STATUS.md](NATIVE_UI_STATUS.md) for the full module map,
  invariants, and verification matrix.
- Native documents and themes are canonical JSON assets imported from
  `.kui.json5` and `.kstyle.json5`. The public authoring model has explicit
  bindings/actions and named theme styles rather than an authored selector
  language. Sandboxed development graphs add direct files, watcher/debounce,
  staging, and last-good frame-boundary reload.
- `src/features/ui/native/system_impl.h` is private and declaration-only;
  runtime modules do not include it. `system_frame.cpp` intentionally retains
  the ordered reload/reconcile/style/motion/layout/paint/accessibility sequence,
  and `system_input.cpp` retains platform-event routing so neither boundary is
  replaced with a callback host mirroring `System::Impl`.

## Optional Dependencies
Optional dependencies are controlled via CMake (window/audio/physics/network backends). When `KARMA_FETCH_DEPS=ON`,
missing dependencies are fetched automatically. ImGui support is opt-in via
`KARMA_ENABLE_IMGUI`; its demo is controlled by `KARMA_BUILD_IMGUI_DEMO`.
Native UI is enabled for graphical builds by `KARMA_ENABLE_NATIVE_UI`. Its
authoring pipeline uses Karma's deterministic JSON5 profile over nlohmann JSON;
Yoga, FreeType, HarfBuzz, ICU, and LunaSVG provide layout, text, and static SVG
implementation details. ImGui and RmlUi remain separate optional providers.
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
