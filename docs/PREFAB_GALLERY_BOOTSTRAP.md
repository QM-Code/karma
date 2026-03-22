# Prefab Gallery Bootstrap

This file is the handoff for the current prefab-gallery / staged-explosion / wave-volume work.

## What Exists Now

The prefab gallery scene currently showcases four prefab families in one scene:

1. beam prefabs
2. energy-orb prefabs
3. wave / volumetric-sphere prefabs
4. staged explosion prefabs

Primary file:

- [prefab_gallery_example.cpp](/home/irie/Documents/karma/examples/prefab_gallery_example.cpp)

Explosion package files:

- [explosion_prefab_package.h](/home/irie/Documents/karma/examples/explosion_prefab_package.h)
- [explosion_prefab_package.cpp](/home/irie/Documents/karma/examples/explosion_prefab_package.cpp)
- [prefab.kprefab](/home/irie/Documents/karma/examples/assets/prefabs/explosion/prefab.kprefab)

Wave / volume files:

- [prefab.kprefab](/home/irie/Documents/karma/examples/assets/prefabs/wave/prefab.kprefab)
- [volume_sphere_system.cpp](/home/irie/Documents/karma/src/volumes/volume_sphere_system.cpp)

Renderer-side composition files:

- [backend_render.cpp](/home/irie/Documents/karma/src/renderer/backends/diligent/backend_render.cpp)
- [backend_init.cpp](/home/irie/Documents/karma/src/renderer/backends/diligent/backend_init.cpp)

## Explosion Prefab Status

The explosion is now packaged as a reusable prefab/package instead of being hard-coded only inside `particle_example.cpp`.

Important behavior:

- the prefab stages flash, fireball, heat, authored core flipbook, authored smoke flipbook, embers, shock ring, debris, dust, smoke, scorch, and a pulsed point light
- the gallery instantiates four explosion controllers and replays them on a staggered timer
- the package does **not** use the EXR-sequence build path from `particle_example.cpp`
- the authored fire flipbook is loaded from `Explosion00_5x5.tga`
- the authored smoke flipbook is generated into a runtime atlas texture

That means any gallery-only authored-flipbook inconsistency is more likely a runtime/rendering problem than an EXR import problem.

## Renderer Changes Already Landed

Two important renderer-side fixes are already in the tree:

### 1. Volume / particle composition split

`WaveVolume` and `VolumetricSphere` materials no longer sample a post-particle scene copy.

Instead:

- they are routed into a dedicated pre-particle transparent bucket
- they sample an opaque / pre-particle scene-color copy
- explosion particles no longer become part of the wave volumes' sampled background

Primary file:

- [backend_render.cpp](/home/irie/Documents/karma/src/renderer/backends/diligent/backend_render.cpp)

### 2. Volume-sphere proxy optimization

The analytic volume spheres no longer render as near-full-screen camera-facing quads every frame.

Current behavior:

- `VolumeSphereSystem` projects each sphere to a conservative screen-space bounds rectangle
- the proxy quad is placed and scaled to that bounds rectangle
- off-screen proxies are hidden

Primary file:

- [volume_sphere_system.cpp](/home/irie/Documents/karma/src/volumes/volume_sphere_system.cpp)

This removed the largest obvious overdraw waste, but it did **not** fully solve the gallery FPS drop.

## Current Unresolved Problem

The remaining issue is still performance when the explosion row and the wave-volume row are active in the same scene.

Observed user-facing symptoms:

- frame rate tanks when explosions fire
- the wave / volumetric sphere row is now visually stable
- authored explosion flipbook layers may appear inconsistent under the perf drop

Current best inference:

- the remaining cost is still likely dominated by transparent / distortion / particle overdraw rather than local-light mode switching
- the heat-distortion layer is still a strong suspect
- the authored core/smoke flipbooks are sparse alpha layers, so they are easy to visually lose once frame time collapses

## Debug Instrumentation

The gallery now has opt-in perf logging behind:

```bash
KARMA_PREFAB_GALLERY_STATS=1
```

Current output includes:

- first-update traces: `Gallery update: ...`
- rolling perf lines: `Gallery perf: ...`

Those logs report:

- average FPS
- average and worst frame time
- active explosion visuals
- active explosion lights
- pending delayed restarts
- current local-light stats from the renderer

Primary file:

- [prefab_gallery_example.cpp](/home/irie/Documents/karma/examples/prefab_gallery_example.cpp)

## Important Environment Note

In this Codex environment, `karma_prefab_gallery_example` still does not survive long enough for reliable steady-state perf sampling.

What was observed here:

- startup / asset registration succeeds
- there is a long warm-up / startup delay before the first `onUpdate`
- the process then dies shortly after the first logged update in this environment

So local perf conclusions from this environment are weak. Real perf data should be captured on a machine with a stable windowing session.

## Recommended Next Steps

Best next moves:

1. run `KARMA_PREFAB_GALLERY_STATS=1 ./build/karma_prefab_gallery_example` on a stable machine and capture the `Gallery perf:` lines
2. isolate the heat-distortion layer by temporarily disabling `controller.heat` restarts and compare perf
3. if the spike remains, profile the alpha/distortion particle passes before changing lighting again
4. if the authored flipbook issue remains after perf is addressed, instrument per-controller restart counts for `core_flipbook` and `smoke_flipbook`

## Design Guidance

Keep these constraints in mind:

- do not conflate the gallery perf problem with the already-fixed local-light mode-switch bug
- do not assume the explosion prefab uses the EXR sequence path; it does not
- prefer targeted scene/perf measurements before another renderer rewrite
