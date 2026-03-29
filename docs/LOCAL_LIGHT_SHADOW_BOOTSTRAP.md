# Local Light Shadow Bootstrap

This file is the handoff for the current local-light / point-shadow renderer pass.

## What Exists Now

There are two related systems in the renderer:

1. local-light accumulation
2. point-light shadowing for shadow-casting point lights

They share the Forward+ light data path, but the shadow path still has its own resource allocation, slot selection, face rendering, and sample/compare flow.

## Current Status

The recent blocking bug is fixed.

What was happening:

- local lights were being selected and submitted correctly
- the point-shadow path was active
- but the point-shadow texture and sampler resources were not being rebound on the SRBs that actually rendered materials

The renderer now:

- creates explicit `R32_FLOAT` SRVs for the directional and point-shadow depth arrays
- binds `g_ShadowMap`, `g_PointShadowMap`, and `g_ShadowSampler` onto the actual material/default SRBs instead of relying only on static PSO bindings
- refreshes all dirty point-shadow faces in a frame when selected point lights are moving
- uses a much smaller point-shadow light-position cache threshold so animated lights do not update in visible chunks
- blends across adjacent point-shadow faces near seams instead of hard-switching a single face
- renders point-shadow faces with a slight FOV overlap to reduce seam exposure at face edges

That is the change that brought visible point-light illumination and shadows back in the probe sample.

## High-Signal Files

Shadow resource allocation, comparison sampler setup, and shading code:

- [`../src/renderer/backends/diligent/backend_init.cpp`](../src/renderer/backends/diligent/backend_init.cpp)

SRB shadow rebinding on actual material draw paths:

- [`../src/renderer/backends/diligent/resources/materials.cpp`](../src/renderer/backends/diligent/resources/materials.cpp)

Point-shadow slot selection, cache invalidation, and face rendering:

- [`../src/renderer/backends/diligent/passes/shadows.cpp`](../src/renderer/backends/diligent/passes/shadows.cpp)

Forward shading consumption and frame orchestration:

- [`../src/renderer/backends/diligent/passes/forward.cpp`](../src/renderer/backends/diligent/passes/forward.cpp)
- [`../src/renderer/backends/diligent/backend_render.cpp`](../src/renderer/backends/diligent/backend_render.cpp)

Backend state and limits:

- [`../include/karma/renderer/backends/diligent/backend.hpp`](../include/karma/renderer/backends/diligent/backend.hpp)
- [`../src/app/engine_app.cpp`](../src/app/engine_app.cpp)

Probe sample:

- [`../examples/light_stress_example.cpp`](../examples/light_stress_example.cpp)

## Runtime Behavior

Current point-shadow behavior:

- `point_shadow_max_lights` sets the runtime point-shadow light budget
- the renderer currently supports up to `16` shadow-casting point lights at compile time
- each selected point light renders `6` faces into a `Texture2DArray` depth map
- point-shadow map resolution defaults to half of `shadow_map_size` with a minimum of `256`
- the light-stress sample uses a gradual `1-16` shadowed-light workflow

Important related renderer behavior:

- local lights use Forward+ tiling past the small CPU fallback path
- rejected/tight compute cases still fall back correctly instead of silently dropping local lights
- near-plane point-light screen coverage has already been fixed so lights do not cut off when the camera moves into the volume
- when selected point lights move, the renderer refreshes all dirty point-shadow faces that frame instead of trickling them through a small cache budget
- point-shadow seam artifacts are reduced by sampling adjacent faces near boundaries instead of relying on one hard face pick

## Probe Example

Reference sample:

- [`../examples/light_stress_example.cpp`](../examples/light_stress_example.cpp)

Current intended workflow:

- default run: one obvious moving shadowed point light
- `--lights N`: scale safe mode up gradually with moving point lights
- `--stats`: log Forward+ stats after startup
- `--unsafe`: dense non-shadowed stress profile

For sample-specific layout and motion choices, also read:

- [LOCAL_LIGHT_PROBE_BOOTSTRAP.md](LOCAL_LIGHT_PROBE_BOOTSTRAP.md)

## Validation

Commands used during the recent renderer split:

```bash
cmake -S . -B build-local
cmake --build build-local --target karma_light_stress_example -j2
```

Useful runtime checks on a machine with a windowing session:

```bash
./build-local/karma_light_stress_example
./build-local/karma_light_stress_example --lights 9
./build-local/karma_light_stress_example --lights 16 --stats
```

Expected result:

- visible local-light contribution in safe mode
- visible point-light shadows in safe mode
- no disappearing lights at the `8 -> 9` transition
- no obvious low-rate shadow refresh jitter while lights animate
- much less visible cubemap-face seam boxing on the ground

## Recommended Next Steps

Best next moves:

1. tune point-shadow quality and update budgeting now that the path is stable
2. validate shadow quality across more geometry than the probe scene
3. only move to a custom linear-depth point-shadow format if depth-SRV behavior regresses again on another backend

## Design Guidance

Keep these constraints in mind:

- shadow resources should be bound on the SRBs that actually draw materials
- point-shadow runtime limits should stay explicit and conservative by default
- the probe example should remain a gradual validation scene first and a stress scene second
