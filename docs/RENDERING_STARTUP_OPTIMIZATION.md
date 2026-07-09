# Rendering Startup Optimization Notes

## Scope

This pass focused on Diligent renderer startup latency using
`rendering_gltf_viewer` as the benchmark. The benchmark loads the
DamagedHelmet glTF sample, imported glTF materials, the `papermill.ktx`
environment, and the normal engine warm-up path.

The priority was to make startup measurable first, then remove the safest
high-impact stalls without preserving legacy compatibility paths. Particle GPU
resources are still warmed before first use so explosions do not hitch on their
first visible frame.

## Reproduce The Benchmark

Build the viewer:

```bash
cmake --build build --target karma_scene_bake rendering_gltf_viewer --parallel $(nproc)
```

Bake the default DamagedHelmet startup scene package:

```bash
./build/karma_scene_bake \
  --bake-packages \
  --asset-cache-root examples/assets/rendering/damaged_helmet/bakes/asset_cache \
  --output examples/assets/damaged_helmet_viewer.kscenebake.json \
  examples/assets/damaged_helmet_viewer.kscene.json
```

Run with startup, render-system, and render-resource timing:

```bash
timeout 12s env \
  KARMA_ENGINE_STARTUP_DIAG=1 \
  KARMA_RENDER_SYSTEM_DIAG=1 \
  KARMA_RENDER_RESOURCE_DIAG=1 \
  ./build/examples/rendering/gltf_viewer
```

`KARMA_RENDER_STARTUP_DIAG=1` can be used when only Diligent backend startup
timing is needed. `KARMA_RENDER_SYSTEM_DIAG_EVERY_FRAME=1` keeps
`RenderSystem` stage logging active after startup and should only be used for
short triage runs.

Additional focused diagnostics:

- `KARMA_RENDER_PIPELINE_DIAG=1` logs individual Diligent PSO creation times.
- `KARMA_RENDER_TEXTURE_IMPORT_DIAG=1` logs imported texture reference
  collection, embedded decode, and render-thread upload timings.
- `KARMA_SHADER_CACHE_LOG=1` logs render-state cache path, load/save size, and
  loaded content version.
- `KARMA_SHADER_CACHE_FLUSH=1` saves the render-state cache at device init and
  after renderer warm-up. This is useful for `timeout` benchmark runs where the
  process may not reach normal shutdown.
- `KARMA_SHADER_CACHE_PATH=/path/to/cache.diligentcache` overrides the existing
  Diligent render-state cache file location for isolated cold/warm validation.

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

Future passes should prioritize persistent pipeline cache validation and
loading-screen/background preparation. Persistent decoded asset caches are not
part of the current policy; source assets remain authoritative. Particle work
should remain explicit and completed before first use.

## June 16, 2026 Pass

This pass kept blocking warm-up and avoided persistent or generated decoded
asset caches. Source glTF embedded textures are still decoded from the original
model bytes, and all Diligent texture creation/upload remains on the render
thread.

Local glTF viewer diagnostics on June 16, 2026 showed:

| Measurement | Before | After |
| --- | ---: | ---: |
| Startup through renderer warm-up | ~4365 ms | 3154.36 ms |
| Forward pipeline creation | ~1832 ms | 783.13 ms |
| Imported material/template creation | ~873 ms | 557.81 ms |
| Embedded texture import preload | included in material creation | 348.96 ms |
| Particle resource prewarm | ~468 ms | 501.73 ms |

Forward startup now creates only the opaque and standard transparent PSOs during
device initialization. Depth prepass, additive, additive double-sided, and
transparent double-sided PSOs are created lazily when warm-up scene content
requires them. In the DamagedHelmet benchmark, those unused variants were not
created.

Imported material loading now scans unique Assimp texture references first,
decodes unique embedded textures in parallel on CPU, uploads the decoded images
sequentially on the render thread, and then builds material records from the
existing in-memory texture cache. The measured DamagedHelmet import preloaded 5
embedded textures: 278.68 ms decode and 69.84 ms upload.

Imported material template records now initialize their texture SRBs once per
asset-path template. `createMaterialFromAsset()` clones those records instead of
recreating SRBs for every material clone when the template bindings are already
available.

Particle resources remain intentionally prewarmed before first gameplay use.
The new particle timings break prewarm into fallback textures, shader compile,
constant buffers, compute PSOs, graphics PSOs, SRB/material-table binding, GPU
buffers, and half-resolution composite resources. No particle work was deferred
past warm-up in this pass.

## Pipeline Cache Validation Pass

The follow-up pass made the existing Diligent render-state cache observable and
safe to validate under `timeout` runs. It did not introduce decoded texture/model
caches or generated optimized assets.

Changes:

- Added render-state cache config/load/save diagnostics with cache path,
  existence, byte size, content version, load time, and save time.
- Set Diligent render-state cache verbose logging only when
  `KARMA_SHADER_CACHE_LOG=1`.
- Added a backend warm-up cache flush hook and call it once after blocking
  renderer warm-up when `KARMA_SHADER_CACHE_FLUSH=1`.
- Kept normal shutdown cache saving for ordinary runs.

Clean glTF viewer run on June 16, 2026, with diagnostics but without explicit
cache flush:

| Measurement | Result |
| --- | ---: |
| Startup through renderer warm-up | 3248.99 ms |
| Device init total | 1166.20 ms |
| Forward pipeline creation | 821.88 ms |
| Imported material/template creation | 594.65 ms |
| Embedded texture preload | 352.70 ms |
| Particle resource prewarm | 552.64 ms |

Isolated cache validation used
`KARMA_SHADER_CACHE_PATH=/tmp/karma_shader_cache_validation.diligentcache`.
The cold populate run started with no cache, saved 557868 bytes at device init,
then saved 972680 bytes after renderer warm-up. The warm run loaded that cache
with content version 18.

| Measurement | Cold Isolated Cache | Warm Isolated Cache |
| --- | ---: | ---: |
| Cache load file | 0.01 ms | 0.54 ms |
| Main shader creation | 4637.87 ms | 9.35 ms |
| Particle shader creation | 1190.61 ms | 4.39 ms |
| Particle sort/indirect resources | 1491.78 ms | 138.07 ms |
| Particle resource prewarm | 3056.61 ms | 512.70 ms |
| Startup through renderer warm-up | 10576.68 ms | 3089.95 ms |

The cache validation shows shader serialization/loading is working, especially
for particle warm-up shaders. Forward Vulkan graphics PSO creation remains a
large cost even with the render-state cache loaded, so future work should focus
on reducing or moving PSO creation rather than expecting the existing cache to
remove all pipeline latency.

## Forward Transparent Lazy Pass

The next pass made the standard transparent forward PSO scene-demand lazy
instead of creating it during device initialization. Transparent, reflection,
additive, double-sided, and depth-prepass forward PSOs are still created during
blocking warm-up when the loaded scene queues draws that need them. For the
opaque DamagedHelmet benchmark, no transparent draw lists were populated, so no
transparent forward PSO was created before warm-up completed.

Material binding no longer forces transparent forward variants while importing
opaque material templates. Lazy-created forward PSOs now initialize their default
material SRB immediately, and material/default SRBs created after forward-plus
setup bind the current forward-plus light buffers and shadow resources.

Clean glTF viewer run on June 16, 2026, with diagnostics after this pass:

| Measurement | Result |
| --- | ---: |
| Startup through renderer warm-up | 2574.39 ms |
| Device init total | 733.06 ms |
| Forward pipeline creation | 379.13 ms |
| Imported material/template creation | 520.40 ms |
| Embedded texture preload | 317.31 ms |
| Particle resource prewarm | 483.27 ms |

Relative to the previous clean diagnostic run, startup through warm-up improved
from 3248.99 ms to 2574.39 ms. Device-init forward pipeline creation dropped
from two graphics PSOs at 821.88 ms to one opaque graphics PSO at 379.13 ms.

## June 19 Package And Warm-Start Pass

The latest pass moved rendering examples onto startup asset packages so example
`onStart` code no longer parses source GLB/GLTF files directly. The engine now
imports configured `EngineConfig::startup_asset_packages` before game startup,
then prewarms package meshes, materials, and textures through `RenderSystem`.
Package assets are unloaded on shutdown after the render prewarm handle is
released.

Rendering examples using the package path:

- `rendering_gltf_viewer`
- `rendering_bloom`
- `rendering_light_stress`
- `rendering_material_assignment`
- `rendering_postprocess`
- `rendering_postwar_city`

The package path works with the asset-cache v2 layer documented in
`docs/ASSET_CACHE_V2_AND_RUNTIME_TEXTURES.md`. Warm package imports restore
typed blobs from cache without running source importers. Cold imports still
parse the source assets, write persistent blobs, then write the package
manifest.

Startup timing logs are more verbose. `KARMA_ENGINE_STARTUP_DIAG=1` now prints
stage start offsets, stage durations, and running totals. The `game_onStart`
diagnostic also reports wait-loop time, event polling time, sleep time, splash
frame count, and splash render time so hidden loading-screen work is visible.

The loading splash is no longer forced to appear immediately. By default,
`EngineConfig::LoadingSplashConfig::show_after_ms` is 750 ms, which avoids a
short splash flash on fast warm starts. Set `show_after_ms = 0` for examples or
games that should show the splash as soon as startup begins.

For runtime texture warmup, `RenderSystem` now prepares uploaded texture data
through the content helper that can transcode cached KTX2 Basis payloads to BC7
when `KARMA_TEXTURE_BC7=1` and the backend reports BC7 support. Otherwise the
RGBA8 fallback is uploaded. This avoids re-decoding source textures on warm
package runs while keeping a fallback path for devices without BC7.

Recommended warm-start diagnostic command:

```bash
env \
  KARMA_ENGINE_STARTUP_DIAG=1 \
  KARMA_RENDER_RESOURCE_DIAG=1 \
  KARMA_RENDER_PIPELINE_DIAG=1 \
  KARMA_RENDER_TEXTURE_IMPORT_DIAG=1 \
  KARMA_TEXTURE_BC7=1 \
  ./build/examples/rendering/gltf_viewer
```

## Particle Scene-Demand Prewarm Pass

The next pass stopped running the full particle resource prewarm for render
layers that have no particle work. The renderer now checks for submitted CPU
particle batches, submitted GPU emitters, or existing GPU emitter runtime state
before creating particle shaders, compute PSOs, graphics PSOs, material tables,
GPU buffers, and half-resolution composite resources.

This preserves the no-first-visible-frame-hitch policy for particle content:
when a frame has submitted particles or emitter runtime state, full particle
resources are still created synchronously before `renderParticlePasses()` draws
anything. No persistent decoded asset cache or generated optimized asset was
introduced.

Clean glTF viewer run on June 16, 2026, with diagnostics after this pass:

| Measurement | Result |
| --- | ---: |
| Startup through renderer warm-up | 2145.61 ms |
| Device init total | 741.56 ms |
| Forward pipeline creation | 364.71 ms |
| Imported material/template creation | 579.84 ms |
| Embedded texture preload | 366.28 ms |
| Particle resource prewarm | skipped, 0.01 ms gate |
| Renderer warm-up | 582.88 ms |

Relative to the previous clean diagnostic run, startup through warm-up improved
from 2574.39 ms to 2145.61 ms. The glTF viewer does not submit particle
batches or GPU emitters, so the particle render-layer stages logged
`particle resources skipped` and `particle pass skipped`.

The material import path remains the dominant warm-up cost for this benchmark.
The measured wall time is mostly the second Assimp parse of the glTF plus CPU
decode and render-thread upload of the five embedded material textures. A
larger renderer/content API change is needed to avoid that backend Assimp pass
cleanly by carrying imported material texture references or embedded texture
payloads from the content importer to the renderer.

Particle prewarm remains blocking for frames that submit particle work. The
renderer no longer pays that cost for no-particle scenes such as the glTF
viewer benchmark.

## Imported Material Payload Pass

The follow-up pass carries renderer-facing imported material payloads from the
content GLB importer to the renderer material library. The content importer
captures material scalar values, texture semantics, texture-coordinate
transforms, external texture paths, and encoded embedded texture bytes while the
Assimp scene is already loaded. The Diligent backend now uses that payload to
build imported material templates without reopening the glTF through Assimp.

No persistent decoded texture/model cache was added. Embedded glTF textures are
still sourced from the original model bytes, decoded transiently in memory, and
uploaded sequentially on the render thread.

Clean glTF viewer run on June 16, 2026, with diagnostics after this pass:

| Measurement | Previous | After |
| --- | ---: | ---: |
| Startup through renderer warm-up | 2145.61 ms | 1997.50 ms |
| Device init total | 741.56 ms | 712.26 ms |
| Forward pipeline creation | 364.71 ms | 371.70 ms |
| Imported material backend warm-up | 579.84 ms | 363.48 ms |
| Embedded texture preload | 366.28 ms | 363.18 ms |
| Renderer warm-up | 582.88 ms | 366.45 ms |
| Particle resource prewarm | skipped, 0.01 ms gate | skipped, 0.01 ms gate |

The DamagedHelmet material warm-up used the new
`material_from_imported_payload` path. The old backend
`imported_material_templates` Assimp import path was not used for the helmet
material. Payload texture preload decoded 5 embedded textures in 299.40 ms and
uploaded 5 textures in 63.46 ms; building the renderer material template after
texture upload took 0.26 ms and cloning it took 0.01 ms.

Imported payload material templates are cached by asset path and material index,
then cloned for repeated material descriptors. This preserves the previous
one-template-per-imported-material behavior while removing the backend Assimp
reparse from the glTF scene import path.

Particle prewarm policy is unchanged. A particle example diagnostic run still
created the particle resource stack synchronously before the first particle pass:
`particle_resources` total was 485.37 ms and the render-layer
`particle resources prewarm` stage was 485.41 ms.

## Particle Gallery Startup Pass

The particle gallery exposed a different startup profile from the opaque glTF
viewer: the scene intentionally submits particles, so particle resources still
prewarm synchronously, but the gallery also forced the standard transparent
forward PSO through the orb shell mesh and loaded the slower HDR environment
asset.

This pass did not defer particle prewarm or add any generated asset cache.
Instead it made two narrow changes:

- Material override draws now use source mesh alpha only as a fallback when no
  resolved material exists. Explicit opaque material overrides can keep a mesh
  on the opaque forward path even when the source mesh material was translucent.
- `particles_gallery` uses explicit opaque orb shell tint
  materials and the existing optimized `papermill.ktx` cubemap instead of the
  4K HDR environment.

Local particle gallery diagnostics on June 16, 2026 showed:

| Measurement | Before | After |
| --- | ---: | ---: |
| Startup through renderer warm-up | 4875.69 ms | 1492.99 ms |
| Environment setup | 545.94 ms | 46.31 ms |
| Renderer warm-up | 2322.50 ms | 583.86 ms |
| Transparent pre-particle pass | 1400.16 ms | 0.27 ms |
| Particle resource prewarm | 829.60 ms | 495.27 ms |

The transparent forward PSO was no longer created for the particle gallery
warm-up. The particle prewarm still ran before the first particle pass because
the scene submits beam, orb, and explosion emitters during warm-up.

The glTF viewer benchmark remained on the intended no-particle path after this
change. A follow-up diagnostic run reported startup through warm-up at
1953.24 ms, renderer warm-up at 375.96 ms, and particle resources skipped by the
scene-demand gate.

## July 2026 World Bake Pass

The next optimization pass moved to the `scene_world_bake` benchmark, which
loads `examples/assets/scene/world_bake/world.glb` through a package-backed
scene document and renders it with a KTX skybox. It also added the offline
`karma_scene_bake` tool and `.kscenebake.json` metadata for deterministic scene
bake fingerprints.

The major runtime change was moving texture work out of material prewarm:
`RenderSystem` now prewarms package meshes, batches unique texture
prepare/upload work, then prewarms materials after renderer texture handles are
already cached. Prepared texture upload blobs can also be stored in the asset
cache so KTX2 transcode work is skipped on later runs.

Best measured world-bake warm startup improved from 3906.64 ms on the compact
KTX package cache to 1657.84 ms on a prepared-upload cache hit. Texture batch
acquire dropped from 2537.83 ms to 530.59 ms.

The latest stall diagnosis is still open: the user's default cache was about
26 GB with 25,524 blob files and no prepared blobs, so the next run was a first
prepared-cache population run rather than a prepared-cache hit. A mitigation was
staged so prepared upload blobs use `AssetCache::writeTextureNoIndex(...)`
instead of repeatedly updating the large cache index, but that change was not
fully reverified before the build was stopped.

See [WORLD_BAKE_STARTUP_OPTIMIZATION.md](WORLD_BAKE_STARTUP_OPTIMIZATION.md)
for the complete record, measurements, commands, and open items.

## Visual Correctness Guard

Do not bind a raw environment cubemap as the PBR prefilter map or use a flat
color texture as the BRDF LUT. That shortcut makes reflective materials look
incorrect and can hide base-color textures. KTX cubemaps may skip equirectangular
conversion, but they still need valid generated IBL resources.
