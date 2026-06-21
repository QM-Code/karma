# Explosion Stress Performance

This document is the practical runbook for the staged explosion stress sample in
[`../examples/particles/explosion_stress.cpp`](../examples/particles/explosion_stress.cpp).

## What This Sample Is For

The sample exists to answer two questions:

1. does explosion prefab state clean up correctly across repeated triggers?
2. which particle, lighting, and render-pass costs dominate when explosions are replayed in succession?

It is intentionally not a "pretty demo first" sample. It is a repeatable perf
probe for the direct-load explosion prefab.

## How To Run It

Build:

```bash
cmake --build build-local --target particles_explosion_stress -j2
```

Run with periodic stats:

```bash
./build-local/examples/particles/explosion_stress --stats
```

Environment equivalent:

```bash
KARMA_EXPLOSION_STRESS_STATS=1 ./build-local/examples/particles/explosion_stress
```

Renderer-level particle diagnostics:

```bash
KARMA_PARTICLE_STATS=1 ./build-local/examples/particles/explosion_stress --stats
```

`KARMA_PARTICLE_STATS=1` logs averaged final `ParticlePassStats` once per
second after particle render submission. The line is intentionally stable
`key=value` text so runs can be compared with grep, diffs, or simple scripts.

## Runtime Controls

Command-line flags:

- `--explosions N`
- `--period SECONDS`
- `--stats`
- `--disable layer0,layer1,...`

`--explosions` is clamped to the sample's supported controller range of
`1..128`. The current stress acceptance bar is a 128-controller run.

Environment variables:

- `KARMA_EXPLOSION_STRESS_STATS=1`
- `KARMA_PARTICLE_STATS=1`
- `KARMA_EXPLOSION_STRESS_DISABLE=heat,smoke`

Layer tokens:

- `flash`
- `fireball`
- `heat`
- `core_flipbook`
- `smoke_flipbook`
- `embers`
- `shock_ring`
- `debris`
- `dust_ring`
- `smoke`
- `scorch`
- `light`

Primary source:

- [`../examples/particles/explosion_stress.cpp`](../examples/particles/explosion_stress.cpp)

## Current Authored Stress Content

The stress sample currently uses deliberately heavier ember and debris bursts.

Embers:

- `burst_count = 504`
- `max_particles = 576`
- ground collision remains enabled

Source:

- [`../examples/assets/prefabs/explosion/particles/explosion_embers.kpeffect`](../examples/assets/prefabs/explosion/particles/explosion_embers.kpeffect)

Debris:

- `burst_count = 36`
- `max_particles = 48`

Source:

- [`../examples/assets/prefabs/explosion/particles/explosion_debris.kpeffect`](../examples/assets/prefabs/explosion/particles/explosion_debris.kpeffect)

## Current Visual State

The shared explosion prefab now has the following presentation/runtime state:

- the shock ring depth-tests correctly
- the main smoke layers are darker than the earlier defaults
- core and smoke flipbooks use committed prefab-local PNG atlases baked from the
  authored EXR source frames
- EXR-derived flipbook metadata targets `400x400` atlas frames
- EXR source folders remain source material, not direct runtime dependencies

Primary files:

- [`../examples/assets/prefabs/explosion/prefab.json`](../examples/assets/prefabs/explosion/prefab.json)
- [`../examples/assets/prefabs/explosion/assets.package.json`](../examples/assets/prefabs/explosion/assets.package.json)
- [`../examples/assets/prefabs/explosion/textures/`](../examples/assets/prefabs/explosion/textures/)
- [`../examples/assets/prefabs/explosion/particles/explosion_core_flipbook.kpeffect`](../examples/assets/prefabs/explosion/particles/explosion_core_flipbook.kpeffect)
- [`../examples/assets/prefabs/explosion/particles/explosion_smoke_flipbook.kpeffect`](../examples/assets/prefabs/explosion/particles/explosion_smoke_flipbook.kpeffect)
- [`../examples/assets/prefabs/explosion/particles/explosion_shock_ring.kpeffect`](../examples/assets/prefabs/explosion/particles/explosion_shock_ring.kpeffect)
- [`../examples/assets/prefabs/explosion/particles/explosion_smoke.kpeffect`](../examples/assets/prefabs/explosion/particles/explosion_smoke.kpeffect)

## Current Engine-Side Fixes

### Prefab lifecycle / cleanup

- prefab instances can now be destroyed through `prefabs::destroyPrefab(...)`
- explosion controllers have a dedicated destroy helper
- prefab particle and light members now get `VisibilityComponent`
- hidden or effectively off local lights no longer stay on the local-light path

Primary files:

- [`../include/karma/prefabs.h>0` or `gpu_fallback_active=true` on Vulkan:
  - the persistent GPU particle path is not meeting the current acceptance bar
- `core=procedural_atlas`:
  - the fire flipbook is using the generated fast/default atlas or the EXR path failed
- `smoke=procedural_atlas`:
  - the smoke flipbook is using the generated fast/default atlas or the EXR path failed

## Related Docs

The current engineering handoff and prefab authoring notes live in:

- [NEXT_AGENT.md](NEXT_AGENT.md)
- [EXPLOSION_PREFAB.md](EXPLOSION_PREFAB.md)

## Environment Note

Runtime validation can depend on the active display/driver session. If GLFW
startup fails in a headless environment, build validation is still reliable and
steady-state perf numbers should be captured on a normal desktop session.
