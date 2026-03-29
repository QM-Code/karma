# Next Agent Bootstrap

This repo is in a fast-moving state. Prefer behavior-preserving refactors first, then tighten architecture once the split points are proven. Do not revert unrelated dirty worktree changes.

## Start Here

There are five active technical tracks in the current tree:

1. renderer monolith decomposition
2. particle/render performance
3. effect API split / prefab modularization
4. collision/contact/ground-state ECS work
5. local-light / point-shadow validation

Read these first:

- [docs/RENDERER_REFACTOR_BOOTSTRAP.md](docs/RENDERER_REFACTOR_BOOTSTRAP.md)
- [docs/PARTICLE_PERF_BOOTSTRAP.md](docs/PARTICLE_PERF_BOOTSTRAP.md)
- [docs/EFFECT_API_SPLIT_BOOTSTRAP.md](docs/EFFECT_API_SPLIT_BOOTSTRAP.md)
- [docs/EFFECT_API_SPLIT_PLAN.md](docs/EFFECT_API_SPLIT_PLAN.md)
- [docs/PREFAB_GALLERY_BOOTSTRAP.md](docs/PREFAB_GALLERY_BOOTSTRAP.md)
- [docs/COLLISION_BOOTSTRAP.md](docs/COLLISION_BOOTSTRAP.md)
- [docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md](docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md)
- [docs/LOCAL_LIGHT_PROBE_BOOTSTRAP.md](docs/LOCAL_LIGHT_PROBE_BOOTSTRAP.md)

## Worktree Warning

The worktree is intentionally dirty. Do not assume unrelated modified files are safe to revert.

High-signal areas right now:

- [`src/renderer/backends/diligent/backend_init.cpp`](src/renderer/backends/diligent/backend_init.cpp)
- [`src/renderer/backends/diligent/backend_render.cpp`](src/renderer/backends/diligent/backend_render.cpp)
- [`src/renderer/backends/diligent/passes/`](src/renderer/backends/diligent/passes)
- [`src/renderer/backends/diligent/resources/`](src/renderer/backends/diligent/resources)
- [`src/renderer/render_system.cpp`](src/renderer/render_system.cpp)
- [`src/particles/`](src/particles)
- [`src/beams/beam_path_system.cpp`](src/beams/beam_path_system.cpp)
- [`src/beams/beam_path_runtime_module.cpp`](src/beams/beam_path_runtime_module.cpp)
- [`src/volumes/volume_sphere_system.cpp`](src/volumes/volume_sphere_system.cpp)
- [`src/volumes/volume_sphere_runtime_module.cpp`](src/volumes/volume_sphere_runtime_module.cpp)
- [`src/prefabs/prefab_runtime.cpp`](src/prefabs/prefab_runtime.cpp)
- [`src/prefabs/prefab_entry_handler.cpp`](src/prefabs/prefab_entry_handler.cpp)
- [`include/karma/app/runtime_module.h`](include/karma/app/runtime_module.h)
- [`include/karma/prefabs/prefab_entry_handler.h`](include/karma/prefabs/prefab_entry_handler.h)
- [`src/physics/`](src/physics)
- [`src/collision/`](src/collision)
- [`examples/light_stress_example.cpp`](examples/light_stress_example.cpp)
- [`examples/collision_events_example.cpp`](examples/collision_events_example.cpp)

Local-only artifacts intentionally left out of source work:

- `build-local/`
- `build-check/`

## Build Commands

Verified in this worktree during the renderer split:

```bash
cmake -S . -B build-local
cmake --build build-local --target karma_light_stress_example -j2
```

Useful smoke checks in this environment:

```bash
timeout 5s ./build-local/karma_light_stress_example
timeout 5s ./build-local/karma_light_stress_example --help
```

Expected headless stop here:

```text
GLFW failed to initialize
```

That is normal in this environment. The useful signal is whether startup, asset loading, and system initialization succeed before that line.

For the effect API split specifically, this build was also verified:

```bash
cmake --build build-local --target \
  karma_laser_example \
  karma_laser_prefab_example \
  karma_volumetric_sphere_example \
  karma_volumetric_sphere_prefab_example \
  karma_prefab_gallery_example \
  -j2
```

## Renderer Summary

Recent renderer-structure work already in the tree:

- old `backend_mesh.cpp` was replaced by `resources/materials.cpp`, `resources/meshes.cpp`, `resources/render_targets.cpp`, and `resources/textures.cpp`
- low-risk `backend_render.cpp` lifecycle/state code moved to `passes/frame.cpp`, `passes/camera_override.cpp`, and `passes/render_state.cpp`
- environment, line-resource setup, and particle-resource setup moved to `passes/environment.cpp`, `passes/line.cpp`, and `passes/particles.cpp`
- shadow rendering moved to `passes/shadows.cpp`
- forward opaque/transparent batching moved to `passes/forward.cpp`
- particle draw batching and execution moved to `passes/particle_draw.cpp`
- `backend_render.cpp` now mostly owns frame orchestration, Forward+ setup, scene-copy decisions, an inline line pass, and present glue
- `backend_init.cpp` is now the largest remaining Diligent monolith

If continuing there, start with:

- [docs/RENDERER_REFACTOR_BOOTSTRAP.md](docs/RENDERER_REFACTOR_BOOTSTRAP.md)
- [docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md](docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md)

## Particle / Render Perf Summary

Recent particle-side work already in the tree:

- effect hot-reload polling throttled
- particle batches moved instead of copied
- additive particle batches grouped by render state
- cheaper compaction and curve evaluation in `ParticleSystem`
- GPU-side presentation for `ParticleSystem` batches:
  - size curve
  - alpha curve
  - color interpolation
  - atlas frame selection / UV generation
- beam-authored particles intentionally remain on the baked presentation path
- wave volume proxies now render as projected screen-bounds quads instead of near-full-screen overlays
- prefab gallery perf logging exists behind `KARMA_PREFAB_GALLERY_STATS=1`

If continuing there, start with:

- [docs/PARTICLE_PERF_BOOTSTRAP.md](docs/PARTICLE_PERF_BOOTSTRAP.md)
- [docs/PREFAB_GALLERY_BOOTSTRAP.md](docs/PREFAB_GALLERY_BOOTSTRAP.md)

## Collision / Physics Summary

Recent collision-side work already in the tree:

- overlap enter/stay/exit ECS events
- solid contact enter/stay/exit ECS events with point/normal
- grounded state with enter/exit
- support entity / point / normal for player controllers
- support probe for box rigid bodies
- sample scene with driving, jumping, trigger zones, and on-screen status

If continuing there, start with:

- [docs/COLLISION_BOOTSTRAP.md](docs/COLLISION_BOOTSTRAP.md)

## Local-Light / Shadow Summary

Recent local-light work already in the tree:

- Forward+ light-path fixes past the `8 -> 9` light transition
- point-shadow runtime light budgeting
- explicit `R32_FLOAT` SRVs for directional and point shadow-map arrays
- per-SRB shadow resource binding for actual material/default draw paths
- moving-point-light shadow refresh no longer chunks through the small cache threshold
- reduced point-shadow seam artifacts from hard cubemap-face boundaries
- light-stress sample staged for gradual `1-16` shadowed-light validation with moving markers

If continuing there, start with:

- [docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md](docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md)
- [docs/LOCAL_LIGHT_PROBE_BOOTSTRAP.md](docs/LOCAL_LIGHT_PROBE_BOOTSTRAP.md)

## Good Next Steps

If continuing renderer decomposition:

1. move the inline line draw path out of `backend_render.cpp`
2. split Forward+ setup and SRB binding out of `backend_render.cpp`
3. split `backend_init.cpp` by bootstrap responsibility and move inline shader strings out of it
4. only after that, consider breaking up `passes/forward.cpp` and `passes/shadows.cpp`

If continuing particle work:

1. measure alpha/distortion sort cost before another architecture change
2. explore bucketed or approximate depth ordering before GPU simulation
3. only move simulation fully GPU-side if the user wants a larger renderer rewrite

If continuing the effect API split:

1. read [docs/EFFECT_API_SPLIT_BOOTSTRAP.md](docs/EFFECT_API_SPLIT_BOOTSTRAP.md) first
2. treat runtime modularization as landed and stable enough to build on
3. the next real architecture gap is the orb shell / material pipeline split
4. parser/plugin decoupling for arbitrary prefab section kinds is still missing
5. do not regress the out-of-line runtime-module ctor/dtor setup unless you also change ownership away from forward-declared system pointers

If continuing collision/contact work:

1. extend grounded/support probing to sphere/capsule rigid bodies
2. validate contact event quality across more collider combinations
3. tighten docs once the API surface stabilizes

If continuing local-light/shadow work:

1. tune point-shadow quality and face update strategy
2. validate the shadow path across more scenes and hardware
3. only replace the current depth-SRV path with a custom linear-depth format if another backend shows the old failure mode

## Engineering Notes

- prefer `apply_patch` for edits
- prefer `rg` / `rg --files` for search
- do not revert unrelated dirty worktree changes
- the user values architecture and consistency more than compatibility
