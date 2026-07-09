# World Bake Startup Optimization Notes

## Scope

This note records the July 2026 pass around the `world.glb` scene, offline
scene bake metadata, renderer warm-up diagnostics, and texture-cache hit
performance. The working benchmark was the `scene_world_bake` example with the
packaged GLB scene and skybox environment.

The goal was to make a warm package/cache hit fast and observable. The no-index
prepared-cache write and texture-minification fixes have now been rebuilt and
reverified.

## World Bake Example

The dropped `world.glb` was moved into the example asset tree:

- `examples/assets/scene/world_bake/world.glb`
- `examples/assets/scene/world_bake/golden_gate_hills_512_cubemap.ktx`
- `examples/assets/scene/world_bake/assets.package.json`
- `examples/assets/scene/world_bake/world_bake.kscene.json`
- `examples/assets/scene/world_bake/world_bake.kscenebake.json`
- `examples/assets/scene/world_bake/scene.package.json`
- `examples/scene/world_bake.cpp`

`assets.package.json` registers the skybox as an `environment_map` and the GLB
as a `gltf_scene` with both `import_meshes` and `import_lights` enabled. The
example target is `scene_world_bake`; the executable output is
`examples/scene/world_bake`.

The world package opts into `alpha_mode_policy: "auto_cutout"` so authored
`BLEND` materials are classified from their base-color alpha data during bake:
nearly opaque textures are imported as opaque, and foliage-style cutout textures
are imported as masked. This avoids treating trees, grass cards, and mostly
opaque tent/wood textures as sorted translucent glass.

The large grass ground plane uses a material override with `casts_shadows:
false`. It still receives lighting and shadows, but no longer writes into the
directional shadow map. This avoids camera-dependent cascaded-shadow
self-shadowing from the 64 x 64 unit coplanar receiver while preserving shadows
from tents, trees, and props.

The GLB currently contains authored light data. The example imports lights
through the package path and logs scene/light counts so the importer path is
visible once the light issue is fixed.

## Texture Minification Fix

The prepared upload cache used to clear `TextureDesc::generate_mips` when it
wrote an RGBA8 derivative and again when it restored that derivative. The warm
path therefore uploaded only mip 0 for textures such as the 2048 x 2048 grass
normal map, producing severe grain and shimmer in the distance.

Prepared uploads now preserve generated-mip intent across both steps. The v2
prepared key includes that flag, which prevents reuse of the bad v1 blobs. The
Diligent data sampler also follows the configured anisotropic filtering level,
so normal, roughness, metallic, and occlusion maps receive the same grazing-angle
minification quality as color maps.

## Known Issues

- The terrain/grass, tent-wall, and foliage materials in the world-bake scene
  can still read too glossy or glassy under the current lighting. This remains
  after the alpha-import fixes, tangent-basis correction, reduced terrain
  `normal_scale`, and RGB-preserving normal-map KTX2 path. The remaining
  material-response issue is separate from the fixed warm-cache minification
  defect and needs a focused material/normal-map convention pass.

## Offline Bake Tool

The pass added the `karma_scene_bake` tool target. It reads a `.kscene.json`
scene document, selects a bake entry, and writes deterministic bake metadata to
a `.kscenebake.json` file. With `--bake-packages`, it also writes portable
asset package blobs under `bakes/asset_cache/<asset_package_id>`. It supports
`--check` for stale-bake detection.

Typical commands:

```bash
cmake --build build/portable --target karma_scene_bake scene_world_bake

./build/portable/karma_scene_bake \
  --bake-packages \
  --output examples/assets/scene/world_bake/world_bake.kscenebake.json \
  examples/assets/scene/world_bake/world_bake.kscene.json

./build/portable/karma_scene_bake --check \
  --bake-packages \
  --output examples/assets/scene/world_bake/world_bake.kscenebake.json \
  examples/assets/scene/world_bake/world_bake.kscene.json
```

The current world-bake metadata records the scene fingerprint, source
dependency hashes, static component list, and empty baked-lighting attachment
data. It is a stable contract for future bake products rather than a lightmap
generator.

## Runtime Diagnostics

Useful warm-start command:

```bash
timeout 20s env \
  KARMA_ENGINE_STARTUP_DIAG=1 \
  KARMA_RENDER_SYSTEM_DIAG=1 \
  KARMA_RENDER_RESOURCE_DIAG=1 \
  KARMA_RENDER_PIPELINE_DIAG=1 \
  KARMA_RENDER_TEXTURE_IMPORT_DIAG=1 \
  ./build/portable/examples/scene/world_bake
```

The example is an interactive app, so it normally keeps running after startup.
Use `timeout` for benchmark-style runs; an exit code from `timeout` after the
startup logs does not by itself mean startup failed.

Useful controls for this pass:

- `KARMA_ASSET_CACHE_DIR=/path/to/cache`: isolate cache experiments.
- `KARMA_ASSET_CACHE_FLUSH=1`: force a cold package/cache rebuild.
- `KARMA_TEXTURE_BC7=1`: enable BC7 runtime upload selection when supported.
- `KARMA_RENDER_TEXTURE_PREPARED_CACHE=0`: disable prepared upload cache reads
  and writes.
- `KARMA_ENGINE_PREWARM_STARTUP_PACKAGES=0`: disable startup package GPU
  prewarm for comparison.
- `KARMA_TEXTURE_KTX2_THREADS=N`: cap KTX2 import compression workers.
- `KARMA_TEXTURE_KTX2_UASTC_LEVEL=N`: choose KTX2 UASTC encode level.
- `KARMA_TEXTURE_KTX2_KEEP_FALLBACK=1`: keep RGBA fallback bytes next to KTX2.
- `KARMA_ASSET_TEXTURE_RESTORE_JOBS=N`: cap parallel texture restore jobs.

## Build Configuration Finding

We checked the suspicion that Diligent might still be compiled with a slow
Release configuration. A helper option exists:

```text
KARMA_DILIGENT_STRIP_RELEASE_O1
```

It removes Diligent's GCC Release `-O1` interface option so the normal CMake
Release optimization flags can apply. Local testing showed this made startup
worse for the GCC build being measured, so the option defaults to `OFF`. Do
not flip it as a default optimization without remeasuring the target toolchain.

## Implemented Optimizations

Package/cache identity:

- Package cache keys now include the texture import/profile state and relevant
  dependency tags so warm hits invalidate when texture payload policy changes.
- Texture restore scheduling is capped with `KARMA_ASSET_TEXTURE_RESTORE_JOBS`
  and defaults to at most 4 parallel jobs.

Texture import:

- KTX2 import now builds an explicit mip chain and disables libktx mip
  generation before UASTC compression.
- UASTC compression defaults to the fastest level.
- KTX2 import defaults to compressed-only payloads. The RGBA fallback is kept
  only when `KARMA_TEXTURE_KTX2_KEEP_FALLBACK=1`.
- `KARMA_TEXTURE_BC7` now defaults to enabled for KTX2 textures without an RGBA
  fallback, assuming the renderer reports BC7 support.
- The texture importer/profile versions were bumped so stale cache records are
  rebuilt.

Render prewarm:

- Startup prewarm now acquires meshes first, then batches unique textures from
  packages and materials, then prewarms materials after renderer texture handles
  are already cached.
- `GraphicsDevice::createAndUploadTextures(...)` creates and uploads a batch of
  prepared texture payloads with one renderer-facing call.
- CPU-side texture preparation is parallelized with `std::async` in batches of
  16 before the render-thread upload batch is submitted.
- Material prewarm no longer repeatedly pays texture acquisition costs for the
  same package textures.

Prepared texture upload cache:

- `TextureAsset::PayloadFormat::PreparedUpload` stores backend-ready upload
  bytes in the asset cache.
- `preparedTextureUploadCacheKey(...)` keys prepared uploads by the source
  texture, generated-mip intent, and runtime format capabilities.
- `RenderSystem` first tries to read a prepared upload blob, then falls back to
  preparing/transcoding from the imported texture asset, then writes the
  prepared blob for the next run.
- RGBA8 prepared blobs retain `generate_mips` even when their stored upload
  contains only mip 0, allowing the backend to build the remaining levels.
- Diagnostics log `prepared_cache`, `cache_read_ms`, `prepare_ms`,
  `cache_write_ms`, and upload timing per texture batch.

Pipeline/render-state:

- Existing Diligent render-state cache diagnostics were used to verify shader
  cache behavior.
- The data-texture sampler uses the configured anisotropic filter, matching the
  color sampler's minification quality.
- For the world-bake path, the main variability was texture preparation and
  Diligent startup/warm-up rather than late pipeline creation. Validation runs
  showed the forward pipeline variants needed by the scene were already covered
  by warm-up.

## Measurements

Baseline after restoring the Diligent `-O1` default:

| Measurement | Time |
| --- | ---: |
| Startup through warm-up | 3848.28 ms |
| Startup scene assets | 621.61 ms |
| Startup asset prewarm | 2133.36 ms |
| Material prewarm | 1980.91 ms |
| Renderer warm-up | 603 ms |

Material breakdown before texture batch prewarm:

| Measurement | Time |
| --- | ---: |
| Material total | 1942.34 ms |
| Texture acquisition in materials | 1932.66 ms |
| Texture total | 1923.50 ms |
| Texture upload | 1604.99 ms |
| Texture misses / hits | 91 / 112 |

After initial texture batch prewarm:

| Measurement | Time |
| --- | ---: |
| Startup through warm-up | 2795.15 ms |
| Startup asset prewarm | 1291.03 ms |
| Texture batch acquire | 1139.54 ms |
| Material prewarm | 7.34 ms |
| Renderer warm-up | 559.01 ms |

Skybox/environment KTX path:

| Measurement | Before | After |
| --- | ---: | ---: |
| Environment warm-up | ~292-303 ms | ~95-104 ms |

Some later runs measured environment warm-up around 27 ms, but the more
repeatable improvement was roughly 300 ms to roughly 100 ms.

Compressed-only KTX before prepared upload caching:

| Measurement | Time |
| --- | ---: |
| Startup through warm-up | 9180.06 ms |
| Texture prewarm | 7898.66 ms |

The regression came from serial KTX transcode work on warm package hits. After
parallel preparation, compact-cache hits measured:

| Run | Startup through warm-up | Texture batch acquire | Renderer warm-up |
| --- | ---: | ---: | ---: |
| Best compact hit | 3906.64 ms | 2537.83 ms | 230.78 ms |
| Noisy compact hit | 8558.96 ms | 5505.13 ms | 714.34 ms |

Prepared upload cache, using `/tmp/karma_asset_cache_ktx_parallel`:

| Run | Startup through warm-up | Texture batch acquire | Startup prewarm | Renderer warm-up |
| --- | ---: | ---: | ---: | ---: |
| Populate prepared cache | 5793.40 ms | 4053.21 ms | 4220.66 ms | 275.00 ms |
| Prepared hit 1 | 1725.79 ms | 531.16 ms | 702.52 ms | 374.91 ms |
| Prepared hit 2 | 1657.84 ms | 530.59 ms | 713.28 ms | 320.17 ms |

The compact cache grew from about 300 MB to about 749 MB after writing 91
prepared upload blobs. Against the best compact-cache hit before prepared
uploads, the best prepared hit saved 2248.80 ms, about 58 percent. Texture
batch acquire alone dropped by about 2007 ms.

## Current Stall Diagnosis

The reported stall happened against the default asset cache, not the smaller
`/tmp` cache used for the prepared-cache measurements. The default cache was:

```text
~/.cache/karma/assets: about 26G
blob files: 25,524
prepared upload blobs: 0
```

That means the next run was not actually a prepared-cache hit for the world
textures. It was the first prepared-cache population run against a very large
cache. The likely stall was repeated `index.json` parse/update/write work:
writing 91 prepared blobs called the indexed texture write path for each blob,
and those writes happened from the parallel preparation workers.

The staged mitigation is:

- `AssetCache::writeTextureNoIndex(...)` writes a texture blob atomically
  without touching the cache index.
- Prepared upload writes in `RenderSystem` now use `writeTextureNoIndex(...)`.

This mitigation reduces first-populate work for prepared texture blobs. The
build, rendering tests, and a graphical world-bake cache-population run have now
completed; broader default-cache size and index policy remain open items.

## Last Known Verification

Before the `writeTextureNoIndex(...)` stall mitigation:

```bash
cmake --build build/portable --target scene_world_bake karma_rendering_tests -j 8
ctest --test-dir build/portable -R karma_rendering_tests --output-on-failure
```

Both passed.

After the no-index and generated-mip fixes:

```bash
cmake --build build/portable --target karma_rendering_tests scene_world_bake --parallel 4
ctest --test-dir build/portable -R '^karma_rendering_tests$' --output-on-failure
```

Both targets built successfully and `karma_rendering_tests` passed on July 9,
2026. A graphical world-bake smoke run created the new prepared grass-normal
entry with `generate_mips=1`; its old v1 counterpart had the same mip-0 payload
but `generate_mips=0`.

## Open Items

- Reproduce the user's default-cache first-populate run and confirm the stall is
  gone or expose the next blocking stage in diagnostics.
- Decide whether prepared upload cache should remain enabled by default given
  the cache-size tradeoff.
- Consider a better cache index policy for generated derivative blobs instead
  of skipping all index updates forever.
- Recheck GLB authored-light import once the current light issue is fixed.
- Add persistent environment-map blobs if skybox/environment import becomes a
  repeatable warm-start hotspot.
