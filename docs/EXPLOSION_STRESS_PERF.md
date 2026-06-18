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

- [`../include/karma/content/prefabs/prefab.h`](../include/karma/content/prefabs/prefab.h)
- [`../src/content/prefabs/prefab_runtime.cpp`](../src/content/prefabs/prefab_runtime.cpp)
- [`../src/content/prefabs/prefab_resources.cpp`](../src/content/prefabs/prefab_resources.cpp)
- [`../src/rendering/renderer/render_system.cpp`](../src/rendering/renderer/render_system.cpp)

### Particle simulation / submission

- simulated particle emitters submit `PackedParticleBatch`
- the particle system does coarse whole-emitter visibility culling
- near-invisible alpha particles are skipped before submission
- stress logging now reports simulation, packing, visibility, particle counts, and render-pass timings

Primary files:

- [`../src/features/visual/particles/particle_system.cpp`](../src/features/visual/particles/particle_system.cpp)
- [`../include/karma/rendering/renderer/particles.h`](../include/karma/rendering/renderer/particles.h)
- [`../src/rendering/renderer/backends/diligent/passes/frame.cpp`](../src/rendering/renderer/backends/diligent/passes/frame.cpp)
- [`../examples/particles/explosion_stress.cpp`](../examples/particles/explosion_stress.cpp)

### Renderer refactor

- alpha particles can render through a half-resolution offscreen path
- additive, alpha, and distortion now use a prepared-stream model
- particles are uploaded once per pass, then drawn by span offset instead of rebuilding a temporary vector for every span

Primary files:

- [`../src/rendering/renderer/backends/diligent/passes/particles.cpp`](../src/rendering/renderer/backends/diligent/passes/particles.cpp)
- [`../src/rendering/renderer/backends/diligent/passes/particle_draw.cpp`](../src/rendering/renderer/backends/diligent/passes/particle_draw.cpp)

## How To Read The Log

The sample prints one `Explosion stress: ...` line when stats logging is enabled.
When `KARMA_PARTICLE_STATS=1` is set, the renderer also prints one
`Particle stats: ...` line per second. Prefer the renderer line when comparing
particle changes across examples because it is emitted after final render
submission and includes GPU particle generation, compatibility CPU batches,
sorting, grouping, scene-copy, and draw-submission fields in one place.

The explosion stress startup path now relies on committed prefab-local
EXR-derived flipbook atlases, so there is no runtime flipbook-source selection
to log.

Key fields:

- `world_prefabs`, `world_emitters`, `world_lights`
  - rising counts during repeated replay usually mean lifetime/cleanup problems
- `part_sys_ms(sync/sim/pack)`
  - particle effect binding, renderer descriptor submission, and compatibility packing cost
- `part_render_ms(add/asort/dsort/draw)`
  - renderer-side additive prep, alpha prep, distortion prep, and draw submission
- `gpu_particle_capacity`, `gpu_alive_particles`, `gpu_dead_particles`,
  `gpu_spawned_particles`, `gpu_killed_particles`, `gpu_compacted_particles`
  - persistent renderer GPU particle state capacity and one-frame-delayed exact
    live/dead/spawn/kill/compact counters
- `gpu_compute_dispatches`, `gpu_indirect_draws`,
  `gpu_indirect_dispatches`, `gpu_sort_key_count`, `gpu_sort_passes`
  - compute work, GPU-generated indirect draw/dispatch activity, and grouped
    alpha/distortion sort-key generation
- `gpu_allocator_live_emitters`, `gpu_allocator_free_ranges`,
  `gpu_allocator_active_capacity`, `gpu_allocator_high_water_capacity`,
  `gpu_allocator_retired_emitters`, `gpu_allocator_reused_slots`,
  `gpu_allocator_allocation_failures`
  - persistent GPU emitter slots, particle-slot allocator state, reuse, and
    allocation failure diagnostics
- `gpu_culled_emitters`, `gpu_culled_particles`, `gpu_culling_dispatches`
  - camera/visibility culled GPU emitters and particles rejected during GPU
    instance/sort-key preparation
- `gpu_global_sort_active`, `gpu_grouped_sort_fallback`
  - whether Vulkan descriptor-indexed global transparent sorting is active, or
    whether the backend is using the documented grouped material fallback
- `gpu_sort_overflow`, `gpu_fallback_active`, `gpu_stats_readback_age`
  - sort overflow, analytic GPU bridge fallback, and async readback age
- `cpu_fallback_particles`
  - baked compatibility particles still submitted from CPU producers
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

## Captured Baseline: 2026-05-17

Local build target:

```bash
cmake --build build --target particles_explosion_stress prefabs_gallery
```

Default explosion stress:

```bash
timeout 12s env KARMA_PARTICLE_STATS=1 KARMA_EXPLOSION_STRESS_STATS=1 \
  ./build/examples/particles/explosion_stress --stats
```

Representative steady-state averages:

- `fps=60-62`
- `simulated_particles=3400-3500`, `packed_particles=3420-3500`
- `simulation_ms=0.27-0.31`
- `packing_ms=0.23-0.26`
- `additive_grouping_ms=0.20-0.25`
- `alpha_sort_ms=0.06-0.07`, `distortion_sort_ms=0.008-0.010`
- `draw_submission_ms=0.30-0.35`
- `alpha_draw_calls=140-155`

Prefab gallery:

```bash
timeout 12s env KARMA_PARTICLE_STATS=1 KARMA_PREFAB_GALLERY_STATS=1 \
  ./build/examples/prefabs/gallery
```

Representative active-window averages:

- `fps=60-61`
- `simulated_particles=2200-2900`, `packed_particles=2200-2830`
- `simulation_ms=0.35-0.53`
- `packing_ms=0.32-0.48`
- `additive_grouping_ms=0.25-0.33`
- `alpha_sort_ms=0.04-0.08`, `distortion_sort_ms=0.05-0.09`
- `draw_submission_ms=0.29-0.55`
- `submitted_batches=35-43`

Heavier stress sweep:

```bash
timeout 12s env KARMA_PARTICLE_STATS=1 KARMA_EXPLOSION_STRESS_STATS=1 \
  ./build/examples/particles/explosion_stress --stats --explosions 25 --period 2.0
```

Representative steady-state averages:

- `fps=60-61`
- `simulated_emitters=275`, `visible_emitters=163-164`
- `simulated_particles=14600-14700`, `packed_particles=14780-14800`
- `submitted_batches=163`, `submitted_particles=14780-14800`
- `simulation_ms=1.06-1.18`
- `packing_ms=0.84-0.96`
- `additive_grouping_ms=1.86-2.02`
- `alpha_sort_ms=0.27-0.29`, `distortion_sort_ms=0.03`
- `draw_submission_ms=0.41-0.47`

Interpretation:

The older 2026-05 optimization notes below describe the pre-GPU particle path.
Use them as historical baselines only; current comparisons should include
`gpu_*` and `cpu_fallback_particles`.

- Default and gallery slowdown is not primarily sorting; alpha/distortion sort
  costs are small compared with grouping, simulation, packing, and draw
  submission.
- The heavy sweep makes additive grouping the largest measured particle-side
  cost, followed by simulation and packing.
- Particle count matters because the costs scale with roughly `15k` packed
  particles and `160+` visible/submitted emitters, but the current bottleneck
  presents first as CPU-side grouping/simulation/packing rather than GPU draw
  submission or exact sort.
- The next optimization should target additive grouping and span/batch
  fragmentation before replacing the sort algorithm.

## Optimization Pass: 2026-05-17

Changes:

- Additive grouping now keeps grouped batch references and copies particles only
  once into the prepared upload stream.
- Simulated particle packing now constructs `ParticlePackedInstance` values
  directly in the batch vector.
- Ground-collision simulation now hoists stable drag/friction/rest constants out
  of the per-particle loop.

Heavy stress after the first pass:

```bash
timeout 10s env KARMA_PARTICLE_STATS=1 KARMA_EXPLOSION_STRESS_STATS=1 \
  ./build/examples/particles/explosion_stress --stats --explosions 25 --period 2.0
```

Representative steady-state averages:

- `fps=60-61`
- `simulated_particles=14600-14790`, `packed_particles=14770-14790`
- `simulation_ms=0.92-1.15`
- `packing_ms=0.83-1.07`
- `additive_grouping_ms=0.23-0.27`
- `alpha_sort_ms=0.26-0.34`, `distortion_sort_ms=0.03`
- `draw_submission_ms=0.45-0.63`

Prefab gallery after the first pass:

```bash
timeout 10s env KARMA_PARTICLE_STATS=1 KARMA_PREFAB_GALLERY_STATS=1 \
  ./build/examples/prefabs/gallery
```

Representative active-window averages:

- `fps=60`
- `simulated_particles=2100-2900`, `packed_particles=2130-2890`
- `simulation_ms=0.10-0.35`
- `packing_ms=0.11-0.36`
- `additive_grouping_ms=0.05-0.11`
- `alpha_sort_ms=0.01-0.05`, `distortion_sort_ms=0.02-0.05`
- `draw_submission_ms=0.09-0.35`

Updated interpretation:

- Additive grouping is no longer the first bottleneck in the measured scenes.
- The remaining particle-side costs are simulation and packing under heavy
  particle counts, with draw submission occasionally visible in the stress
  harness.
- The next renderer-side target is persistent/reused packed-batch storage or a
  lower-copy submission path. The next simulation target is reducing work for
  resting ground-collision particles.

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
- `gpu_grouped_sort_fallback=true`:
  - descriptor-indexed global particle material sorting was unavailable, so
    alpha/distortion particles are sorted inside material groups
- `gpu_allocator_high_water_capacity` keeps rising after replay windows repeat:
  - persistent particle GPU slot reuse is probably failing or max-particle
    descriptors are changing unexpectedly
- `gpu_allocator_allocation_failures>0` or `gpu_fallback_active=true` on Vulkan:
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
