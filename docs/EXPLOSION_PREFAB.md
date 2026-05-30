# Explosion Prefab

This is the reusable staged-explosion prefab package used by the stress sample
and prefab gallery.

Primary sources:

- [`../examples/explosion_prefab_package.h`](../examples/explosion_prefab_package.h)
- [`../examples/explosion_prefab_package.cpp`](../examples/explosion_prefab_package.cpp)
- [`../examples/assets/prefabs/explosion/prefab.kprefab`](../examples/assets/prefabs/explosion/prefab.kprefab)
- [`../examples/assets/prefabs/explosion/particles/`](../examples/assets/prefabs/explosion/particles/)
- [`../examples/assets/prefabs/explosion/source/`](../examples/assets/prefabs/explosion/source/)

## What It Provides

The package registers the prefab key `explosion` and handles the runtime setup
that a plain `.kprefab` file cannot do by itself:

- generated procedural atlases for flash / fireball / smoke / heat / rings / debris
- EXR-backed flipbook atlases for the core fire and late smoke passes
- package-scoped particle effect registration
- shared EXR cache / load helpers used by both prefab and particle examples
- typed controller helpers for trigger / update / cleanup

The explosion prefab is self-contained under
`examples/assets/prefabs/explosion/`. The `.kprefab` file references
package-scoped particle keys such as `prefabs/explosion/core_flipbook`, the
effect files live in `particles/`, and the EXR validation sequences live in
`source/`. The package registers its own `prefabs/explosion/...` texture
aliases, so explosion effects do not depend on the global particle demo alias
namespace.

The prefab itself is a layered one-shot bundle:

- flash
- fireball
- heat distortion
- core flipbook
- smoke flipbook
- embers
- shock ring
- debris
- dust ring
- smoke plume
- scorch mark
- point light

## Canonical Usage

Register once:

```cpp
#include "explosion_prefab_package.h"

if (!registerExplosionPrefabPackage(*prefab_registry)) {
  return false;
}
```

Instantiate a controller:

```cpp
const auto explosion = instantiateExplosionPrefabController(
    *world,
    *prefab_registry,
    prefabs::PrefabInstantiateDesc{
        .name = "Explosion",
        .transform = transform,
    });
```

Drive it:

```cpp
triggerExplosionPrefab(*world, *explosion, time_seconds);
updateExplosionPrefab(*world, *explosion, time_seconds);
```

Destroy it when the controller is no longer needed:

```cpp
destroyExplosionPrefabController(*world, *explosion);
```

That destroy step matters. The prefab package now has explicit teardown, and
that is the supported way to avoid stale prefab roots, emitters, and lights.

## Current Visual / Runtime State

The current package is not the original baseline package anymore.

Current state:

- the shock ring depth-tests correctly
- the smoke layers are darker than the earlier authored defaults
- core and smoke flipbooks use generated procedural atlases by default
- opt-in EXR flipbook metadata is authored for `400x400` atlas frames
- if either EXR path fails, the package falls back to a procedural atlas

Debugging helpers:

- `getExplosionPrefabPackageDebugInfo()`
- `explosionFlipbookTextureSourceName(...)`

The stress sample also logs:

- `Explosion prefab package flipbooks: core=... smoke=...`
- `Explosion stress flipbooks: core=... smoke=...`

Possible source values:

- `exr_sequence`
- `procedural_atlas`
- `unknown`

Flipbook startup controls:

- `KARMA_EXPLOSION_FLIPBOOK_SOURCE=fast` uses generated procedural atlases and is the default.
- `KARMA_EXPLOSION_FLIPBOOK_SOURCE=exr` loads or builds the EXR-derived atlases.
- `KARMA_EXPLOSION_FLIPBOOK_SOURCE=auto` uses a valid generated cache, or builds it if missing.
- `KARMA_EXPLOSION_FLIPBOOK_REBUILD=1` forces EXR cache regeneration.

## Stress-Tuned Content Note

The shared explosion assets currently carry heavier ember and debris counts
because the prefab has been used as a stress harness:

- embers: `burst_count = 504`, `max_particles = 576`
- debris: `burst_count = 36`, `max_particles = 48`

That is useful for renderer/perf validation, but it is intentionally aggressive.
If you want a gameplay-facing version, reduce those authored counts rather than
changing the controller API.

## Reference Examples

- [`../examples/explosion_stress_example.cpp`](../examples/explosion_stress_example.cpp)
- [`../examples/prefab_gallery_example.cpp`](../examples/prefab_gallery_example.cpp)

## Validation

The package is currently validated by building:

```bash
cmake --build build-local --target karma_explosion_stress_example karma_prefab_gallery_example karma_particle_example -j2
```
