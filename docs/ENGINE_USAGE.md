# Karma Engine — Usage Guide

## Quick Start
Build and run the default sample:

```bash
./setup.sh
cmake --build build
BZ3_DATA_DIR="$PWD/data" ./build/karma_example
```

## Build Options
Common toggles:

```bash
cmake -B build \
  -DKARMA_FETCH_DEPS=OFF
```

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

Headless build (no window or renderer backends, graphics demos disabled):

```bash
cmake -B build \
  -DKARMA_HEADLESS=ON
cmake --build build --target karma_network_demo
```

## Basic App Structure
```cpp
class MyGame : public karma::app::GameInterface {
public:
  void onStart() override { /* create entities */ }
  void onUpdate(float dt) override { /* per-frame logic */ }
  void onFixedUpdate(float dt) override { /* fixed timestep */ }
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

The engine renders your UI draw lists on top of the 3D frame.

## ECS Point Containment Queries
Karma exposes ECS-facing point containment helpers in `karma/ecs/collider_queries.h`:

```cpp
#include "karma/ecs/collider_queries.h"

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

- Query in fixed update using gameplay transforms, not render-interpolated transforms.
- Store the previous frame's overlapping entity set.
- Compare previous vs current to derive `Enter`, `Stay`, and `Exit` events in your system or gameplay code.

This keeps collider components as data and keeps trigger logic outside `ecs::World`.

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
This is distinct from `MeshComponent.mesh_key = "model.glb"`, which still uses the flat mesh loader.

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
- imported lights become `LightComponent`s on those node entities
- imported point and spot lights are created with `casts_shadows = true`
- mesh primitives become child render entities with `MeshComponent`s already attached
- imported primitive materials preserve the source asset's PBR textures and scalar factors
- the full node tree is recreated in `scene::Scene`

Imported light assumptions:

- `KHR_lights_punctual` light intensities from GLB files are normalized during import because authored glTF values are typically much larger than Karma's runtime light scale
- directional lights are currently scaled by `1/700`
- point and spot lights are currently scaled by `1/50`
- when Assimp reports the glTF default quadratic attenuation (`constant = 0`, `linear = 0`, `quadratic = 1`), Karma derives a usable local-light range from the scaled intensity instead of using a radius of `1.0`
- that derived point/spot range is currently clamped to `[4.0, 40.0]`
- imported light color is normalized and the scaled magnitude becomes `LightComponent::intensity`

Current v1 limitations:

- imported node transforms are instantiated as baked world transforms
- scene hierarchy is preserved, but parent-driven transform propagation is not implemented yet
- imported material alpha/double-sided metadata is preserved, but the runtime still does not specialize draw state per material
- GLB light scaling is currently importer-defined, not configurable per asset
- cameras, animation, and skinning are not imported yet

## Rendering Features
- Directional light with shadows (PCF supported)
- Cascaded shadow maps (CSM)
- Point and spot lights via Forward+ tiled local lights (GPU light culling per screen tile)
- Point-light shadows for `LightComponent::Type::Point` lights with `casts_shadows = true`
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
- Up to 2 shadow-casting point lights are rendered each frame (nearest to camera).
- Each selected point light renders 6 faces into a depth texture array.
- Point shadow map resolution defaults to half of `shadow_map_size` (min 256).
- Local lights use inverse-square attenuation with a smooth range cutoff.
- Local lights can optionally lift directional-shadow darkness via `local_light_directional_shadow_lift_strength`.
- Point/local-light tuning is exposed in Debug UI for bias, attenuation, AO interaction, shadow lift, and exposure.

## Data Path
Assets and configs are typically loaded from the `data/` directory.
Use `BZ3_DATA_DIR` at runtime when needed:

```bash
BZ3_DATA_DIR="$PWD/data" ./build/karma_example
```

## Demos
- `karma_example` (default scene)
- `karma_imgui_ui_demo` (ImGui draw data bridge)
- `karma_rmlui_ui_demo` (RmlUi draw data bridge)
