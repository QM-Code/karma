# Next Agent Bootstrap

This repo is in a fast-moving state. Prefer behavior-preserving refactors first, then tighten architecture once the split points are proven. Do not revert unrelated dirty worktree changes.

## Start Here

There are seven active technical tracks in the current tree:

1. renderer monolith decomposition
2. particle/render performance
3. effect API split / prefab modularization
4. collision/contact/ground-state ECS work
5. local-light / point-shadow validation
6. GLB node animation / first-pass skeletal CPU skinning
7. static navmesh generation and pathfinding

Read these first:

- [docs/GLB_ANIMATION_BOOTSTRAP.md](docs/GLB_ANIMATION_BOOTSTRAP.md)
- [docs/NAVIGATION_BOOTSTRAP.md](docs/NAVIGATION_BOOTSTRAP.md)
- [docs/NAVIGATION.md](docs/NAVIGATION.md)
- [docs/RENDERER_REFACTOR_BOOTSTRAP.md](docs/RENDERER_REFACTOR_BOOTSTRAP.md)
- [docs/PARTICLE_PERF_BOOTSTRAP.md](docs/PARTICLE_PERF_BOOTSTRAP.md)
- [docs/PARTICLE_SYSTEM_ANALYSIS.md](docs/PARTICLE_SYSTEM_ANALYSIS.md)
- [docs/PARTICLE_EFFECT_GENERATION.md](docs/PARTICLE_EFFECT_GENERATION.md)
- [docs/EFFECT_API_SPLIT_BOOTSTRAP.md](docs/EFFECT_API_SPLIT_BOOTSTRAP.md)
- [docs/EFFECT_API_SPLIT_PLAN.md](docs/EFFECT_API_SPLIT_PLAN.md)
- [docs/PREFAB_GALLERY_BOOTSTRAP.md](docs/PREFAB_GALLERY_BOOTSTRAP.md)
- [docs/VOLUMETRIC_SPHERE_TRANSPARENCY.md](docs/VOLUMETRIC_SPHERE_TRANSPARENCY.md)
- [docs/COLLISION_BOOTSTRAP.md](docs/COLLISION_BOOTSTRAP.md)
- [docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md](docs/LOCAL_LIGHT_SHADOW_BOOTSTRAP.md)
- [docs/LOCAL_LIGHT_PROBE_BOOTSTRAP.md](docs/LOCAL_LIGHT_PROBE_BOOTSTRAP.md)

## Worktree Warning

The worktree is intentionally dirty. Do not assume unrelated modified files are safe to revert.

High-signal areas right now:

- [`src/rendering/renderer/backends/diligent/backend_init.cpp`](src/rendering/renderer/backends/diligent/backend_init.cpp)
- [`src/rendering/renderer/backends/diligent/backend_render.cpp`](src/rendering/renderer/backends/diligent/backend_render.cpp)
- [`src/rendering/renderer/backends/diligent/passes/`](src/rendering/renderer/backends/diligent/passes)
- [`src/rendering/renderer/backends/diligent/resources/`](src/rendering/renderer/backends/diligent/resources)
- [`src/rendering/renderer/render_system.cpp`](src/rendering/renderer/render_system.cpp)
- [`src/features/visual/particles/`](src/particles)
- [`src/features/visual/beams/beam_path_system.cpp`](src/features/visual/beams/beam_path_system.cpp)
- [`src/features/visual/beams/beam_path_runtime_module.cpp`](src/features/visual/beams/beam_path_runtime_module.cpp)
- [`src/features/visual/volumes/volume_sphere_system.cpp`](src/features/visual/volumes/volume_sphere_system.cpp)
- [`src/features/visual/volumes/volume_sphere_runtime_module.cpp`](src/features/visual/volumes/volume_sphere_runtime_module.cpp)
- [`src/content/prefabs/prefab_runtime.cpp`](src/content/prefabs/prefab_runtime.cpp)
- [`src/content/prefabs/prefab_entry_handler.cpp`](src/content/prefabs/prefab_entry_handler.cpp)
- [`include/karma/runtime/app/runtime_module.h`](include/karma/runtime/app/runtime_module.h)
- [`include/karma/content/prefabs/prefab_entry_handler.h`](include/karma/content/prefabs/prefab_entry_handler.h)
- [`src/simulation/physics/`](src/physics)
- [`src/simulation/collision/`](src/collision)
- [`src/simulation/animation/`](src/animation)
- [`include/karma/simulation/animation/`](include/karma/animation)
- [`src/content/importers/glb_scene_import.cpp`](src/content/importers/glb_scene_import.cpp)
- [`src/world/scene/transform_hierarchy.cpp`](src/world/scene/transform_hierarchy.cpp)
- [`include/karma/simulation/navigation/`](include/karma/navigation)
- [`src/simulation/navigation/`](src/navigation)
- [`examples/navmesh_example.cpp`](examples/navmesh_example.cpp)
- [`tests/navmesh_tests.cpp`](tests/navmesh_tests.cpp)
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

For GLB animation / first-pass skeletal skinning, these were verified:

```bash
cmake --build build --target karma_animation_tests -j2
./build/karma_animation_tests
ctest --test-dir build -R karma_animation_tests --output-on-failure
cmake --build build --target karma_glb_scene_import_example karma_glb_animation_example -j2
```

`karma_animation_tests` is headless and exits silently on success.

## GLB Animation / Skinning Summary

Recent animation-side work already in the tree:

- `LocalTransformComponent` was added for local scene hierarchy poses
- `TransformComponent` remains the final world transform used by render,
  physics, lights, particles, and audio
- `scene::updateWorldTransforms(...)` composes scene roots/children and writes
  world transforms
- GLB scene import now creates local and world transforms for imported nodes and
  primitive entities
- GLB animation clips are parsed from Assimp animation channels and stored on
  `GlbScenePrefab::animations`
- `AnimationPlayerComponent` supports clip storage, playback state, speed, loop,
  and helper functions for play/pause/stop and clip selection
- imported GLB roots autoplay clip `0` when clips exist unless
  `GlbSceneInstantiateOptions::autoplay_animations` is false
- first-pass skeletal support imports Assimp bones, vertex weights, joint node
  references, and inverse bind matrices into `SkinnedMeshComponent`
- `CpuSkinningSystem` deforms bind-pose mesh data on the CPU and uploads through
  `GraphicsDevice::updateMesh(...)`

Important limitations:

- no GPU skinning yet
- no retargeting between different skeletons
- no blending, state machine, root motion, or morph targets
- CPU skinning is a correctness path and can be expensive
- `MeshComponent.mesh_key = "model.glb"` remains the flat mesh path and does not
  use this scene animation/skinning path

If continuing there, start with:

- [docs/GLB_ANIMATION_BOOTSTRAP.md](docs/GLB_ANIMATION_BOOTSTRAP.md)
- [src/simulation/animation/AGENTS.md](src/simulation/animation/AGENTS.md)
- [include/karma/simulation/animation/AGENTS.md](include/karma/simulation/animation/AGENTS.md)

For the navigation work specifically, this build was verified:

```bash
cmake -S . -B build -DKARMA_FETCH_DEPS=ON -DKARMA_BUILD_RMLUI_DEMO=OFF
cmake --build build -j2
ctest --test-dir build -R karma_navmesh_tests --output-on-failure
```

The fully default build currently fails in this environment before all examples
complete because RmlUi 6.0 finds `/usr/local/lib/libfreetype.a` version 2.4.9,
which lacks newer FreeType symbols used by RmlUi. This is unrelated to the
navigation implementation; disabling only `KARMA_BUILD_RMLUI_DEMO` lets the rest
of the project, including `karma_navmesh_example`, build.

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
- analytic volumetric spheres now render post-particle, sort by real sphere center, and alpha-compose without erasing background spheres
- prefab gallery perf logging exists behind `KARMA_PREFAB_GALLERY_STATS=1`

If continuing there, start with:

- [docs/PARTICLE_SYSTEM_ANALYSIS.md](docs/PARTICLE_SYSTEM_ANALYSIS.md)
- [docs/PARTICLE_EFFECT_GENERATION.md](docs/PARTICLE_EFFECT_GENERATION.md)
- [docs/PARTICLE_PERF_BOOTSTRAP.md](docs/PARTICLE_PERF_BOOTSTRAP.md)
- [docs/PREFAB_GALLERY_BOOTSTRAP.md](docs/PREFAB_GALLERY_BOOTSTRAP.md)
- [docs/VOLUMETRIC_SPHERE_TRANSPARENCY.md](docs/VOLUMETRIC_SPHERE_TRANSPARENCY.md)

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

## Navigation Summary

Recent navigation work already in the tree:

- Recast/Detour dependency integration behind `KARMA_ENABLE_NAVIGATION`
- static navmesh bake API in `include/karma/simulation/navigation/nav_mesh.h`
- GLB prefab, ECS mesh-collider, and direct mesh geometry collection in `nav_geometry`
- Detour path, nearest-point, and raycast query wrapper
- navmesh/path debug drawing through existing renderer line drawing
- minimal rendered `karma_navmesh_example` with a baked procedural navmesh and visible sample path
- `karma_navmesh_tests` covering bake/query/failure/transform/detour cases
- `NavMeshAgentComponent` as a placeholder state container only

If continuing there, start with:

- [docs/NAVIGATION_BOOTSTRAP.md](docs/NAVIGATION_BOOTSTRAP.md)
- [docs/NAVIGATION.md](docs/NAVIGATION.md)

## Good Next Steps

If continuing renderer decomposition:

1. move the inline line draw path out of `backend_render.cpp`
2. split Forward+ setup and SRB binding out of `backend_render.cpp`
3. split `backend_init.cpp` by bootstrap responsibility and move inline shader strings out of it
4. only after that, consider breaking up `passes/forward.cpp` and `passes/shadows.cpp`

If continuing particle work:

1. read [docs/PARTICLE_SYSTEM_ANALYSIS.md](docs/PARTICLE_SYSTEM_ANALYSIS.md)
2. add stable benchmark logging for explosion stress and prefab gallery
3. measure alpha/distortion sort cost before another architecture change
4. explore bucketed or approximate depth ordering before GPU simulation
5. consider particle material/state IDs before adding more renderer state fields to emitters
6. only move simulation fully GPU-side if the user wants a larger renderer rewrite

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

If continuing GLB animation/skinning work:

1. add a windowed skeletal visual sample with a tiny generated or checked-in GLB
2. validate a real Blender-authored skinned GLB through scene import
3. decide whether CPU skinning stays as debug/fallback or is replaced by GPU skinning
4. if implementing GPU skinning, update forward and shadow passes together
5. keep node animation, hierarchy composition, and skinning tests green

If continuing navigation work:

1. add a `NavigationSystem` or runtime module that owns a navmesh and consumes `NavMeshAgentComponent`
2. add path-following behavior before adding DetourCrowd
3. add navmesh serialization if startup rebake cost becomes visible
4. postpone tiled/dynamic navmesh work until there is a real large-world or obstacle-carving use case

## Engineering Notes

- prefer `apply_patch` for edits
- prefer `rg` / `rg --files` for search
- do not revert unrelated dirty worktree changes
- the user values architecture and consistency more than compatibility
