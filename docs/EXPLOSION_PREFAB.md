# Explosion Prefab

The explosion is a direct-load prefab under
`examples/assets/prefabs/explosion/`.

Primary sources:

- [`../examples/assets/prefabs/explosion/prefab.json`](../examples/assets/prefabs/explosion/prefab.json)
- [`../examples/assets/prefabs/explosion/prefab.resources.json`](../examples/assets/prefabs/explosion/prefab.resources.json)
- [`../examples/assets/prefabs/explosion/particles/`](../examples/assets/prefabs/explosion/particles/)
- [`../examples/assets/prefabs/explosion/textures/`](../examples/assets/prefabs/explosion/textures/)

## What It Provides

`prefab.resources.json` registers the prefab-owned texture aliases and particle
effect files the first time the prefab directory is instantiated. The sidecar
uses paths relative to the prefab directory.

`prefab.json` owns the entity hierarchy, particle effect bindings, child
emitter `start_delay` values, and the point-light pulse data. No explosion
package or explosion-specific controller is required.

The prefab is a layered one-shot bundle:

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
- point-light pulse

## Canonical Usage

Instantiate directly:

```cpp
const auto explosion = prefabs::instantiatePrefab(
    *world,
    *scene,
    resolveExampleAssetPath("prefabs/explosion"),
    prefabs::PrefabInstantiateDesc{
        .root_transform = transform,
        .name_override = "Explosion",
    });
```

For repeated explosions, instantiate a fresh prefab on each trigger and destroy
the root after the effect window:

```cpp
prefabs::destroyPrefab(*world, *scene, explosion->root);
```

The runtime `EngineApp` binds the prefab resource context so direct loads can
upload sidecar textures and register sidecar particle effect files.

## Runtime Timing

Particle staging is generic:

- `ParticleEmitterComponent::start_delay` delays one-shot emission.
- `ParticleEffectComponent::preserve_start_delay` keeps prefab-authored delays
  when a named `.kpeffect` template is applied.
- `LightPulseComponent` fades point-light intensity/range and hides the light
  after its duration.

The old controller timings are now serialized in `prefab.json`:

- immediate: flash
- `0.01s`: core flipbook
- `0.03s`: fireball
- `0.04s`: heat, shock ring
- `0.05s`: embers, debris, dust ring
- `0.12s`: scorch
- `0.22s`: smoke flipbook
- `0.24s`: smoke
- `0.64s`: light pulse duration

## Assets

Committed PNG atlases live under `textures/`. Core and smoke flipbooks use the
fast procedural visual defaults baked into PNG assets. EXR source folders may
remain as reference material, but they are no longer runtime dependencies for
the explosion prefab.

## Reference Examples

- [`../examples/explosion_stress_example.cpp`](../examples/explosion_stress_example.cpp)
- [`../examples/prefab_gallery_example.cpp`](../examples/prefab_gallery_example.cpp)

## Validation

Current validation targets:

```bash
cmake --build build --target karma_prefab_tests karma_prefab_gallery_example karma_explosion_stress_example karma_particle_example -j2
./build/karma_prefab_tests
ctest --test-dir build --output-on-failure -R 'karma_prefab_tests|karma_animation_tests'
```
