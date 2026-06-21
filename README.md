<p align="center">
  <img src="docs/logo.png" alt="Karma Engine logo" width="180">
</p>

# Karma Engine

<p align="center">
  <a href="VERSION"><img alt="Version" src="https://img.shields.io/badge/dynamic/regex?url=https%3A%2F%2Fraw.githubusercontent.com%2FQM-Code%2Fkarma%2Fmain%2FVERSION&amp;search=%28.%2A%29&amp;replace=%241&amp;label=version&amp;color=blue"></a>
</p>

Karma is a C++20 ECS-driven 3D game engine. The current tree is a working
static library plus examples, with a layered runtime, Diligent/Vulkan rendering,
prefabs, particles, glTF/GLB import, animation/skinning, physics/collision,
navigation, audio, networking, and UI adapters.

Published API docs: <https://qm-code.github.io/karma/>

License: [MIT](LICENSE)

## Current State

Karma is organized as layered engine code under `include/karma/<layer>` and
`src/<layer>`. The important layers are:

- `core`: IDs, type helpers, math, and timing.
- `world`: ECS, components, system graph, and scene hierarchy.
- `simulation`: animation, physics, collision, and Recast/Detour navigation.
- `rendering`: renderer-facing APIs, renderer systems, and Diligent backend.
- `media`: audio APIs and backends.
- `content`: importers, geometry loading, prefabs, and resource sidecars.
- `platform`: window and network edges.
- `features`: optional visual/UI feature modules.
- `runtime`: `EngineApp`, input, UI context, debug overlay, and app wiring.

The default graphical profile currently enables GLFW, Diligent, miniaudio,
ENet, Jolt, navigation, debug UI, and the ImGui demo. RmlUi is opt-in because
system RmlUi/FreeType packages vary more across machines. The server profile is
a minimal ECS/network surface, while the headless profile keeps the broader
non-visual runtime surface and omits window, graphical renderer, UI provider,
and audio backends by default.

## Quick Start

Use the repo build script for the default graphical build. It configures with
fetched dependencies and builds examples/tests by default:

```bash
./build.sh
```

On Windows:

```bat
build.bat
```

Useful script options:

```bash
./build.sh --headless
./build.sh --headless-only --minimal-headless --no-examples --no-tests
./build.sh --no-examples --config Debug --jobs 4
```

Equivalent manual CMake commands:

```bash
cmake -S . -B build \
  -DKARMA_FETCH_DEPS=ON

cmake --build build --parallel
./build/examples/gameplay/tank
```

The same profile is available as a CMake preset:

```bash
cmake --preset portable
cmake --build --preset portable
```

Useful focused targets:

```bash
cmake --build build --target navigation_navmesh --parallel
cmake --build build --target prefabs_gallery --parallel
cmake --build build --target particles_explosion_stress --parallel
cmake --build build --target karma_animation_tests --parallel
ctest --test-dir build --output-on-failure
```

For a headless build that skips window/render backends and graphics demos:

```bash
cmake --preset headless
cmake --build --preset headless --target network_server
```

`KARMA_HEADLESS` builds the non-visual `karma::headless` runtime profile and
disables window, render, graphical UI provider, debug UI, audio backend,
graphics examples, and rendered navmesh example targets. The minimal
`karma::server` profile is controlled separately and defaults to enabled. Use
these non-visual profiles for network/server targets, simulation/gameplay logic,
and tests that do not need a window or GPU. Runtime code should treat graphics
handles as optional in headless mode; renderer-facing APIs either no-op or return
invalid handles when no backend exists.

Headless is also not a minimal dependency preset by itself. Content import,
physics, navigation, and networking stay enabled by default unless their own
CMake options are disabled. Use `minimal-headless` when you want the smallest
build surface for non-visual code:

```bash
cmake --preset minimal-headless
cmake --build --preset minimal-headless
```

## Using Karma As A Dependency

Current consumer import status:

- Supported paths are source-vendored CMake import and installed CMake package
  import.
- Consumers choose a public profile target:
  - `karma::server`: minimal ECS/network server profile.
  - `karma::headless`: server/non-visual runtime profile.
  - `karma::graphical`: full graphical runtime profile.
  - `karma::karma`: compatibility alias for `karma::graphical` when the
    graphical profile is built.
- Public includes are root headers under `include/karma`; consumers should
  include profile headers such as `<karma/server.h>`, `<karma/headless.h>`, or
  `<karma/karma.h>`, or focused domain headers such as `<karma/assets.h>`.
- The engine is currently built as a static C++20 library. It is still moving
  quickly, so source-vendoring is the most flexible integration path during
  active development.
- GitHub CI smoke-tests both consumer paths on Linux, macOS, and Windows. The
  smoke tests build small external executables, link `karma::server`,
  `karma::headless`, `karma::graphical`, and `karma::karma`, and exercise basic
  public headers and linkage.
- Installed packages do not fetch missing third-party dependency targets during
  `find_package(karma)` by default. Set `KARMA_CONFIG_FETCH_DEPS=ON` before
  `find_package(karma)` if package-time dependency fetching is desired.
- Package-manager support is available through the root vcpkg manifest and the
  local `ports/karma` overlay port. See [Packaging](docs/PACKAGING.md).

For source-vendored use, add Karma as a subdirectory and link the namespaced
target:

```cmake
set(KARMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(KARMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(KARMA_FETCH_DEPS ON CACHE BOOL "" FORCE)

add_subdirectory(external/karma)

target_link_libraries(my_server PRIVATE karma::server)
target_link_libraries(my_simulation_tool PRIVATE karma::headless)
target_link_libraries(my_game PRIVATE karma::graphical)
```

For `FetchContent`, set the same options before `FetchContent_MakeAvailable`.

Karma also installs a CMake package:

```bash
cmake -S . -B build/package \
  -DKARMA_HEADLESS=ON \
  -DKARMA_ENABLE_AUDIO=OFF \
  -DKARMA_ENABLE_NAVIGATION=OFF \
  -DKARMA_NETWORK_BACKEND_ENET=OFF \
  -DKARMA_PHYSICS_BACKEND_JOLT=OFF \
  -DKARMA_BUILD_EXAMPLES=OFF \
  -DKARMA_BUILD_TESTS=OFF \
  -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX=/path/to/karma-install
cmake --build build/package --parallel
cmake --install build/package
```

Consumers can then use the installed server or headless profile:

```cmake
find_package(karma CONFIG REQUIRED)
target_link_libraries(my_server PRIVATE karma::server)
```

For graphical installs, use `karma::graphical` or `karma::karma` as the
convenient default graphical target. The installed package config requires
dependency targets to be available by default; set `KARMA_CONFIG_FETCH_DEPS=ON`
before `find_package(karma)` to allow package-time fetching.

## Major Features

- ECS runtime with fixed-step updates, per-frame systems, scene graph transforms,
  app/game interfaces, and optional runtime modules.
- Diligent renderer with Forward+ local lights, directional shadows, point-light
  shadows, camera-selected post-process profiles, bloom/tone/color controls,
  transparent passes, debug lines, particles, UI draw-data composition, and
  environment/texture/mesh resource management.
- Content pipeline for glTF/GLB scene import, materials, lights, node animation,
  skeletal animation, GPU skinning with CPU fallback, morph target deformation,
  JSON prefabs, and prefab-local `assets.package.json` registrations.
- Particle tooling with `.kpeffect` files, hot reload, emitter overrides,
  flipbooks, distortion, ground-aligned particles, soft particles, and renderer
  performance diagnostics.
- Visual feature modules for particle beam prefabs, analytic volumetric solids,
  light pulses, energy orbs, staged explosions, and prefab gallery scenes.
- Simulation stack with Jolt physics as the production backend and an
  experimental Bullet backend, collision/contact ECS
  events, grounded/support state, character controllers, Jolt constraints,
  filtered queries, vehicles, soft bodies, static navmesh baking, and Detour
  path queries.
- UI adapters for ImGui and RmlUi behind `runtime/app/UiLayer`.
- Audio and networking through miniaudio/SDL and ENet-backed abstractions.

## TODO

- Investigate a VDB/NanoVDB-backed volume renderer using
  [GPU Volume Rendering with Hierarchical Compression Using VDB](2504.04564v2.pdf)
  as the design reference. The paper is enough to define the architecture
  (OpenVDB CPU compression, NanoVDB GPU sampling, sparse texture-like renderer
  backend), but implementation should also use OpenVDB/NanoVDB docs or samples
  for exact API, device-memory, coordinate-transform, filtering, and traversal
  details.

## Examples

The examples under [examples/](examples/) cover the current engine surface and
build into category directories under `build/examples/`:

- `gameplay_tank`: tank/world movement demo emitted as
  `build/examples/gameplay/tank`.
- `physics_collision_events`, `physics_shape_gallery`,
  `physics_body_controls`, `physics_query_lab`, `physics_constraint_lab`, and
  `physics_car`: collision, rigid-body, query, constraint, and vehicle samples
  emitted under `build/examples/physics`.
- `rendering_gltf_viewer`, `rendering_postprocess`, `rendering_bloom`,
  `rendering_postwar_city`, `rendering_light_stress`,
  `rendering_material_assignment`, `rendering_grass_card`,
  `rendering_grass_field`, and
  `rendering_terrain`: rendering inspection and stress samples.
- `scene_gltf_import` and `animation_gltf`: authored glTF/GLB scene import,
  animation, deformation, and skinning paths.
- `particles_*`, `effects_*`, and `prefabs_*`: particle, visual effect, and
  prefab proof/stress scenes.
- `navigation_*`: click-to-move, sample-gallery, crowd, tile-cache, query,
  off-mesh, and physics-bridge navigation examples.
- `ui_imgui`, `ui_rmlui`, `network_server`, and `network_client`: provider and
  platform demos.

See [examples/README.md](examples/README.md) for the full target list and
runtime flags.

## Build Options

Common CMake switches:

- `KARMA_FETCH_DEPS`: fetch missing third-party dependencies with CMake
  `FetchContent`.
- `KARMA_ASSIMP_MINIMAL_IMPORTERS`: when fetching Assimp, build only GLTF/GLB,
  OBJ, and STL import support.
- `KARMA_BUILD_EXAMPLES`: build example executable targets.
- `KARMA_BUILD_TESTS`: build Karma test executable targets when `BUILD_TESTING`
  is also enabled.
- `KARMA_BUILD_SERVER_PROFILE`: build the minimal `karma::server` target.
- `KARMA_BUILD_HEADLESS_PROFILE`: build the `karma::headless` target.
- `KARMA_BUILD_GRAPHICAL_PROFILE`: build the `karma::graphical` and
  `karma::karma` targets.
- `KARMA_HEADLESS`: legacy shortcut for building the non-visual
  `karma::headless` runtime profile; disables graphical and audio backends plus
  graphics demos.
- `KARMA_RENDER_BACKEND_DILIGENT`: enable the Diligent renderer.
- `KARMA_WINDOW_BACKEND_GLFW` / `KARMA_WINDOW_BACKEND_SDL`: select one window
  backend.
- `KARMA_PHYSICS_BACKEND_JOLT` / `KARMA_PHYSICS_BACKEND_BULLET`: select one
  physics backend. Jolt is the production backend; Bullet is experimental.
- `KARMA_ENABLE_AUDIO`: enable or disable runtime audio backend creation.
- `KARMA_AUDIO_BACKEND_MINIAUDIO` / `KARMA_AUDIO_BACKEND_SDL`: select one audio
  backend.
- `KARMA_NETWORK_BACKEND_ENET`: build the default ENet networking transport and
  split network demo targets when their profiles are enabled.
- `KARMA_ENABLE_NAVIGATION`: build Recast/Detour navigation support.
- `KARMA_BUILD_DEBUG_UI`: build the runtime debug overlay.
- `KARMA_BUILD_IMGUI_DEMO`, `KARMA_BUILD_RMLUI_DEMO`, `KARMA_ENABLE_RMLUI`:
  optional UI demos/adapters.

## CI/CD

Default pull-request and `main` validation lives at `.github/workflows/ci.yml`.
It builds the Ubuntu headless profile with examples and tests enabled, runs
CTest, and runs a separate AddressSanitizer/UBSan debug test job.

The heavier cross-platform workflow lives at
`.github/workflows/full-build.yml`. It runs on Linux, macOS, and Windows for
manual dispatches, pushes to `main`, and pull requests labeled
`ci/full-build`. The workflow builds headless profiles with examples, runs
CTest, builds the graphical profile and graphical examples without launching a
window, installs the minimal headless package, uploads that install tree as a
workflow artifact, and builds source/installed consumer smoke projects.

Code scanning lives at `.github/workflows/codeql.yml` and runs CodeQL for C/C++
on pull requests, pushes to `main`, weekly schedule, and manual dispatches.
API docs publishing lives at `.github/workflows/docs.yml`; it builds the
Doxygen API docs on pull requests and deploys them to GitHub Pages from `main`
when the repository Pages source is set to GitHub Actions.
Release packaging lives at `.github/workflows/release.yml`; pushing a `v*` tag
builds minimal headless packages on Linux, macOS, and Windows, uploads workflow
artifacts, and publishes them to the matching GitHub release.

## Versioning

The root [VERSION](VERSION) file is the source of truth for the CMake project
version, generated package version files, and `<karma/version.h>`. Karma
uses SemVer `0.x`: breaking changes are allowed before `1.0.0`, but version
updates should still be intentional. The README badge reads the pushed `main`
branch's `VERSION` file through Shields, so local unpushed version edits are
visible in the file itself before the badge updates.

## Runtime Diagnostics

Useful environment flags:

- `KARMA_ENGINE_STARTUP_DIAG=1`: startup-stage timing for example boot triage.
- `KARMA_ENGINE_FRAME_DIAG=1`: per-frame runtime timing.
- `KARMA_PARTICLE_STATS=1`: renderer particle-pass diagnostics.
- `KARMA_EXPLOSION_STRESS_STATS=1`: explosion stress scene perf logs.
- `KARMA_PREFAB_GALLERY_STATS=1`: prefab gallery perf logs.
- `KARMA_LIGHT_PROBE_STATS=1`: local-light probe stats.
- `KARMA_VK_VALIDATION=1` and `KARMA_DILIGENT_DEBUG=1`: Vulkan/Diligent debug
  output.
- `KARMA_VK_ADAPTER=<index>`: force a Vulkan adapter printed at startup.
- `KARMA_ALLOW_SOFTWARE_VULKAN=1`: permit software Vulkan adapters when
  hardware Vulkan is unavailable.
- `KARMA_DRAW_DEBUG=1`: print forward draw submissions before Diligent draw calls.
- `KARMA_DISABLE_DEPTH_PREPASS=1` / `KARMA_FORCE_DEPTH_PREPASS=1`: override the
  guarded forward depth-prepass policy for renderer triage.

## Known Issues

Known runtime and platform caveats are tracked in
[Known Issues](docs/KNOWN_ISSUES.md). This currently includes the Linux NVIDIA
Vulkan mouse-click present stall observed in dense grass rendering tests.

## Documentation

Start with:

- [Usage Guide](docs/ENGINE_USAGE.md)
- [Known Issues](docs/KNOWN_ISSUES.md)
- [Consumer Profiles And Versioning](docs/CONSUMER_PROFILES.md)
- [API Documentation](docs/API.md)
- [Implementation Notes](docs/ENGINE_IMPLEMENTATION.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Examples](examples/README.md)
- [Current Agent Handoff](docs/NEXT_AGENT.md)

Focused references:

- [Navigation](docs/NAVIGATION.md)
- [Jolt Physics](docs/JOLT_PHYSICS.md)
- [Particle System](docs/PARTICLE_SYSTEM.md)
- [Particle Effect Generation](docs/PARTICLE_EFFECT_GENERATION.md)
- [Effect Prefabs](docs/EFFECT_PREFABS.md)
- [Particle Beam Prefabs](docs/BEAM_PATHS.md)
- [Explosion Prefab](docs/EXPLOSION_PREFAB.md)
- [Explosion Stress Performance](docs/EXPLOSION_STRESS_PERF.md)
- [Animation V2 Architecture](docs/ANIMATION_V2.md)
- [Rigged glTF/GLB Authoring](docs/RIGGED_GLTF_AUTHORING.md)
- [Volumetric Solid Transparency](docs/VOLUMETRIC_SPHERE_TRANSPARENCY.md)
- [Debug Editor](docs/DEBUG_EDITOR.md)

## Notes For Contributors

Check `git status` before editing; the worktree is often intentionally dirty
while engine tasks are in progress. Follow [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
for module placement and dependency direction. Active continuation notes belong
in [NEXT_AGENT.md](docs/NEXT_AGENT.md); durable usage, authoring, and diagnostic
material belongs under `docs/`.
