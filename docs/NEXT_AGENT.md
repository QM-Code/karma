# Current Agent Handoff

This repo is in a fast-moving state. Prefer behavior-preserving refactors first,
then tighten architecture once the split points are proven. Check the worktree
before editing and do not revert unrelated user changes.

## Start Here

This file is the consolidated agent handoff. The old one-off handoff and plan
docs under `docs/` were folded here or removed after completion.

There are seven active technical tracks in the current tree:

1. renderer monolith decomposition
2. particle/render performance
3. effect API split / prefab modularization
4. collision/contact/ground-state ECS work
5. local-light / point-shadow validation
6. gltf node animation / skeletal skinning / morph targets
7. static navmesh generation and pathfinding

Durable reference docs:

- [docs/ARCHITECTURE.md](ARCHITECTURE.md)
- [docs/ENGINE_USAGE.md](ENGINE_USAGE.md)
- [docs/ENGINE_IMPLEMENTATION.md](ENGINE_IMPLEMENTATION.md)
- [docs/NAVIGATION.md](NAVIGATION.md)
- [docs/PARTICLE_SYSTEM.md](PARTICLE_SYSTEM.md)
- [docs/PARTICLE_EFFECT_GENERATION.md](PARTICLE_EFFECT_GENERATION.md)
- [docs/EFFECT_PREFABS.md](EFFECT_PREFABS.md)
- [docs/BEAM_PATHS.md](BEAM_PATHS.md)
- [docs/EXPLOSION_PREFAB.md](EXPLOSION_PREFAB.md)
- [docs/EXPLOSION_STRESS_PERF.md](EXPLOSION_STRESS_PERF.md)
- [docs/VOLUMETRIC_SPHERE_TRANSPARENCY.md](VOLUMETRIC_SPHERE_TRANSPARENCY.md)
- [docs/ANIMATION_V2.md](ANIMATION_V2.md)
- [docs/ASSET_PIPELINE_CLEANUP.md](ASSET_PIPELINE_CLEANUP.md)
- [docs/RIGGED_GLTF_AUTHORING.md](RIGGED_GLTF_AUTHORING.md)
- [docs/DEBUG_EDITOR.md](DEBUG_EDITOR.md) if changing the runtime debug editor

## Worktree Caution

Do not assume unrelated modified files are safe to revert.

High-signal areas right now:

- [`src/rendering/renderer/backends/diligent/backend_init.cpp`](../src/rendering/renderer/backends/diligent/backend_init.cpp)
- [`src/rendering/renderer/backends/diligent/backend_render.cpp`](../src/rendering/renderer/backends/diligent/backend_render.cpp)
- [`src/rendering/renderer/backends/diligent/passes/`](../src/rendering/renderer/backends/diligent/passes)
- [`src/rendering/renderer/backends/diligent/resources/`](../src/rendering/renderer/backends/diligent/resources)
- [`src/rendering/renderer/render_system.cpp`](../src/rendering/renderer/render_system.cpp)
- [`src/features/visual/particles/`](../src/features/visual/particles)
- [`src/features/visual/beams/beam_path_system.cpp`](../src/features/visual/beams/beam_path_system.cpp)
- [`src/features/visual/beams/beam_path_runtime_module.cpp`](../src/features/visual/beams/beam_path_runtime_module.cpp)
- [`src/features/visual/volumes/volume_sphere_system.cpp`](../src/features/visual/volumes/volume_sphere_system.cpp)
- [`src/features/visual/volumes/volume_sphere_runtime_module.cpp`](../src/features/visual/volumes/volume_sphere_runtime_module.cpp)
- [`src/content/prefabs/prefab_runtime.cpp`](../src/content/prefabs/prefab_runtime.cpp)
- [`src/content/assets/asset_package.cpp`](../src/content/assets/asset_package.cpp)
- [`src/content/prefabs/component_serializer_registry.cpp`](../src/content/prefabs/component_serializer_registry.cpp)
- [`include/karma/runtime/app/runtime_module.h`](../include/karma/runtime/app/runtime_module.h)
- [`include/karma/content/assets/asset_package.h`](../include/karma/content/assets/asset_package.h)
- [`include/karma/content/prefabs/component_serializer_registry.h`](../include/karma/content/prefabs/component_serializer_registry.h)
- [`src/simulation/physics/`](../src/simulation/physics)
- [`src/simulation/collision/`](../src/simulation/collision)
- [`src/simulation/animation/`](../src/simulation/animation)
- [`include/karma/simulation/animation/`](../include/karma/simulation/animation)
- [`src/content/importers/gltf_scene_import.cpp`](../src/content/importers/gltf_scene_import.cpp)
- [`src/world/scene/transform_hierarchy.cpp`](../src/world/scene/transform_hierarchy.cpp)
- [`include/karma/simulation/navigation/`](../include/karma/simulation/navigation)
- [`src/simulation/navigation/`](../src/simulation/navigation)
- [`examples/navigation/navmesh.cpp`](../examples/navigation/navmesh.cpp)
- [`tests/navmesh_tests.cpp`](../tests/navmesh_tests.cpp)
- [`examples/light_stress_example.cpp`](../examples/light_stress_example.cpp)
- [`examples/collision_events_example.cpp`](../examples/collision_events_example.cpp)

Local-only artifacts intentionally left out of source work:

- `build-local/`
- `build-check/`

## Build Commands

Verified in this worktree during the renderer split:

```bash
cmake -S . -B build-local
cmake --build build-local --target rendering_light_stress -j2
```

Useful smoke checks in this environment:

```bash
timeout 5s ./build-local/examples/rendering/light_stress
timeout 5s ./build-local/examples/rendering/light_stress --help
```

Expected headless stop here:

```text
GLFW failed to initialize
```

That is normal in this environment. The useful signal is whether startup, asset loading, and system initialization succeed before that line.

For the effect API split specifically, this build was also verified:

```bash
cmake --build build-local --target \
  effects_laser \
  prefabs_laser \
  effects_volumetric_sphere \
  prefabs_volumetric_sphere \
  prefabs_gallery \
  -j2
```

For glTF animation / deformation, these were verified:

```bash
cmake --build build --target karma_animation_tests -j2
./build/karma_animation_tests
ctest --test-dir build -R karma_animation_tests --output-on-failure
cmake --build build --target scene_gltf_import animation_gltf -j2
```

`karma_animation_tests` is headless and exits silently on success.

## glTF Animation / Deformation Summary

Recent animation-side work already in the tree:

- `LocalTransformComponent` was added for local scene hierarchy poses
- `TransformComponent` remains the final world transform used by render,
  physics, lights, particles, and audio
- `scene::updateWorldTransforms(...)` composes scene roots/children and writes
  world transforms
- glTF scene import now creates local and world transforms for imported nodes and
  primitive entities
- glTF animation clips are parsed from explicit glTF channels when available and
  fall back to Assimp channels
- `AnimatorComponent` supports clip storage, playback state, speed, loop,
  state machines, transform sampling, morph-weight sampling, events, root
  motion, and helper functions for play/pause/stop and clip selection
- imported glTF roots autoplay clip `0` when clips exist unless
  `GltfSceneInstantiateOptions::autoplay_animations` is false
- skeletal support imports vertex weights, joint node references, skins,
  skeletons, and inverse bind matrices into `DeformableMeshComponent`
- imported skinned and morphed primitives use GPU deformation by default; CPU
  reference deformation remains available for diagnostics
- morph target deltas and mesh default weights import into `MeshData` and
  `DeformableMeshComponent`
- `DeformationSystem` is now the mesh deformation upload point: it builds joint
  palettes, updates renderer-owned deformation resources, and performs CPU
  reference deformation when selected
- explicit skeleton retargeting is available through `SkeletonMap` and
  `retargetClip(...)`
- `animation_gltf` has an ImGui clip/transition/deformation/root
  motion panel and defaults to
  `examples/assets/animation_model/source/dustbound_wayfarer_merged_animations.glb`

Important limitations:

- no humanoid semantic retarget profiles yet; use explicit skeleton maps
- no standalone animation-library asset format or humanoid profile binding
  layer yet
- `MeshComponent.mesh_asset_key` remains a registered mesh asset key. Import sources with
  `AssetRegistry::importMeshAsset(...)`, then assign the key; this flat mesh path does not
  use the scene animation/skinning path.

If continuing there, start with:

- [docs/ANIMATION_V2.md](ANIMATION_V2.md)
- [docs/RIGGED_GLTF_AUTHORING.md](RIGGED_GLTF_AUTHORING.md)
- [src/simulation/animation/AGENTS.md](../src/simulation/animation/AGENTS.md)
- [include/karma/simulation/animation/AGENTS.md](../include/karma/simulation/animation/AGENTS.md)

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
of the project, including `navigation_navmesh`, build.

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

- [docs/ENGINE_USAGE.md](ENGINE_USAGE.md)
- [docs/VOLUMETRIC_SPHERE_TRANSPARENCY.md](VOLUMETRIC_SPHERE_TRANSPARENCY.md)
- [src/rendering/renderer/backends/diligent/passes/AGENTS.md](../src/rendering/renderer/backends/diligent/passes/AGENTS.md)

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
- normal `.kpeffect` emitters now use persistent Diligent GPU state by default:
  - emitter descriptor/state buffers
  - particle state buffers with alive/dead list output
  - GPU compaction to the existing packed instance vertex layout
  - grouped alpha/distortion depth-key sorting
  - GPU-written indirect draw arguments
  - one-frame-delayed async stats readback
- particle GPU diagnostics now report allocator live/free/high-water state,
  retired/reused/failure counts, GPU emitter/particle culling, culling dispatch
  counts, and explicit global-sort versus grouped-sort fallback flags
- the Diligent Vulkan particle path now requests descriptor-indexed resource
  arrays, binds a fixed particle texture table plus material-record buffer, and
  uses that path for global alpha/distortion sorting when available
- the older analytic GPU instance bridge remains as a fallback when indirect
  draw or required UAV resources are unavailable and reports
  `gpu_fallback_active`
- beam-authored particles intentionally remain on the baked presentation path
- wave volume proxies now render as projected screen-bounds quads instead of near-full-screen overlays
- analytic volumetric spheres now render post-particle, sort by real sphere center, and alpha-compose without erasing background spheres
- prefab gallery perf logging exists behind `KARMA_PREFAB_GALLERY_STATS=1`
- direct-load staged explosion prefabs use `assets.package.json` packages for
  prefab-local atlas textures and effect files
- `particles_explosion_stress` supports up to 128 staged explosion
  controllers; 128 is the current stress acceptance target
- EXR source folders for the explosion are reference assets only, not runtime
  dependencies

2026-06-03 prefab visual session notes:

- explosion flipbooks use committed runtime PNG atlases baked from authored EXR
  source sequences; OpenEXR files remain source assets and the runtime prefab
  resource path still uploads `RGBA8`
- the bake utility is
  [examples/assets/prefabs/explosion/source/bake_exr_flipbooks.py](../examples/assets/prefabs/explosion/source/bake_exr_flipbooks.py)
- energy orb shells use a dedicated smooth transparent mesh at
  [examples/assets/orb_shell.glb](../examples/assets/orb_shell.glb)
- prefab gallery orb variants tint the shell material and use more saturated
  particle start/end colors so red, blue, green, and purple read clearly
- wave and volumetric-sphere prefabs currently use `center_opacity = 0.62`
- prefab tests use always-on `KARMA_REQUIRE` checks and validate sidecar texture
  upload metadata, including the EXR-derived flipbook atlas sizes

Validation for that pass:

```bash
cmake --build build --target karma_prefab_tests prefabs_gallery effects_energy_orb effects_wave prefabs_volumetric_sphere particles_explosion_stress -j2
ctest --test-dir build -R karma_prefab_tests --output-on-failure
git diff --check
```

If continuing there, start with:

- [docs/PARTICLE_SYSTEM.md](PARTICLE_SYSTEM.md)
- [docs/PARTICLE_EFFECT_GENERATION.md](PARTICLE_EFFECT_GENERATION.md)
- [docs/EXPLOSION_PREFAB.md](EXPLOSION_PREFAB.md)
- [docs/EXPLOSION_STRESS_PERF.md](EXPLOSION_STRESS_PERF.md)
- [docs/VOLUMETRIC_SPHERE_TRANSPARENCY.md](VOLUMETRIC_SPHERE_TRANSPARENCY.md)

## Collision / Physics Summary

Recent collision-side work already in the tree:

- overlap enter/stay/exit ECS events
- solid contact enter/stay/exit ECS events with point/normal
- grounded state with enter/exit
- support entity / point / normal for player controllers
- support probe for box rigid bodies
- sample scene with driving, jumping, trigger zones, and on-screen status

If continuing there, start with:

- [examples/collision_events_example.cpp](../examples/collision_events_example.cpp)
- [src/simulation/collision/](../src/simulation/collision)
- [include/karma/world/components/collision_events.h](../include/karma/world/components/collision_events.h)
- [include/karma/world/components/contact_events.h](../include/karma/world/components/contact_events.h)
- [include/karma/world/components/ground_contact.h](../include/karma/world/components/ground_contact.h)

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

- [docs/ENGINE_USAGE.md](ENGINE_USAGE.md)
- [examples/light_stress_example.cpp](../examples/light_stress_example.cpp)
- [src/rendering/renderer/backends/diligent/passes/shadows.cpp](../src/rendering/renderer/backends/diligent/passes/shadows.cpp)

## Navigation Summary

Recent navigation work already in the tree:

- Recast/Detour dependency integration behind `KARMA_ENABLE_NAVIGATION`
- static navmesh bake API in `include/karma/simulation/navigation/nav_mesh.h`
- GLB prefab, ECS mesh-collider, and direct mesh geometry collection in `nav_geometry`
- Detour path, nearest-point, and raycast query wrapper
- navmesh/path debug drawing through existing renderer line drawing
- minimal rendered `navigation_navmesh` with a baked procedural navmesh and visible sample path
- `karma_navmesh_tests` covering bake/query/failure/transform/detour cases
- `NavMeshAgentComponent` as a placeholder state container only

If continuing there, start with:

- [docs/NAVIGATION.md](NAVIGATION.md)
- [examples/navigation/navmesh.cpp](../examples/navigation/navmesh.cpp)
- [tests/navmesh_tests.cpp](../tests/navmesh_tests.cpp)

## Good Next Steps

If continuing renderer decomposition:

1. move the inline line draw path out of `backend_render.cpp`
2. split Forward+ setup and SRB binding out of `backend_render.cpp`
3. split `backend_init.cpp` by initialization responsibility and move inline shader strings out of it
4. only after that, consider breaking up `passes/forward.cpp` and `passes/shadows.cpp`

If continuing particle work:

1. use [docs/EXPLOSION_STRESS_PERF.md](EXPLOSION_STRESS_PERF.md) as the
   measurement guide
2. capture `KARMA_PREFAB_GALLERY_STATS=1` output on a stable windowing session
3. compare against `particles_explosion_stress --disable heat` if heat
   distortion remains a suspect
4. inspect grouped GPU sort cost and sort-key counts before changing sort
   strategy
5. consider bindless or texture-array particle materials if exact
   cross-material transparent ordering becomes necessary
6. consider particle material/state IDs before adding more renderer state
   fields to emitters
7. validate moving-emitter local-space behavior visually before relying on it
   for gameplay-critical effects

If continuing the effect API split:

1. treat runtime modularization as landed and stable enough to build on
2. keep runtime-module constructors/destructors out of line while public headers
   forward-declare private system types
3. remember that prefab loading now deserializes component payloads by component
   type name; old prefab section handlers and entry enum values are gone
4. the component serializer registry is still process-global state
5. the next real architecture gaps are scoped component serializer ownership,
   schema validation, and the orb shell / material pipeline split

If continuing collision/contact work:

1. extend grounded/support probing to sphere/capsule rigid bodies
2. validate contact event quality across more collider combinations
3. tighten docs once the API surface stabilizes

If continuing local-light/shadow work:

1. tune point-shadow quality and face update strategy
2. validate the shadow path across more scenes and hardware
3. only replace the current depth-SRV path with a custom linear-depth format if another backend shows the old failure mode

If continuing glTF animation/deformation work:

1. add a windowed skeletal visual sample with a tiny generated or checked-in GLB
2. validate a real Blender-authored skinned and morphed GLB through scene import
3. keep GPU deformation and CPU reference diagnostics visually aligned
4. keep node animation, hierarchy composition, morph, and deformation tests green

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
