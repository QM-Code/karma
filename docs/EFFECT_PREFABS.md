# Karma Effect Prefabs

Karma now has a small file-backed ECS prefab workflow for layered runtime
effects such as the orb sample.

The core pieces are:

- `prefabs::loadEffectPrefab(...)`
- `prefabs::instantiateEffectPrefab(...)`
- `prefabs::EffectPrefabRegistry`
- `prefabs::instantiateRegisteredPrefab(...)`
- `prefabs::setPrefabPlayback(...)`
- `prefabs::restartPrefab(...)`
- `prefabs::EffectPrefabSystem` (engine-owned, updates child transforms)

## What It Solves

Use an effect prefab when one gameplay effect is really a bundle of ECS
entities:

- one or more particle layers
- one or more mesh shells
- one or more lights
- one or more beam-path entities

Instead of constructing all of that in C++, author it once in a `.kprefab`
file and instantiate it with one call.

## Typical Flow

1. Either instantiate a `.kprefab` directly, or register it in an `EffectPrefabRegistry`.
2. If needed, let the registry prepare package dependencies such as textures or particle keys.
3. Pass prefab parameters such as colors at instantiate time.
4. Move the root entity by changing its `TransformComponent`.
5. Toggle or restart the whole effect through the prefab helper API.

## Instantiating

```cpp
#include "karma/prefabs/effect_prefab.h"

const auto orb = prefabs::instantiateEffectPrefab(
    *world,
    graphics,
    "examples/assets/prefabs/energy_orb.kprefab",
    prefabs::EffectPrefabInstantiateDesc{
        .name = "Energy Orb",
        .transform = transform,
        .color_overrides = {{"accent", math::Color{0.18f, 1.0f, 0.28f, 1.0f}}},
    });
```

That creates:

- one root ECS entity
- one child ECS entity per prefab entry
- automatic transform syncing from root to children every frame

For a minimal end-to-end usage example, see
[../examples/laser_prefab_example.cpp](../examples/laser_prefab_example.cpp),
which sets up a simple scene and instantiates
[../examples/assets/prefabs/laser_path.kprefab](../examples/assets/prefabs/laser_path.kprefab)
directly with one call.

## Registry Packages

Use `EffectPrefabRegistry` when a prefab also needs one-time runtime setup,
for example:

- generated textures
- particle effect registrations
- material registrations

The registry owns those package callbacks and can instantiate by prefab key
instead of file path:

```cpp
prefab_registry->registerPrefab(
    "laser_path",
    prefabs::RegisteredEffectPrefabDesc{
        .prefab_path = "examples/assets/prefabs/laser_path.kprefab",
    });

const auto laser = prefab_registry->instantiate(
    *world,
    "laser_path",
    prefabs::EffectPrefabInstantiateDesc{
        .name = "Laser",
    });
```

There is also a free helper:

```cpp
const auto laser = prefabs::instantiateRegisteredPrefab(
    *world,
    *prefab_registry,
    "laser_path");
```

## Using The Orb Prefab Today

The orb is now fully packaged behind the registry layer. The reference
implementation lives in
[../examples/energy_orb_prefab_package.cpp](../examples/energy_orb_prefab_package.cpp),
which registers:

- the prefab file
- generated orb atlas textures
- the orb particle effect keys and texture aliases

Usage in gameplay code is:

```cpp
karma::demo::registerEnergyOrbPrefabPackage(*prefab_registry);

const auto orb = prefab_registry->instantiate(
    *world,
    karma::demo::kEnergyOrbPrefabKey,
    prefabs::EffectPrefabInstantiateDesc{
        .name = "Energy Orb",
        .transform = transform,
        .color_overrides = {{"accent", color}},
    });
```

The reference implementation is the orb sample in
[../examples/energy_orb_example.cpp](../examples/energy_orb_example.cpp), which:

1. registers the orb package once;
2. instantiates the prefab by key;
3. moves and controls the prefab root like any other ECS entity.

## Runtime Control

```cpp
prefabs::setPrefabPlayback(*world, orb->root, false);
prefabs::restartPrefab(*world, orb->root);
```

`setPrefabPlayback(...)` currently handles:

- mesh visibility
- particle enabled/playing state
- light intensity/range
- beam visibility

## File Format

The format is line-oriented like `.kpeffect`.

Supported sections:

- `[prefab]`
- `[color name]`
- `[mesh name]`
- `[particle name]`
- `[light name]`
- `[beam name]`

Comments start with `#`.

Example:

```ini
[prefab]
name = Energy Orb

[color accent]
default = 0.18, 1.0, 0.28, 1.0

[mesh shell]
mesh = ../shot.glb
uniform_scale = 1.3125
shadow_visible = false
material.base_color_param = accent
material.base_color_scale = 1.0, 1.0, 1.0, 0.16
material.shading_model = energy_shell
material.transparent = true
material.depth_write = false
material.double_sided = true

[particle core]
effect = energy_orb_core
override.size_scale = 0.1875
override.end_color_param = accent
override.end_color_scale = 0.85, 0.92, 0.85, 0.0

[light glow]
type = point
color_param = accent
intensity = 9.75
range = 3.0
casts_shadows = false
```

## Color Parameters

Color parameters are the first prefab constant surface.

Each color-capable field can bind to:

- a literal color
- a named prefab color parameter
- a per-channel scale
- an optional mix color + mix factor

That keeps one prefab reusable across multiple variants without introducing
effect-specific helper functions such as `createOrb(...)`.

## Supported Entry Fields

Shared transform fields on mesh/particle/light/beam entries:

- `position`
- `rotation_deg`
- `scale`
- `uniform_scale`

Mesh fields:

- `mesh`
- `visible`
- `shadow_visible`
- `material.*` for the current `MaterialDesc` surface

Particle fields:

- `effect`
- `enabled`
- `playing`
- `auto_apply`
- `preserve_enabled`
- `preserve_playing`
- `override.*` for the current `ParticleEffectOverrideComponent` surface

Light fields:

- `type`
- `color*`
- `intensity`
- `range`
- `casts_shadows`
- `inner_cone_degrees`
- `outer_cone_degrees`
- `shadow_extent`

Beam fields:

- `points`
- `core_color*`
- `glow_color*`
- `core_radius`
- `glow_radius`
- `core_intensity`
- `glow_intensity`
- `endpoint_core_size`
- `endpoint_glow_size`
- `light_count`
- `light_intensity`
- `light_range`
- `light_spacing`
- `electric_intensity`
- `electric_size`
- `electric_spacing`
- `electric_jitter_radius`
- `electric_speed`
- `distortion_intensity`
- `distortion_size`
- `distortion_spacing`
- `distortion_jitter_radius`
- `distortion_strength`
- `distortion_soft_particle_distance`
- `distortion_speed`
- `layer`
- `visible`
- `depth_test`
- `closed_loop`
- `world_space`
- `endpoint_flares`

## Notes

- Prefabs are ECS composition, not a renderer-only feature.
- Child transforms follow the root, but particle simulation still uses the
  authored emitter settings. Root scaling is best treated as a placement tool,
  not a full resimulation control.
- The orb sample in
  [../examples/energy_orb_example.cpp](../examples/energy_orb_example.cpp)
  is the reference implementation for this workflow.
