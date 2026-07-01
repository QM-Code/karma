# Startup Diagnostics

Karma startup timing is gated by environment variables so normal runs stay quiet.

## Engine Startup Timeline

Set `KARMA_ENGINE_STARTUP_DIAG=1` to emit the startup timeline and nested engine
startup probes.

Example with the PF2e game:

```bash
KARMA_ENGINE_STARTUP_DIAG=1 \
KARMA_NAVMESH_DIAG=1 \
timeout 20s ./build/dev-scene-karma-local/pf2e_navmesh_scene
```

The log uses these high-level sections:

- `Engine startup timeline`: ordered `EngineApp::start` stages with `start_ms`,
  stage `ms`, and cumulative `total_ms`.
- `runtime_init`: subsystem creation, including window, graphics device, renderer,
  physics, navigation, and audio.
- `diligent_backend` / `diligent_device`: renderer backend initialization.
- `asset_package`: package manifest, cache, import, commit, and cache write stages.
- `asset_package_cache_asset`: one line per asset restored from an asset package
  cache hit.
- `asset_package_entry`: one line per package entry, including type, key, source,
  and elapsed time.
- `asset_source_import`: source-level mesh, texture, and glTF import phases.
- `gltf_scene_load`: Assimp read, material metadata, node traversal, glTF JSON
  metadata, skinning, and animation import phases.
- `navigation_rebuild`: ECS-level navmesh rebuild geometry collection and build
  timing.
- `nav_mesh_build`: Recast/Detour solo navmesh build phases.
- `RenderSystem prewarm`: GPU resource acquisition counts and timings.
- `game_onStart`: time spent inside the game callback versus splash wait-loop
  overhead.

## Useful Extra Probes

- `KARMA_RENDER_RESOURCE_DIAG=1`: renderer resource creation timings.
- `KARMA_RENDER_PIPELINE_DIAG=1`: renderer pipeline/shader creation timings.
- `KARMA_RENDER_LAYER_FRAME_DIAG=1`: first-frame render layer timings.
- `KARMA_NAVMESH_DIAG=1`: navigation request and rebuild diagnostics.

## Reading A Slow Startup

Start with the largest `Engine startup timeline` stages. If `game onStart` is
dominant, compare it with the nested `asset_package`, `asset_source_import`, and
`gltf_scene_load` lines. If `renderer warm-up` or `startup asset prewarm` is
dominant, inspect `RenderSystem prewarm`, `diligent_device`, resource, and
pipeline diagnostics.

For cache analysis, compare `asset_package` totals:

- `total cache hit`: package restored from the asset cache.
- `total source import`: package imported from source files and then written to
  the cache.
