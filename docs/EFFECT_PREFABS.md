# Karma Effect Prefabs

Karma now has a small file-backed ECS prefab workflow for layered runtime
effects such as the orb sample.

The core pieces are:

- `prefabs::loadEffectPrefab(...)`
- `prefabs::instantiateEffectPrefab(...)`
- `prefabs::setPrefabPlayback(...)`
- `prefabs::restartPrefab(...)`
- `prefabs::EffectPrefabSystem` (engine-owned, updates child transforms)

## What It Solves

Use an effect prefab when one gameplay effect is really a bundle of ECS
entities:

- one or more particle layers
- one or more mesh shells
- one or more lights

Instead of constructing all of that in C++, author it once in a `.kprefab`
file and instantiate it with one call.

## Typical Flow

1. Register any shared particle effect keys and texture aliases as usual.
2. Load or instantiate a `.kprefab`.
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

## Runtime Control

```cpp
prefabs::setPrefabPlayback(*world, orb->root, false);
prefabs::restartPrefab(*world, orb->root);
```

`setPrefabPlayback(...)` currently handles:

- mesh visibility
- particle enabled/playing state
- light intensity/range

## File Format

The format is line-oriented like `.kpeffect`.

Supported sections:

- `[prefab]`
- `[color name]`
- `[mesh name]`
- `[particle name]`
- `[light name]`

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

Shared transform fields on mesh/particle/light entries:

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

## Notes

- Prefabs are ECS composition, not a renderer-only feature.
- Child transforms follow the root, but particle simulation still uses the
  authored emitter settings. Root scaling is best treated as a placement tool,
  not a full resimulation control.
- The orb sample in
  [../examples/energy_orb_example.cpp](../examples/energy_orb_example.cpp)
  is the reference implementation for this workflow.
