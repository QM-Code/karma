# Particle Performance Bootstrap

This file is the handoff for the current particle-performance pass.

## What Was Optimized

### 1. Effect hot-reload polling

Particle effect file polling no longer checks timestamps every frame.

Primary files:

- [effect_library.h](/home/irie/Documents/karma/include/karma/particles/effect_library.h)
- [effect_library.cpp](/home/irie/Documents/karma/src/particles/effect_library.cpp)

Current behavior:

- polling is throttled to `250ms`
- no polling work happens when no effect files are registered

### 2. Batch submission copies

Particle batches are now passed by value and moved into backend storage.

Primary files:

- [backend.hpp](/home/irie/Documents/karma/include/karma/renderer/backend.hpp)
- [device.h](/home/irie/Documents/karma/include/karma/renderer/device.h)
- [device.cpp](/home/irie/Documents/karma/src/renderer/device.cpp)
- [backend.hpp](/home/irie/Documents/karma/include/karma/renderer/backends/diligent/backend.hpp)

### 3. Additive draw grouping

Additive particle draws are grouped by compatible render state before upload/draw.

Primary file:

- [backend_render.cpp](/home/irie/Documents/karma/src/renderer/backends/diligent/backend_render.cpp)

### 4. CPU-side simulation/presentation cuts

`ParticleSystem` does less CPU work than before.

Reduced work includes:

- fast path for linear curve exponents
- cheaper alive-particle compaction
- no CPU-side size lerp for `ParticleSystem` presentation
- no CPU-side alpha lerp for `ParticleSystem` presentation
- no CPU-side RGB lerp for `ParticleSystem` presentation
- no CPU-side atlas UV baking for `ParticleSystem` presentation

Primary file:

- [particle_system.cpp](/home/irie/Documents/karma/src/particles/particle_system.cpp)

## Current Architecture

There are now two particle presentation modes in:

- [types.h](/home/irie/Documents/karma/include/karma/renderer/types.h)

### `Baked`

Used by custom/manual particle producers like the beam path.

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

- [particle_system.cpp](/home/irie/Documents/karma/src/particles/particle_system.cpp)
- [types.h](/home/irie/Documents/karma/include/karma/renderer/types.h)
- [backend.hpp](/home/irie/Documents/karma/include/karma/renderer/backends/diligent/backend.hpp)
- [backend_render.cpp](/home/irie/Documents/karma/src/renderer/backends/diligent/backend_render.cpp)
- [beam_path_system.cpp](/home/irie/Documents/karma/src/beams/beam_path_system.cpp)

## Validation

Commands used during this pass:

```bash
cmake --build build --target karma_energy_orb_example karma_prefab_gallery_example -j2
timeout 5s ./build/karma_energy_orb_example
```

Expected result in this environment:

- build succeeds
- runtime stops at `GLFW failed to initialize`
- particle systems still initialize and hot-reload cleanly before exit

## Gallery-Specific Follow-Up

There is now a separate handoff for the current staged-explosion / wave-volume interaction in the prefab gallery:

- [PREFAB_GALLERY_BOOTSTRAP.md](/home/irie/Documents/karma/docs/PREFAB_GALLERY_BOOTSTRAP.md)

Important state already in the tree:

- wave / volumetric-sphere materials now sample a pre-particle scene copy instead of a post-particle copy
- wave volume proxies now render as projected screen-bounds quads instead of near-full-screen overlays
- the prefab gallery has opt-in perf logging behind `KARMA_PREFAB_GALLERY_STATS=1`

That work reduced obvious overdraw and fixed the visual disappearing-volume failure, but the gallery still has an unresolved frame-rate drop when explosions fire.

## What Still Costs CPU Time

The main remaining CPU-side costs are:

1. alpha/distortion depth sorting
2. particle simulation itself

Secondary costs:

1. per-frame instance buffer upload
2. beam path still using baked particle presentation

## Recommended Next Steps

Best next moves, in order:

1. measure alpha/distortion sorting before more architecture changes
2. try bucketed or approximate depth ordering if sort cost is high
3. only after that, evaluate a larger move toward GPU simulation
4. for the prefab gallery specifically, isolate the heat-distortion layer before changing light/shadow code again

## Caution

Do not casually unify the beam path onto the simulated presentation mode unless you verify the authored look survives. The current split is intentional.
