# Particle Performance Bootstrap

This file now represents the earlier particle-performance pass only.

The current explosion stress / particle renderer handoff lives in:

- [EXPLOSION_STRESS_BOOTSTRAP.md](EXPLOSION_STRESS_BOOTSTRAP.md)
- [EXPLOSION_STRESS_PERF.md](EXPLOSION_STRESS_PERF.md)

This file is the handoff for the current particle-performance pass.

## What Was Optimized

### 1. Effect hot-reload polling

Particle effect file polling no longer checks timestamps every frame.

Primary files:

- [`../include/karma/features/visual/particles/effect_library.h`](../include/karma/features/visual/particles/effect_library.h)
- [`../src/features/visual/particles/effect_library.cpp`](../src/features/visual/particles/effect_library.cpp)

Current behavior:

- polling is throttled to `250ms`
- no polling work happens when no effect files are registered

### 2. Batch submission copies

Particle batches are passed by value and moved into backend storage.

Primary files:

- [`../include/karma/rendering/renderer/backend.hpp`](../include/karma/rendering/renderer/backend.hpp)
- [`../include/karma/rendering/renderer/device.h`](../include/karma/rendering/renderer/device.h)
- [`../src/rendering/renderer/device.cpp`](../src/rendering/renderer/device.cpp)
- [`../src/rendering/renderer/backends/diligent/backend.hpp`](../src/rendering/renderer/backends/diligent/backend.hpp)

### 3. Additive draw grouping

Additive particle draws are grouped by compatible render state before upload and draw.

Primary file:

- [`../src/rendering/renderer/backends/diligent/passes/particle_draw.cpp`](../src/rendering/renderer/backends/diligent/passes/particle_draw.cpp)

### 4. CPU-side simulation and presentation cuts

`ParticleSystem` now does less CPU work than before.

Reduced work includes:

- fast path for linear curve exponents
- cheaper alive-particle compaction
- no CPU-side size lerp for `ParticleSystem` presentation
- no CPU-side alpha lerp for `ParticleSystem` presentation
- no CPU-side RGB lerp for `ParticleSystem` presentation
- no CPU-side atlas UV baking for `ParticleSystem` presentation

Primary file:

- [`../src/features/visual/particles/particle_system.cpp`](../src/features/visual/particles/particle_system.cpp)

## Current Architecture

There are now two particle presentation modes in:

- [`../include/karma/rendering/renderer/particles.h`](../include/karma/rendering/renderer/particles.h)

### `Baked`

Used by custom or manual particle producers like the beam path.

CPU provides:

- final size
- final color
- final atlas UVs

### `Simulated`

Used by `ParticleSystem`.

CPU provides:

- position
- rotation
- start/end size
- start/end color
- normalized age
- age seconds
- frame offset
- batch-level curve and atlas metadata

GPU derives:

- size curve
- alpha curve
- color interpolation
- atlas frame selection
- atlas UV rectangle

This split is deliberate. The heavy generic path got optimized without forcing beam-authored batches onto a new representation immediately.

## Important Files

High-signal files for continuing this work:

- [`../src/features/visual/particles/particle_system.cpp`](../src/features/visual/particles/particle_system.cpp)
- [`../include/karma/rendering/renderer/particles.h`](../include/karma/rendering/renderer/particles.h)
- [`../src/rendering/renderer/backends/diligent/backend.hpp`](../src/rendering/renderer/backends/diligent/backend.hpp)
- [`../src/rendering/renderer/backends/diligent/passes/particles.cpp`](../src/rendering/renderer/backends/diligent/passes/particles.cpp)
- [`../src/rendering/renderer/backends/diligent/passes/particle_draw.cpp`](../src/rendering/renderer/backends/diligent/passes/particle_draw.cpp)
- [`../src/rendering/renderer/backends/diligent/backend_render.cpp`](../src/rendering/renderer/backends/diligent/backend_render.cpp)
- [`../src/features/visual/beams/beam_path_system.cpp`](../src/features/visual/beams/beam_path_system.cpp)

## Validation

Historical validation for this pass used:

```bash
cmake --build build --target karma_energy_orb_example karma_prefab_gallery_example -j2
timeout 5s ./build/karma_energy_orb_example
```

Expected result in this environment:

- build succeeds
- runtime stops at `GLFW failed to initialize`
- particle systems still initialize and hot-reload cleanly before exit

## Gallery-Specific Follow-Up

There is a separate handoff for the staged explosion and wave-volume interaction in the prefab gallery:

- [PREFAB_GALLERY_BOOTSTRAP.md](PREFAB_GALLERY_BOOTSTRAP.md)

Important state already in the tree:

- wave and volumetric-sphere materials now sample a pre-particle scene copy instead of a post-particle copy
- wave volume proxies now render as projected screen-bounds quads instead of near-full-screen overlays
- the prefab gallery has opt-in perf logging behind `KARMA_PREFAB_GALLERY_STATS=1`

That work reduced obvious overdraw and fixed the disappearing-volume failure, but the gallery still has an unresolved frame-rate drop when explosions fire.

## What Still Costs CPU Time

The main remaining CPU-side costs are:

1. alpha/distortion depth sorting
2. particle simulation itself

Secondary costs:

1. per-frame instance buffer upload
2. beam path still using baked particle presentation

## Recommended Next Steps

Best next moves, in order:

1. measure alpha and distortion sorting before more architecture changes
2. try bucketed or approximate depth ordering if sort cost is high
3. only after that, evaluate a larger move toward GPU simulation
4. for the prefab gallery specifically, isolate the heat-distortion layer before changing light/shadow code again

## Caution

Do not casually unify the beam path onto the simulated presentation mode unless you verify the authored look survives. The current split is intentional.
