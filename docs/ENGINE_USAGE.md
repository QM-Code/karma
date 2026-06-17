# Karma Engine — Usage Guide

## Quick Start
Build and run the default sample:

```bash
./build.sh
./build/portable/karma_example
```

On Windows:

```bat
build.bat
build\portable\Release\karma_example.exe
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
./build/karma_example
```

You can use the equivalent preset for fresh checkouts:

```bash
cmake --preset portable
cmake --build --preset portable
```

For generated public API reference, see [API.md](API.md).

## Using Karma As A Dependency

Source-vendored consumers should disable examples/tests before adding Karma:

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

Installed-package consumers can use the exported CMake package:

```cmake
find_package(karma CONFIG REQUIRED)
target_link_libraries(my_game PRIVATE karma::karma)
```

`karma::server` is the minimal ECS/network profile, `karma::headless` is the
server/non-visual runtime profile, `karma::graphical` is the full graphical
profile, and `karma::karma` is the compatibility alias for `karma::graphical`.
The installed package config does not fetch missing dependency targets by
default. Set `KARMA_CONFIG_FETCH_DEPS=ON` before `find_package(karma)` to allow
package-time dependency fetching.

See [CONSUMER_PROFILES.md](CONSUMER_PROFILES.md) for the profile target
contract, package behavior, versioning policy, and validation matrix.

## Build Options
Common toggles:

```bash
cmake -B build \
  -DKARMA_FETCH_DEPS=OFF
```

Fetched Assimp builds only GLTF/GLB, OBJ, and STL importers by default through
`KARMA_ASSIMP_MINIMAL_IMPORTERS=ON`. Disable that option if you need broad
Assimp format coverage from the fetched dependency.

Optional demo:

```bash
cmake -B build \
  -DKARMA_BUILD_IMGUI_DEMO=ON
```

RmlUi demo:

```bash
cmake -B build \
  -DKARMA_BUILD_RMLUI_DEMO=ON
```

### Headless Builds

Headless builds compile the `karma::headless` profile without window,
graphical renderer, graphical UI provider, debug UI, or audio backends and
disable graphics demos:

```bash
cmake -B build-headless \
  -DKARMA_HEADLESS=ON
cmake --build build-headless --target karma_network_server_demo
```

Or use the preset:

```bash
cmake --preset headless
cmake --build --preset headless --target karma_network_server_demo
```

This profile is build-supported for non-visual programs such as network/server
targets, simulation or gameplay tests, and tools that do not need a GPU. In this
mode `platform::CreateWindow` returns `nullptr`, and `EngineApp` skips
`GraphicsDevice`, `RenderSystem`, and renderer-backed particle-system creation.
Game code and runtime modules should treat `GameInterface::graphics` and
`RuntimeModuleContext::graphics` as optional.

Headless is not a full graphical-runtime equivalent. Renderer-facing APIs remain
available to keep public types and components usable, but without a backend they
no-op or return invalid handles. Graphics examples and the rendered navmesh
example are not built.

Headless is also not a minimal dependency preset. Content import, physics,
navigation, and networking remain controlled by their own options, so a default
headless build can still build dependencies such as Assimp, Jolt,
Recast/Detour, and ENet. Disable unused subsystems explicitly where the current
backend factories support it, for example:

```bash
cmake -B build-headless \
  -DKARMA_HEADLESS=ON \
  -DKARMA_ENABLE_NAVIGATION=OFF
```

For the smallest non-visual profile, use:

```bash
cmake --preset minimal-headless
cmake --build --preset minimal-headless
```

When `KARMA_ENABLE_AUDIO=OFF`, the audio facade compiles but does not create a
backend. Calls that require loading clips still report that no backend is
available.

## Core Helpers
Karma's foundational math and timing helpers live under `karma/core`.

```cpp
#include "karma/core/math/glm.h"
#include "karma/core/math/quat.h"
#include "karma/core/math/scalar.h"
#include "karma/core/math/vec3.h"
#include "karma/core/time.h"

const karma::math::Vec3 a{1.0f, 0.0f, 0.0f};
const karma::math::Vec3 b{0.0f, 2.0f, 0.0f};

const karma::math::Vec3 sum = karma::math::add(a, b);
const karma::math::Vec3 delta = karma::math::subtract(b, a);
const karma::math::Vec3 half = karma::math::scale(sum, 0.5f);
const karma::math::Vec3 component_scaled = karma::math::multiply(a, b);
const karma::math::Vec3 midpoint = karma::math::lerp(a, b, 0.5f);

const float raw_alpha = 1.25f;
const float alpha = karma::math::clamp01(raw_alpha);

const karma::math::Quat previous_rotation{};
const karma::math::Quat current_rotation = karma::math::fromYawPitch(0.25f, 0.1f);
const karma::math::Quat blended =
    karma::math::slerp(previous_rotation, current_rotation, alpha);
const karma::math::Quat unit_rotation = karma::math::normalize(blended);

const glm::vec3 renderer_position = karma::math::toGlm(midpoint);
const karma::math::Vec3 engine_position = karma::math::fromGlm(renderer_position);

const auto start = karma::core::SteadyClock::now();
// ...
const double elapsed_ms = karma::core::elapsedMillisecondsSince(start);
```

Use these helpers instead of adding local `Vec3` arithmetic, quaternion
conversion, clamp/interpolation, scale, or elapsed-time wrappers in systems,
runtime code, and examples. GLM conversions for engine math types should go
through `karma/core/math/glm.h`; backend-specific conversions such as Bullet,
Jolt, or Assimp native types can stay local to those backend/importer files.

## Renderer Diagnostics
For Vulkan-side renderer debugging, Karma exposes two environment variables:

```bash
KARMA_VK_VALIDATION=1
KARMA_DILIGENT_DEBUG=1
```

- `KARMA_VK_VALIDATION=1` enables Diligent's Vulkan validation path and shader buffer size checks.
- `KARMA_DILIGENT_DEBUG=1` forwards Diligent debug messages to stderr without enabling Vulkan validation.

Typical crash or hang triage run:

```bash
KARMA_VK_VALIDATION=1 KARMA_DILIGENT_DEBUG=1 ./build/karma_laser_example
```

### Startup And Resource Timing

Use the Diligent glTF viewer as the renderer startup benchmark:

```bash
cmake --build build --target karma_diligent_gltf_viewer_example --parallel $(nproc)
timeout 12s env \
  KARMA_ENGINE_STARTUP_DIAG=1 \
  KARMA_RENDER_SYSTEM_DIAG=1 \
  KARMA_RENDER_RESOURCE_DIAG=1 \
  ./build/karma_diligent_gltf_viewer_example
```

Useful startup diagnostics:

- `KARMA_ENGINE_STARTUP_DIAG=1`: logs runtime subsystem creation, loading
  splash work, renderer warm-up, and Diligent startup stages.
- `KARMA_RENDER_STARTUP_DIAG=1`: enables Diligent backend startup timing without
  the broader runtime startup log.
- `KARMA_RENDER_RESOURCE_DIAG=1`: logs renderer resource creation/import timing.
  It is also enabled by `KARMA_ENGINE_STARTUP_DIAG=1`.
- `KARMA_RENDER_SYSTEM_DIAG=1`: logs `RenderSystem` extraction, binding, and
  submission stages during startup.
- `KARMA_RENDER_SYSTEM_DIAG_EVERY_FRAME=1`: keeps `RenderSystem` timing active
  after startup; use only for short profiling runs.

The current rendering startup optimization record is
[RENDERING_STARTUP_OPTIMIZATION.md](RENDERING_STARTUP_OPTIMIZATION.md).

### Vulkan Adapter Selection
On startup, the Diligent Vulkan backend enumerates compatible adapters and
chooses a hardware adapter explicitly. The default preference order is:

1. discrete GPU
2. integrated GPU
3. other non-software adapter

Software Vulkan adapters, such as Mesa Lavapipe/llvmpipe, are skipped by
default when a hardware adapter is available. This avoids failures where the
loader exposes both a hardware ICD and a CPU Vulkan ICD, but the default adapter
path enters the software driver and crashes or stalls during rendering/present.

To force a specific enumerated adapter for triage, set `KARMA_VK_ADAPTER` to the
adapter index printed at startup:

```bash
KARMA_VK_ADAPTER=0 ./build/karma_navmesh_example
```

To allow the backend to choose a software Vulkan adapter on systems without
hardware Vulkan support, opt in explicitly:

```bash
KARMA_ALLOW_SOFTWARE_VULKAN=1 ./build/karma_navmesh_example
```

For one-off host debugging outside Karma's adapter selection, you can still
force a Vulkan ICD through the loader:

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/intel_icd.json ./build/karma_navmesh_example
```

### Forward Draw Diagnostics
Set `KARMA_DRAW_DEBUG=1` to print forward-pass draw submissions immediately
before `Draw` or `DrawIndexed` reaches Diligent:

```bash
KARMA_DRAW_DEBUG=1 ./build/karma_prefab_gallery_example
```

Each line includes the pass, mesh/material IDs, vertex and index counts, first
index, instance count, and whether vertex/index buffers are bound. This is meant
for short crash triage runs; it is intentionally verbose.

The forward depth prepass is skipped by default on Vulkan drivers where it has
shown driver-specific instability. To disable it explicitly during triage:

```bash
KARMA_DISABLE_DEPTH_PREPASS=1 ./build/karma_prefab_gallery_example
```

To force the depth prepass even on guarded drivers:

```bash
KARMA_FORCE_DEPTH_PREPASS=1 ./build/karma_prefab_gallery_example
```

For frame pacing and input-latency triage, enable engine frame diagnostics:

```bash
KARMA_ENGINE_FRAME_DIAG=1 \
KARMA_ENGINE_FRAME_DIAG_THRESHOLD_MS=18 \
./build/karma_navmesh_example
```

The frame log splits event handling into poll, UI dispatch, input update, event
clearing, and close checks. It also reports input event counts for mouse button,
mouse move, focus, and resize events. Use the `fb` bucket to spot framebuffer
size/query stalls and `end_frame` to spot renderer present or acquire stalls.

## Present Mode Policy
Karma defaults to the low-latency present path for the Diligent Vulkan backend.
The default `EngineConfig::vsync` value is `false`, which calls Diligent
`Present(0)`. Diligent then chooses the first available mode in this order:

1. `VK_PRESENT_MODE_MAILBOX_KHR`
2. `VK_PRESENT_MODE_IMMEDIATE_KHR`
3. `VK_PRESENT_MODE_FIFO_KHR`

This avoids FIFO/FIFO_RELAXED stalls observed on some Linux Vulkan surfaces, where
`vkQueuePresentKHR` can block for multiple refresh intervals after input. If
mailbox is not supported, the backend may use immediate mode; that removes the
stall at the cost of possible tearing unless the app caps frame rate elsewhere.

Set `config.vsync = true` or use `KARMA_ENGINE_VSYNC=1` to opt into
FIFO/FIFO_RELAXED vblank pacing:

```bash
KARMA_ENGINE_VSYNC=1 ./build/karma_navmesh_example
```

Use this when tear-free pacing is more important than input latency, or when the
target driver/compositor does not exhibit FIFO present stalls.

## Post-Process Profiles
Cameras select post-processing by name through
`CameraComponent::post_process_profile_key`. An empty key uses the engine
default profile. Missing named profiles also fall back to the default profile so
camera authoring errors do not stop rendering.

`EngineConfig::post_process` seeds the startup default profile. Games can
register named profiles during `onStart()` or update them at runtime through the
borrowed `post_process_profiles` registry:

```cpp
constexpr const char* kCinematicProfile = "camera/cinematic";

renderer::PostProcessSettings cinematic{};
cinematic.bloom_enabled = true;
cinematic.bloom_threshold = 0.7f;
cinematic.bloom_intensity = 0.35f;
cinematic.tone_mapping_enabled = true;
cinematic.tone_exposure = 1.1f;
post_process_profiles->registerProfile(kCinematicProfile, cinematic);

world->add(camera_entity, components::CameraComponent{
    .is_primary = true,
    .post_process_profile_key = kCinematicProfile,
});
```

Offscreen render-target cameras resolve their own profile in the same way as the
primary camera. Cameras only select profile intent; Diligent-owned post-process
passes, render targets, bloom mip textures, and history resources stay inside
the renderer backend.

`RenderSystem` resolves the active `PostProcessSettings` immediately before
each camera pass and passes those settings to `GraphicsDevice::renderLayer`.
There is no global `GraphicsDevice::setPostProcessSettings` or backend-wide
post-process state. Custom render paths that bypass `RenderSystem` must pass
the resolved settings explicitly for each layer/target render.

The Diligent backend loads built-in post-process HLSL assets from
`src/rendering/renderer/backends/diligent/shaders/post_process/` in source-tree
builds and installs them under `share/karma/shaders/diligent/post_process/`.
`KARMA_DILIGENT_SHADER_DIR` can override the shader directory for local
experiments. A minimal embedded fallback is used only if a required shader file
cannot be found.

Current Diligent pass layout:

- shared fullscreen triangle vertex shader
- bloom prefilter, downsample, and upsample/combine passes over a mip chain
- final composite/tone/color pass
- temporal resolve/history pass
- SSAO, SSR, and DOF controls are still evaluated in the composite path until
  the renderer grows dedicated G-buffer and motion-vector inputs

## Loading Splash
Windowed apps show a lightweight engine-owned loading splash by default:

```cpp
karma::app::EngineConfig config;
config.loading_splash.enabled = true;
config.loading_splash.image_path = "docs/logo.png";
config.loading_splash.target_fps = 30;
config.loading_splash.accent = {0.24f, 0.56f, 1.0f, 1.0f};
```

When enabled, `EngineApp` presents a simple provider-neutral UI frame as soon as
the window and renderer are available. Set `image_path` to a PNG asset, such as
the repo logo at `docs/logo.png`.

Splash-enabled apps run `GameInterface::onStart()` on a worker thread while the
main thread keeps presenting the splash at `target_fps`. Renderer facade calls
are serialized so startup resource creation cannot enter the backend at the same
time as splash rendering.

Local-light / point-shadow sanity check:

```bash
./build/karma_light_stress_example
./build/karma_light_stress_example --lights 16 --stats
```

For current renderer/sample implementation notes, see:

- [NEXT_AGENT.md](NEXT_AGENT.md)
- [VOLUMETRIC_SPHERE_TRANSPARENCY.md](VOLUMETRIC_SPHERE_TRANSPARENCY.md)
- [BEAM_PATHS.md](BEAM_PATHS.md)
- [EFFECT_PREFABS.md](EFFECT_PREFABS.md)

## Basic App Structure
```cpp
class MyGame : public karma::app::GameInterface {
public:
  void onStart() override { /* create entities */ }
  void onUpdate(float dt) override { /* per-frame logic */ }
  void onFixedUpdate(float dt) override { /* fixed timestep */ }
  void onPostFixedUpdate(float dt) override { /* post-physics fixed step */ }
};

class MyUi : public karma::app::UiLayer {
 public:
  void onFrame(karma::app::UIContext& ctx) override { /* fill ctx.drawData() */ }
};

int main() {
  karma::app::EngineApp engine;
  MyGame game;

  // Optional UI layer
  auto ui = std::make_unique<MyUi>();
  engine.setUi(std::move(ui));

  karma::app::EngineConfig config;
  config.window.title = "My Game";
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_bias = 0.0006f;
  config.shadow_pcf_radius = 1;
  config.shadow_raster_depth_bias = 0;
  config.shadow_raster_slope_bias = 0.0f;
  config.shadow_receiver_bias_scale = 0.75f;
  config.shadow_normal_bias_scale = 1.0f;
  config.point_shadow_max_lights = 4;
  config.point_shadow_constant_bias = 0.0012f;
  config.point_shadow_slope_bias_scale = 2.0f;
  config.point_shadow_normal_bias_scale = 1.5f;
  config.point_shadow_receiver_bias_scale = 0.35f;
  config.local_light_distance_damping = 0.08f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.85f;
  config.lighting_exposure = 1.1f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
}
```

## UI draw data
Karma's UI integration is backend-agnostic. You provide a `UiLayer` implementation
that owns its UI system and submits draw data into `UIContext` each frame:

- `onEvent(...)` for input
- `onFrame(...)` for timing + draw list submission
- `UIContext::createTextureRGBA8(...)` for UI textures
- `UIContext::loadTextureRGBA8FromPng(...)` for PNG UI textures owned by the
  UI context and destroyed automatically when the UI context shuts down

The engine renders your UI draw lists on top of the 3D frame.

For ImGui, Karma provides the adapter layer and you provide only the ImGui
content:

```cpp
#include "karma/features/ui/imgui/imgui_layer.h"
#include <imgui.h>

struct ToolsUi {
  karma::app::UITexture logo;

  void draw(karma::app::UIContext& ctx) {
    if (!logo) {
      logo = ctx.loadTextureRGBA8FromPng("assets/logo.png");
    }

    ImGui::Begin("Tools");
    ImGui::TextUnformatted("Hello from ImGui");
    if (logo) {
      ImGui::Image(karma::imgui::toTextureId(logo.handle),
                   ImVec2(static_cast<float>(logo.width),
                          static_cast<float>(logo.height)));
    }
    ImGui::End();
  }
};

auto tools = std::make_shared<ToolsUi>();
engine.setUi(karma::imgui::createUiLayer([tools](karma::app::UIContext& ctx) {
  tools->draw(ctx);
}));
```

Provider adapters live under `karma/features/ui/<provider>`. Keep code that
talks to ImGui, RmlUi, or another UI library in that adapter; keep engine
composition on the generic `UiLayer` contract.

RmlUi follows the same pattern. Karma owns the RmlUi render/system/file bridge,
and app code only initializes documents when the RmlUi context is ready:

```cpp
#include "karma/features/ui/rmlui/rmlui_layer.h"
#include <RmlUi/Core.h>

engine.setUi(karma::rmlui::createUiLayer([](Rml::Context& context) {
  Rml::ElementDocument* document =
      context.LoadDocumentFromMemory("<rml><body><div>Hello</div></body></rml>");
  if (document) {
    document->Show();
  }
}));
```

## Particle Effects

Particle effects are ECS-driven through `ParticleLibrary`,
`ParticleEffectComponent`, `ParticleEffectOverrideComponent`, and
`ParticleSystem`.

For the intended registration/binding/restart workflow, see
[PARTICLE_SYSTEM.md](PARTICLE_SYSTEM.md).

## Prefabs

Layered gameplay objects that need multiple ECS entities can be authored as
JSON prefabs. A prefab stores one root entity, its `scene::Scene` children, and
component payloads keyed by real component type names such as
`TransformComponent`, `MeshComponent`, and `ParticleEffectComponent`.

Instantiate directly with `prefabs::instantiatePrefab(world, scene, path, desc)`.
If the path is a directory, Karma loads `prefab.json` from that directory. When
the directory also contains `prefab.resources.json`, Karma loads prefab-local
texture aliases and particle effect registrations before creating entities.

Runtime-only renderer and particle IDs are not persisted. Prefabs save stable
keys such as mesh, material, texture, and particle effect keys; the runtime
systems resolve those keys after load.

For the file format and runtime helper API, see [EFFECT_PREFABS.md](EFFECT_PREFABS.md).

## Collision Events

For gameplay-facing `enter/stay/exit` overlap events, the intended workflow is:

- add `CollisionListenerComponent` to the entity you want to monitor
- add `CollisionEventsComponent` to receive the event buffers
- read the results in `onPostFixedUpdate(...)`, which runs after fixed-step physics and collision systems

Example:

```cpp
#include "karma/karma.h"

class MyGame : public karma::app::GameInterface {
 public:
  void onStart() override {
    sensor_ = world->createEntity();
    world->add(sensor_, karma::components::TransformComponent{});
    world->add(sensor_, karma::components::SphereColliderComponent{
        .center = {},
        .radius = 2.0f});
    world->add(sensor_, karma::components::CollisionListenerComponent{
        .enabled = true,
        .mode = karma::components::CollisionListenMode::TriggersOnly,
        .emit_stay = true});
    world->add(sensor_, karma::components::CollisionEventsComponent{});
  }

  void onPostFixedUpdate(float /*dt*/) override {
    const auto& events = world->get<karma::components::CollisionEventsComponent>(sensor_);

    for (const auto& hit : events.entered) {
      // just entered this overlap
    }
    for (const auto& hit : events.stayed) {
      // still overlapping this fixed tick
    }
    for (const auto& hit : events.exited) {
      // left overlap this fixed tick
    }
    for (const auto& hit : events.active) {
      // currently overlapping after this fixed-step update
    }
  }

 private:
  karma::ecs::Entity sensor_{};
};
```

Notes:

- `entered`, `stayed`, and `exited` are transient per-fixed-tick buffers.
- `active` is the current overlap set for that tick.
- The engine-owned `CollisionEventSystem` currently derives these from the ECS collider query path, so it supports `Box`, `Sphere`, and `Capsule` listener entities.
- For actual contact normals/impulses from the physics backend, that should be a later contact-event path, not this overlap system.

## Physical Contact Events

For solid-body contacts with point and normal data, use:

- `ContactListenerComponent`
- `ContactEventsComponent`

This path is physics-driven and separate from trigger / overlap events.

Each `ContactEvent` currently includes:

- `other`
- `other_shape`
- `point`
- `normal`

Example:

```cpp
class MyGame : public karma::app::GameInterface {
 public:
  void onStart() override {
    actor_ = world->createEntity();
    world->add(actor_, karma::components::TransformComponent{});
    world->add(actor_, karma::components::BoxColliderComponent{
        .half_extents = {0.5f, 0.5f, 0.5f}});
    world->add(actor_, karma::components::RigidbodyComponent{});
    world->add(actor_, karma::components::ContactListenerComponent{
        .enabled = true,
        .emit_stay = true});
    world->add(actor_, karma::components::ContactEventsComponent{});
  }

  void onPostFixedUpdate(float /*dt*/) override {
    const auto& contacts = world->get<karma::components::ContactEventsComponent>(actor_);
    for (const auto& hit : contacts.entered) {
      // hit.point and hit.normal are valid for this contact
    }
  }

 private:
  karma::ecs::Entity actor_{};
};
```

Notes:

- This is intended for solid physical contacts, not trigger zones.
- `normal` is reported in the listener entity's frame of reference, i.e. it points away from the contacted surface and into the listener.
- The current runtime supports `RigidbodyComponent` bodies, and on the default Jolt backend the built-in player controller path as used in `collision_events_example.cpp`.

## Ground Contact

For the common gameplay question "am I touching the floor?", use
`GroundContactComponent` on an entity that is already driven by physics.

The component is opt-in output state:

- `grounded` is the current grounded state after this fixed physics tick
- `entered` is true on the tick the entity becomes grounded
- `exited` is true on the tick the entity leaves the ground
- `support_entity` is the current support body when it can be resolved
- `point` is the support point when available
- `normal` is the support normal when available

Example:

```cpp
class MyGame : public karma::app::GameInterface {
 public:
  void onStart() override {
    player_ = world->createEntity();
    world->add(player_, karma::components::TransformComponent{});
    world->add(player_, karma::components::BoxColliderComponent{
        .half_extents = {0.5f, 1.0f, 0.5f}});
    world->add(player_, karma::components::PlayerControllerComponent{});
    world->add(player_, karma::components::GroundContactComponent{});
  }

  void onPostFixedUpdate(float /*dt*/) override {
    const auto& ground = world->get<karma::components::GroundContactComponent>(player_);
    if (ground.entered) {
      // just landed
    }
    if (ground.exited) {
      // just left the floor
    }
    if (ground.grounded) {
      // standing on a support surface this tick
    }
  }

 private:
  karma::ecs::Entity player_{};
};
```

Current support:

- `PlayerControllerComponent`
- `RigidbodyComponent` with `BoxColliderComponent`

For box rigid bodies, the engine now also runs a short downward support probe after physics so `support_entity`, `point`, and `normal` can be resolved for common "what am I standing on?" gameplay logic.

## ECS Point Containment Queries
Karma exposes ECS-facing point containment helpers in `karma/world/ecs/collider_queries.h`:

```cpp
#include "karma/world/ecs/collider_queries.h"

using karma::ecs::queries::PointContainmentFilter;

const karma::math::Vec3 point{0.0f, 1.0f, 0.0f};

if (karma::ecs::queries::containsPoint(*world, trigger_entity, point)) {
  // This point is inside at least one supported collider on trigger_entity.
}

auto hit = karma::ecs::queries::findContainingCollider(
    *world,
    point,
    PointContainmentFilter{
        .only_triggers = true,
        .collision_layer_mask = 0xFFFFFFFFu});

auto hits = karma::ecs::queries::findContainingColliders(
    *world,
    point,
    PointContainmentFilter{.only_triggers = true});
```

Current support:

- `BoxColliderComponent`
- `SphereColliderComponent`
- `CapsuleColliderComponent`

Overlap queries:

```cpp
auto hit = karma::ecs::queries::findOverlappingCollider(
    *world,
    sensor_entity,
    karma::ecs::queries::OverlapFilter{
        .only_triggers = false,
        .collision_layer_mask = 0xFFFFFFFFu,
        .skip_self = true});

auto hits = karma::ecs::queries::findOverlappingColliders(
    *world,
    sensor_entity,
    karma::ecs::queries::OverlapFilter{
        .only_triggers = false,
        .collision_layer_mask = 0xFFFFFFFFu,
        .skip_self = true});
```

Overlap query support:

- `Sphere` vs `Sphere`
- `Sphere` vs `Box`
- `Sphere` vs `Capsule`
- `Box` vs `Box`
- `Box` vs `Capsule`
- `Capsule` vs `Capsule`

Assumption:

- Use one logical collider component per entity. Overlap queries are designed around that model.

Current limitation:

- `MeshColliderComponent` is not included in point containment or overlap queries yet. Point-inside-mesh semantics are only sensible for closed volume meshes, and the current ECS query path intentionally leaves that undefined.

Trigger pattern:

- Prefer `CollisionListenerComponent` + `CollisionEventsComponent` for normal `enter/stay/exit` gameplay.
- Use the raw collider query helpers when you need one-off tests, custom filtering, or ad hoc spatial logic outside the engine-owned collision event path.

## Runtime Materials
`GameInterface` now exposes a runtime material library through the `materials` pointer.
The intended workflow is:

- register a material variant from game code, usually in `onStart()`
- assign its key to `MeshComponent.material_key`
- let the renderer clone the mesh's imported material set and apply the override

Tinting a GLB while preserving its existing textures and PBR data:

```cpp
#include "karma/karma.h"

class MyGame : public karma::app::GameInterface {
 public:
  void onStart() override {
    const std::string tank_mesh = "assets/tank_final.glb";

    materials->registerFromMeshTint(
        "tank_blue",
        tank_mesh,
        karma::math::Color{0.35f, 0.55f, 1.0f, 1.0f});

    auto tank = world->createEntity();
    world->add(tank, karma::components::TransformComponent{});
    world->add(tank, karma::components::MeshComponent{
        .mesh_key = tank_mesh,
        .material_key = "tank_blue",
        .visible = true});
  }
};
```

The lower-level form uses `MaterialResourceDesc` directly:

```cpp
materials->registerMaterial(
    "tank_blue",
    karma::renderer::MaterialResourceDesc::fromMeshTint(
        tank_mesh,
        karma::math::Color{0.35f, 0.55f, 1.0f, 1.0f}));
```

Current semantics:

- empty `material_key`: use the mesh's original imported materials
- registered tint material: clone the mesh's imported material set and multiply each base color by the tint
- unknown key: log once and fall back to the mesh's original imported materials
- mismatched mesh/material pairing: log once and fall back to the mesh's original imported materials

Current scope:

- runtime registration from game code is supported now
- descriptors are shaped so `.mat` file loading can be added later without changing `MeshComponent.material_key`
- overrides are whole-mesh material-set variants, not per-submesh authoring APIs

Current limitation:

- the tint workflow assumes one logical mesh asset per `MeshComponent`
- it preserves imported submesh materials, but does not yet expose per-submesh override selection to gameplay code

## GLB Scene Import
Karma now has a separate GLB scene-import path for authored scenes.
This is distinct from `MeshComponent.mesh_key = "model.glb"`, which still uses the flat mesh import path.

Use the convenience import directly:

```cpp
auto imported = karma::scene::importGlbScene(
    *world,
    *scene,
    *graphics,
    "assets/level.glb",
    karma::scene::GlbSceneImportOptions{
        .load = {
            .import_meshes = true,
            .import_lights = true,
        },
        .instantiate = {
            .create_synthetic_root = true,
        }});

if (imported.valid()) {
  // imported.root_entity is the top-level handle for this imported scene.
}
```

You can also split loading and instantiation:

```cpp
const auto prefab = karma::scene::loadGlbScenePrefab("assets/level.glb");
auto imported = karma::scene::instantiateGlbScenePrefab(
    *world,
    *scene,
    *graphics,
    prefab,
    {.create_synthetic_root = true});
```

Current behavior:

- one structural ECS entity is created for each imported GLB node
- imported local transforms are preserved and `scene::updateWorldTransforms(...)`
  composes final world transforms
- imported lights become `LightComponent`s on those node entities
- imported point and spot lights are created with `casts_shadows = true`
- mesh primitives become child render entities with key-only `MeshComponent`s backed by renderer-owned runtime mesh registrations
- imported primitive materials preserve the source asset's PBR textures and scalar factors through registered material keys
- animation clips import transform and morph-weight channels
- skinned primitives import joint indices, weights, skins, skeletons, and inverse
  bind matrices
- imported animation roots get an `AnimatorComponent` and can autoplay clip `0`
- GPU skinning is the default for imported skinned meshes; CPU skinning remains
  the fallback/reference path
- morph target deltas and default weights import from GLB mesh primitives and
  morph-weight animation updates runtime morph components
- the full node tree is recreated in `scene::Scene`

Animation runtime flow:

- `AnimationSystem` samples imported clips on the root `AnimatorComponent`
- transform channels write `LocalTransformComponent` values on imported node
  entities
- morph-weight channels write `MorphTargetComponent::weights` on the matching
  renderable primitive entities
- `scene::updateWorldTransforms(...)` composes final world transforms after
  animation sampling
- `CpuSkinningSystem` is the mesh deformation upload stage: it applies morph
  targets on CPU, builds skinning palettes, leaves GPU-skinned meshes in bind or
  morphed-bind pose, and uploads CPU-skinned fallback meshes when needed
- `RenderSystem` submits visible meshes and GPU skinning palettes to the
  renderer

Imported light assumptions:

- `KHR_lights_punctual` light intensities from GLB files are normalized during import because authored glTF values are typically much larger than Karma's runtime light scale
- directional lights are currently scaled by `1/700`
- point and spot lights are currently scaled by `1/50`
- when Assimp reports the glTF default quadratic attenuation (`constant = 0`, `linear = 0`, `quadratic = 1`), Karma derives a usable local-light range from the scaled intensity instead of using a radius of `1.0`
- that derived point/spot range is currently clamped to `[4.0, 40.0]`
- imported light color is normalized and the scaled magnitude becomes `LightComponent::intensity`

Current v1 limitations:

- imported material alpha/double-sided metadata is preserved, but the runtime still does not specialize draw state per material
- GLB light scaling is currently importer-defined, not configurable per asset
- cameras are not imported yet
- retargeting clips between different skeletons is not implemented
- glTF sparse accessors and external `.bin` buffers are not imported by the
  explicit GLB metadata reader
- morph target deformation is currently CPU-applied before GPU skinning; a pure
  GPU morph path is still future renderer work

## Rendering Features
- Directional light with shadows (PCF supported)
- Cascaded shadow maps (CSM)
- Point and spot lights via Forward+ tiled local lights (GPU light culling per screen tile)
- Point-light shadows for `LightComponent::Type::Point` lights with `casts_shadows = true`
- Camera-selected post-process profiles with Diligent bloom, tone/color,
  SSAO, SSR, TAA, and DOF controls
- Optional anisotropy + mip generation

## Shadow Defaults
Recommended baseline values for the current directional-shadow pipeline:

- `shadow_map_size = 2048`
- `shadow_pcf_radius = 1`
- `shadow_bias = 0.0006f`
- `shadow_receiver_bias_scale = 0.75f`
- `shadow_normal_bias_scale = 1.0f`
- `shadow_raster_depth_bias = 0`
- `shadow_raster_slope_bias = 0.0f`

Current CSM setup:
- 4 cascades (texture-array based)
- Stabilized light-space snapping to reduce shimmer
- Cascade transition blending near split boundaries

Point-light shadow setup:
- `point_shadow_max_lights` controls the runtime budget for shadow-casting point lights.
- The current renderer supports up to 16 shadow-casting point lights at compile time.
- The default engine config budget remains conservative at `2`.
- Each selected point light renders 6 faces into a depth texture array.
- Point shadow map resolution defaults to half of `shadow_map_size` (min 256).
- Local lights use inverse-square attenuation with a smooth range cutoff.
- Local lights can optionally lift directional-shadow darkness via `local_light_directional_shadow_lift_strength`.
- Point/local-light tuning is exposed in Debug UI for bias, attenuation, AO interaction, shadow lift, and exposure.

Reference sample:

- [../examples/main.cpp](../examples/main.cpp) is the tank/world movement
  sample. It uses a shadow-casting directional sun with intensity `1.6`, plus
  local point lights and a radar render-target camera.
- [../examples/light_stress_example.cpp](../examples/light_stress_example.cpp) provides the current local-light probe workflow for `1-16` safe-mode shadowed point lights.
- [NEXT_AGENT.md](NEXT_AGENT.md) carries active renderer/local-light handoff notes.

## Data Path
Assets and configs are typically loaded from the `data/` directory.
Use `KARMA_DATA_DIR` at runtime when needed:

```bash
KARMA_DATA_DIR="$PWD/data" ./build/karma_example
```

## Demos
- `karma_example` (default scene)
- `karma_imgui_ui_demo` (ImGui draw data bridge)
- `karma_rmlui_ui_demo` (RmlUi draw data bridge)
