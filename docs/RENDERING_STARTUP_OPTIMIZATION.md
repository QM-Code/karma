# Rendering Startup Optimization Notes

## Scope

This pass focused on Diligent renderer startup latency using
`karma_diligent_gltf_viewer_example` as the benchmark. The benchmark loads the
DamagedHelmet glTF sample, imported glTF materials, the `papermill.ktx`
environment, and the normal engine warm-up path.

The priority was to make startup measurable first, then remove the safest
high-impact stalls without preserving legacy compatibility paths. Particle GPU
resources are still warmed before first use so explosions do not hitch on their
first visible frame.

## Reproduce The Benchmark

Build the viewer:

```bash
cmake --build build --target karma_diligent_gltf_viewer_example --parallel $(nproc)
```

Run with startup, render-system, and render-resource timing:

```bash
timeout 12s env \
  KARMA_ENGINE_STARTUP_DIAG=1 \
  KARMA_RENDER_SYSTEM_DIAG=1 \
  KARMA_RENDER_RESOURCE_DIAG=1 \
  ./build/karma_diligent_gltf_viewer_example
```

`KARMA_RENDER_STARTUP_DIAG=1` can be used when only Diligent backend startup
timing is needed. `KARMA_RENDER_SYSTEM_DIAG_EVERY_FRAME=1` keeps
`RenderSystem` stage logging active after startup and should only be used for
short triage runs.

## Results

Local glTF viewer runs on June 15, 2026 showed:

| Measurement | Before | After |
| --- | ---: | ---: |
| Startup through renderer warm-up | 5889.43 ms | 4242.59 ms |
| Environment setup | 1339.15 ms | 51.11 ms |
| Material bind/import warm-up | 917.09 ms | 896.42 ms |
| Particle resource prewarm | measured as part of warm-up | 471.73 ms |

The biggest win came from avoiding the slow HDR environment image load path for
the benchmark's KTX cubemap while still generating proper irradiance,
prefilter, and BRDF LUT resources. An unsafe direct-IBL shortcut was removed
because it made the helmet appear overly shiny and hid the source textures.

## Implemented Changes

- Added startup timing to `EngineApp` subsystem creation, loading splash frames,
  renderer warm-up, and first-frame stages.
- Added Diligent startup/resource timing helpers behind
  `KARMA_ENGINE_STARTUP_DIAG`, `KARMA_RENDER_STARTUP_DIAG`, and
  `KARMA_RENDER_RESOURCE_DIAG`.
- Replaced iterator-based binary file reads with a pre-sized read path for
  renderer assets.
- Added a narrow RGBA16F KTX cubemap loader for the copied Diligent
  `papermill.ktx` environment asset.
- Kept KTX environments on the normal generated IBL path:
  cubemap, irradiance, prefilter, and BRDF LUT are produced before rendering.
- Reduced material-only Assimp import work by loading material templates without
  mesh postprocess flags.
- Trimmed material shader-resource binding creation so additive and double-sided
  variants are created only when the material needs them.
- Kept particle resource prewarm in startup instead of deferring it to the first
  explosion or first particle instance.
- Added `RenderSystem` diagnostics for mesh/material binding and render stage
  timing to expose remaining startup costs.

## Remaining Startup Costs

The current visible hotspots are:

- Diligent pipeline/render-state creation and cache interaction.
- Embedded glTF texture decode/upload during material warm-up.
- Particle resource prewarm, which is intentional until a better no-hitch
  background or loading-screen prewarm policy exists.

Future passes should prioritize persistent pipeline cache validation, reusable
decoded texture caches, and loading-screen/background preparation. Particle work
should remain explicit and completed before first use.

## Visual Correctness Guard

Do not bind a raw environment cubemap as the PBR prefilter map or use a flat
color texture as the BRDF LUT. That shortcut makes reflective materials look
incorrect and can hide base-color textures. KTX cubemaps may skip equirectangular
conversion, but they still need valid generated IBL resources.
