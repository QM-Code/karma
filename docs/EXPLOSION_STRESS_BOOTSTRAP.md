# Explosion Stress Bootstrap

This file is the handoff for the explosion stress / particle renderer
performance pass.

It covers the leak fixes, particle instrumentation, renderer refactor, and the
current heavy-stress authored content that were landed during this pass.

## Scope

This work started from a repeated-replay slowdown in the explosion stress
sample:

- explosions were triggered in succession rather than all at once
- frame time still degraded over time
- embers and debris were the strongest authored suspects

The result was a mix of correctness fixes and renderer infrastructure changes.

## What Landed

### 1. Prefab lifecycle correctness

Explosion prefab instances were missing an explicit destroy path. That meant
repeated one-shot instantiation could leave old prefab roots and members alive
in ECS even after the visible explosion was finished.

Current state:

- generic prefab destruction exists via `prefabs::destroyPrefab(...)`
- explosions have a wrapper helper via `destroyExplosionPrefabController(...)`
- prefab particle and light members receive `VisibilityComponent`
- explosion lights hide correctly when off
- invisible or zero-intensity/range non-directional lights are skipped by scene extraction

Primary files:

- [`../include/karma/prefabs/prefab.h`](../include/karma/prefabs/prefab.h)
- [`../src/prefabs/prefab_runtime.cpp`](../src/prefabs/prefab_runtime.cpp)
- [`../examples/explosion_prefab_package.h`](../examples/explosion_prefab_package.h)
- [`../examples/explosion_prefab_package.cpp`](../examples/explosion_prefab_package.cpp)
- [`../src/renderer/render_system.cpp`](../src/renderer/render_system.cpp)

### 2. Particle system instrumentation

The stress sample now logs enough information to stop guessing.

Added categories include:

- prefab / emitter / light world counts
- particle system timings:
  - `sync`
  - `sim`
  - `pack`
- renderer timings:
  - `add`
  - `asort`
  - `dsort`
  - `draw`
- alpha and distortion substage timings:
  - `collect`
  - `sort`
  - `span`
- emitter and particle counters:
  - iterated / visible / culled / submitted emitters
  - simulated / packed / culled / ground-collision particles
- scene-copy state and half-resolution alpha state

Primary files:

- [`../include/karma/renderer/types.h`](../include/karma/renderer/types.h)
- [`../src/renderer/device.cpp`](../src/renderer/device.cpp)
- [`../src/renderer/backends/diligent/passes/render_state.cpp`](../src/renderer/backends/diligent/passes/render_state.cpp)
- [`../examples/explosion_stress_example.cpp`](../examples/explosion_stress_example.cpp)

### 3. Particle submission path cleanup

The particle system no longer routes simulated particles through the old
`ParticleInstance` submission path.

Current state:

- `ParticleSystem` emits `PackedParticleBatch`
- the backend accepts packed particle batches directly
- non-simulated producers can still use the compatibility path
- whole-emitter culling uses the primary camera and conservative live bounds
- alpha particles that are effectively invisible are skipped before submission

Primary files:

- [`../include/karma/renderer/backend.hpp`](../include/karma/renderer/backend.hpp)
- [`../include/karma/renderer/types.h`](../include/karma/renderer/types.h)
- [`../src/particles/particle_system.cpp`](../src/particles/particle_system.cpp)
- [`../src/renderer/backends/diligent/passes/frame.cpp`](../src/renderer/backends/diligent/passes/frame.cpp)

### 4. Renderer-side alpha restructuring

The major win in this pass was structural, not a content-side reduction.

The old alpha path sorted particles, then repeatedly rebuilt temporary
per-span particle vectors and remapped the instance buffer for each draw span.
That overhead dominated `part_alpha_ms(...span...)`.

Current state:

- particle rendering uses prepared streams:
  - one contiguous particle instance array
  - one span table with draw-state metadata and instance offsets
- each pass uploads particle instances once
- draws consume spans by instance-buffer offset
- additive, alpha, and distortion now all use this infrastructure
- the half-resolution alpha path remains available and instrumented

Primary files:

- [`../src/renderer/backends/diligent/passes/particles.cpp`](../src/renderer/backends/diligent/passes/particles.cpp)
- [`../src/renderer/backends/diligent/passes/particle_draw.cpp`](../src/renderer/backends/diligent/passes/particle_draw.cpp)
- [`../src/renderer/backends/diligent/backend_render.cpp`](../src/renderer/backends/diligent/backend_render.cpp)

### 5. Prefab packaging / visual polish

The reusable explosion package was also cleaned up as a shipping prefab surface,
not just a stress harness.

Current state:

- shock ring depth-testing is enabled
- smoke tinting is darker than the earlier defaults
- core and smoke flipbooks prefer EXR sequence atlases
- shared flipbook metadata now targets `400x400` atlas frames
- fire falls back to a resampled legacy sheet when the EXR sequence path fails
- smoke falls back to the procedural atlas when the smoke EXR path fails
- the package exposes flipbook source debug info and the stress sample logs it

Primary files:

- [`../examples/explosion_prefab_package.h`](../examples/explosion_prefab_package.h)
- [`../examples/explosion_prefab_package.cpp`](../examples/explosion_prefab_package.cpp)
- [`../examples/assets/particles/explosion_core_flipbook.kpeffect`](../examples/assets/particles/explosion_core_flipbook.kpeffect)
- [`../examples/assets/particles/explosion_smoke_flipbook.kpeffect`](../examples/assets/particles/explosion_smoke_flipbook.kpeffect)
- [`../examples/assets/particles/explosion_shock_ring.kpeffect`](../examples/assets/particles/explosion_shock_ring.kpeffect)
- [`../examples/assets/particles/explosion_smoke.kpeffect`](../examples/assets/particles/explosion_smoke.kpeffect)

## What The Stats Proved

Before the renderer refactor, the most important signal was:

- `part_sys_ms(sync/sim/pack)` stayed tiny
- `part_alpha_ms(collect/sort/span)` showed almost all alpha time in `span`
- the half-resolution alpha path alone did not fix the slowdown

That ruled out:

- particle effect binding churn as the main problem
- raw simulation cost as the main problem
- sort comparator instability as the main problem
- alpha pixel cost alone as the main problem

It pointed directly at renderer-side alpha span preparation and draw-path
fragmentation.

## Current Stress Content

To keep pressure on the system after the renderer refactor, authored particle
counts were raised:

- embers:
  - `burst_count = 504`
  - `max_particles = 576`
- debris:
  - `burst_count = 36`
  - `max_particles = 48`

Primary files:

- [`../examples/assets/particles/explosion_embers.kpeffect`](../examples/assets/particles/explosion_embers.kpeffect)
- [`../examples/assets/particles/explosion_debris.kpeffect`](../examples/assets/particles/explosion_debris.kpeffect)

## Key Files For Future Work

Most useful files if this area regresses again:

- [`../examples/explosion_stress_example.cpp`](../examples/explosion_stress_example.cpp)
- [`../examples/explosion_prefab_package.cpp`](../examples/explosion_prefab_package.cpp)
- [`../src/particles/particle_system.cpp`](../src/particles/particle_system.cpp)
- [`../include/karma/renderer/types.h`](../include/karma/renderer/types.h)
- [`../src/renderer/backends/diligent/passes/frame.cpp`](../src/renderer/backends/diligent/passes/frame.cpp)
- [`../src/renderer/backends/diligent/passes/particles.cpp`](../src/renderer/backends/diligent/passes/particles.cpp)
- [`../src/renderer/backends/diligent/passes/particle_draw.cpp`](../src/renderer/backends/diligent/passes/particle_draw.cpp)
- [`../src/renderer/render_system.cpp`](../src/renderer/render_system.cpp)
- [`../src/prefabs/prefab_runtime.cpp`](../src/prefabs/prefab_runtime.cpp)

## Validation

Build validation used:

```bash
cmake --build build-local --target karma_explosion_stress_example karma_prefab_gallery_example karma_particle_example -j2
```

Expected result in this environment:

- build succeeds
- windowed runtime validation is still blocked by GLFW initialization in this headless session

## Recommended Next Steps

If more work is needed in this area, the best next moves are:

1. add runtime multipliers for ember and debris burst counts so authored stress can be swept without editing assets
2. if simulation becomes dominant again, special-case resting ground-collision particles so they can sleep
3. if draw-call count becomes the next limit, reduce state fragmentation further instead of cutting authored effects first
4. keep using the stress sample as the primary proof harness before changing the gallery or other scenes
5. if more visual fidelity is still needed, revisit the current `RGBA8` flipbook import path before blaming the authored EXR assets alone
