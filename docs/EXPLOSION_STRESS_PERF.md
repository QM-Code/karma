# Explosion Stress Performance

This document is the practical runbook for the staged explosion stress sample in
[`../examples/explosion_stress_example.cpp`](../examples/explosion_stress_example.cpp).

## What This Sample Is For

The sample exists to answer two questions:

1. does explosion prefab state clean up correctly across repeated triggers?
2. which particle, lighting, and render-pass costs dominate when explosions are replayed in succession?

It is intentionally not a "pretty demo first" sample. It is a repeatable perf
probe for the reusable explosion prefab package.

## How To Run It

Build:

```bash
cmake --build build-local --target karma_explosion_stress_example -j2
```

Run with periodic stats:

```bash
./build-local/karma_explosion_stress_example --stats
```

Environment equivalent:

```bash
KARMA_EXPLOSION_STRESS_STATS=1 ./build-local/karma_explosion_stress_example
```

## Runtime Controls

Command-line flags:

- `--explosions N`
- `--period SECONDS`
- `--stats`
- `--disable layer0,layer1,...`

Environment variables:

- `KARMA_EXPLOSION_STRESS_STATS=1`
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

- [`../examples/explosion_stress_example.cpp`](../examples/explosion_stress_example.cpp)

## Current Authored Stress Content

The stress sample currently uses deliberately heavier ember and debris bursts.

Embers:

- `burst_count = 504`
- `max_particles = 576`
- ground collision remains enabled

Source:

- [`../examples/assets/particles/explosion_embers.kpeffect`](../examples/assets/particles/explosion_embers.kpeffect)

Debris:

- `burst_count = 36`
- `max_particles = 48`

Source:

- [`../examples/assets/particles/explosion_debris.kpeffect`](../examples/assets/particles/explosion_debris.kpeffect)

## Current Visual Package State

The shared explosion prefab package now has the following presentation/runtime
state:

- the shock ring depth-tests correctly
- the main smoke layers are darker than the earlier defaults
- core and smoke flipbooks prefer EXR sequence atlases
- shared flipbook metadata now targets `400x400` atlas frames
- fire falls back to a resampled legacy sheet if the EXR sequence path fails
- smoke falls back to the procedural atlas if the smoke EXR path fails

Primary files:

- [`../examples/explosion_prefab_package.cpp`](../examples/explosion_prefab_package.cpp)
- [`../examples/explosion_prefab_package.h`](../examples/explosion_prefab_package.h)
- [`../examples/assets/particles/explosion_core_flipbook.kpeffect`](../examples/assets/particles/explosion_core_flipbook.kpeffect)
- [`../examples/assets/particles/explosion_smoke_flipbook.kpeffect`](../examples/assets/particles/explosion_smoke_flipbook.kpeffect)
- [`../examples/assets/particles/explosion_shock_ring.kpeffect`](../examples/assets/particles/explosion_shock_ring.kpeffect)
- [`../examples/assets/particles/explosion_smoke.kpeffect`](../examples/assets/particles/explosion_smoke.kpeffect)

## Current Engine-Side Fixes

### Prefab lifecycle / cleanup

- prefab instances can now be destroyed through `prefabs::destroyPrefab(...)`
- explosion controllers have a dedicated destroy helper
- prefab particle and light members now get `VisibilityComponent`
- hidden or effectively off local lights no longer stay on the local-light path

Primary files:

- [`../include/karma/prefabs/prefab.h`](../include/karma/prefabs/prefab.h)
- [`../src/prefabs/prefab_runtime.cpp`](../src/prefabs/prefab_runtime.cpp)
- [`../examples/explosion_prefab_package.cpp`](../examples/explosion_prefab_package.cpp)
- [`../src/renderer/render_system.cpp`](../src/renderer/render_system.cpp)

### Particle simulation / submission

- simulated particle emitters submit `PackedParticleBatch`
- the particle system does coarse whole-emitter visibility culling
- near-invisible alpha particles are skipped before submission
- stress logging now reports simulation, packing, visibility, particle counts, and render-pass timings

Primary files:

- [`../src/particles/particle_system.cpp`](../src/particles/particle_system.cpp)
- [`../include/karma/renderer/types.h`](../include/karma/renderer/types.h)
- [`../src/renderer/backends/diligent/passes/frame.cpp`](../src/renderer/backends/diligent/passes/frame.cpp)
- [`../examples/explosion_stress_example.cpp`](../examples/explosion_stress_example.cpp)

### Renderer refactor

- alpha particles can render through a half-resolution offscreen path
- additive, alpha, and distortion now use a prepared-stream model
- particles are uploaded once per pass, then drawn by span offset instead of rebuilding a temporary vector for every span

Primary files:

- [`../src/renderer/backends/diligent/passes/particles.cpp`](../src/renderer/backends/diligent/passes/particles.cpp)
- [`../src/renderer/backends/diligent/passes/particle_draw.cpp`](../src/renderer/backends/diligent/passes/particle_draw.cpp)

## How To Read The Log

The sample prints one `Explosion stress: ...` line when stats logging is enabled.

The startup path also prints flipbook-source lines:

- `Explosion prefab package flipbooks: core=... smoke=...`
- `Explosion stress flipbooks: core=... smoke=...`

Possible source values:

- `exr_sequence`
- `legacy_sheet`
- `procedural_atlas`
- `unknown`

Key fields:

- `world_prefabs`, `world_emitters`, `world_lights`
  - rising counts during repeated replay usually mean lifetime/cleanup problems
- `part_sys_ms(sync/sim/pack)`
  - particle effect binding, simulation, and CPU-side packing cost
- `part_render_ms(add/asort/dsort/draw)`
  - renderer-side additive prep, alpha prep, distortion prep, and draw submission
- `part_alpha_ms(collect/sort/span)`
  - alpha-path breakdown; `span` is the high-signal field for draw-fragmentation / prep cost
- `part_dist_ms(collect/sort/span)`
  - distortion-path breakdown
- `part_sys_emit(iter/vis/cull/sub)`
  - emitters visited, visible, fully culled, and actually submitted
- `part_sys_particles(sim/pack/cull/ground)`
  - live simulated particles, packed/submitted particles, particles rejected by whole-emitter cull, and particles touching ground-collision logic
- `part_batches`, `part_particles`, `part_draws`
  - additive / alpha / distortion batch, particle, and draw-call totals
- `part_sorted`
  - alpha and distortion sorted-particle totals
- `scene_copy`
  - true when a scene-color copy was needed for distortion or other scene-sampling particles
- `alpha_half_res`
  - true when the half-resolution alpha path is active

## High-Signal Interpretations

- `world_*` counts climbing while only one replay window is active:
  - stale prefab/controller state is accumulating
- `part_sys_ms(...sim...)` high and `ground` high:
  - ember/debris collision simulation is the likely bottleneck
- `part_alpha_ms(...span...)` high:
  - renderer-side alpha batching / span execution is the likely bottleneck
- `scene_copy=true` and heat is enabled:
  - the heat-distortion layer is active and still paying for scene-color sampling
- `alpha_half_res=true` with poor performance:
  - the problem is probably not raw alpha pixel cost alone
- `core=legacy_sheet`:
  - the fire flipbook EXR path failed and the package is on its fallback atlas
- `smoke=procedural_atlas`:
  - the smoke flipbook EXR path failed and the package is on its procedural fallback

## Related Handoff

The engineering summary and continuation notes for this work live in:

- [EXPLOSION_STRESS_BOOTSTRAP.md](EXPLOSION_STRESS_BOOTSTRAP.md)
- [EXPLOSION_PREFAB.md](EXPLOSION_PREFAB.md)

## Environment Note

In the headless Codex environment, windowed runtime validation still fails at
GLFW initialization. Build validation is reliable here; steady-state runtime
perf numbers should be captured on a normal desktop session.
