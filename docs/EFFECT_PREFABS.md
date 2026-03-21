# Karma Prefabs

Karma prefabs are file-backed ECS subtrees. A prefab instantiates one root
entity plus any number of child entities under it, and the engine keeps the
children synced to the root transform automatically.

The current prefab runtime supports these authored entry types:

- `mesh`
- `particle`
- `light`
- `beam`
- `volume_sphere`

The core pieces are:

- `prefabs::loadPrefab(...)`
- `prefabs::instantiatePrefab(...)`
- `prefabs::PrefabRegistry`
- `prefabs::setPrefabPlayback(...)`
- `prefabs::restartPrefab(...)`
- `prefabs::EffectPrefabSystem` (engine-owned child transform syncing)

## What It Solves

Use a prefab when one gameplay object is really a bundle of ECS entities:

- a mesh shell plus one or more lights
- a layered particle effect
- a reusable beam setup
- an analytic volume sphere with helper lights

Instead of building that subtree in C++, author it once in a prefab manifest
and instantiate it with one call.

## Canonical Workflow

1. Author `prefab.kprefab` in a directory, or author a standalone `.kprefab` file.
2. Instantiate it directly from a file or directory path.
3. Optionally pass typed parameter overrides at instantiate time.
4. Move, scale, or destroy the prefab by operating on its root entity.
5. If the prefab also needs one-time runtime setup, register it in a `PrefabRegistry`.

## Direct Instantiation

The canonical direct API is:

```cpp
#include "karma/prefabs/effect_prefab.h"

const auto instance = prefabs::instantiatePrefab(
    *world,
    graphics,
    "examples/assets/prefabs/volumetric_sphere",
    prefabs::PrefabInstantiateDesc{
        .name = "Shield",
        .transform = transform,
        .param_overrides = {
            {"color", math::Color{0.18f, 0.82f, 1.0f, 1.0f}},
            {"radius", 4.2f},
            {"opacity", 0.5f},
        },
    });
```

That creates:

- one root ECS entity
- one child ECS entity per prefab entry
- automatic root-to-child transform syncing every frame

If the path is a directory, Karma loads `prefab.kprefab` from that directory.

For a minimal end-to-end example, see
[../examples/volumetric_sphere_example.cpp](../examples/volumetric_sphere_example.cpp),
which instantiates
[../examples/assets/prefabs/volumetric_sphere/prefab.kprefab](../examples/assets/prefabs/volumetric_sphere/prefab.kprefab)
by passing the prefab directory path directly.

## Registry Packages

Use `PrefabRegistry` when a prefab also needs one-time runtime setup, for
example:

- generated textures
- particle effect registrations
- material registrations

The registry owns those prepare/cleanup callbacks and can instantiate by key
instead of file path:

```cpp
prefab_registry->registerPrefab(
    "energy_orb",
    prefabs::RegisteredPrefabDesc{
        .prefab_path = "examples/assets/prefabs/energy_orb",
        .prepare = prepare_callback,
        .cleanup = cleanup_callback,
    });

const auto orb = prefab_registry->instantiate(
    *world,
    "energy_orb",
    prefabs::PrefabInstantiateDesc{
        .name = "Orb",
        .transform = transform,
        .param_overrides = {
            {"accent", math::Color{0.18f, 1.0f, 0.28f, 1.0f}},
        },
    });
```

The orb sample is the reference implementation for that path:
[../examples/energy_orb_example.cpp](../examples/energy_orb_example.cpp).

## Runtime Control

```cpp
prefabs::setPrefabPlayback(*world, instance->root, false);
prefabs::restartPrefab(*world, instance->root);
```

`setPrefabPlayback(...)` currently handles:

- mesh visibility
- particle enabled/playing state
- light intensity/range
- beam visibility
- volume sphere visibility

## File Format

The format is line-oriented like `.kpeffect`.

Supported sections:

- `[prefab]`
- `[param name]`
- `[mesh name]`
- `[particle name]`
- `[light name]`
- `[beam name]`
- `[volume_sphere name]`

Comments start with `#`.

Example:

```ini
[prefab]
name = Volumetric Sphere

[param color]
type = color
default = 0.18, 0.82, 1.0, 1.0

[param radius]
type = float
default = 4.2

[volume_sphere body]
color_param = color
radius_param = radius
center_opacity = 0.5
distortion_strength = 1.4

[light glow]
type = point
color_param = color
intensity = 180.0
range = 52.0
casts_shadows = false
```

## Parameters

Prefab parameters are typed. Supported parameter types are:

- `bool`
- `float`
- `vec3`
- `color`
- `string`

Fields bind to parameters through the usual `*_param` pattern. Color bindings
also support `*_scale`, `*_mix`, and `*_mix_factor`. Float bindings support
`*_scale` and `*_bias`.

Examples:

- `material.base_color_param = accent`
- `range_param = light_range`
- `radius_param = radius`

`color_overrides` still work as a convenience bridge, but the forward-looking
surface is `param_overrides`.

## Supported Entry Fields

Shared transform fields on `mesh`, `particle`, `light`, `beam`, and
`volume_sphere` entries:

- `position`
- `rotation_deg`
- `scale`
- `uniform_scale`

Mesh fields:

- `mesh`
- `visible`
- `shadow_visible`
- `material.*`

Particle fields:

- `effect`
- `enabled`
- `playing`
- `auto_apply`
- `preserve_enabled`
- `preserve_playing`
- `override.*`

Light fields:

- `type`
- `color*`
- `intensity*`
- `range*`
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
- `electric_*`
- `distortion_*`
- `layer`
- `visible`
- `depth_test`
- `closed_loop`
- `world_space`
- `endpoint_flares`

Volume sphere fields:

- `color*`
- `emissive_color*`
- `radius*`
- `center_opacity*`
- `distortion_strength*`
- `noise_strength*`
- `overlay_depth*`
- `visible`
- `scale_with_transform`

`*` means the field accepts the typed binding form, such as `*_param`,
`*_scale`, or `*_bias`.
