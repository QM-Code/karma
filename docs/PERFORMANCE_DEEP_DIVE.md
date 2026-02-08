# Karma Project Deep Dive

## Scope

This document captures a static deep dive across:

- Engine runtime orchestration
- ECS data flow and iteration patterns
- Rendering submission and backend execution
- UI compositing path
- Supporting systems (physics, audio, input)
- Build layout and subsystem sizing

The findings are code-path based (not runtime-profiler captures).

## Rendering Pipeline Map

1. Frame orchestration
   - `src/app/engine_app.cpp:145`
   - Polls window/input, runs fixed-step systems, runs game update, updates audio, runs render, runs UI, presents.
2. Scene submission
   - `src/renderer/render_system.cpp:239`
   - Selects camera/light/env, culls mesh entities, submits `DrawItem`s, emits debug collider lines.
3. GPU rendering
   - `src/renderer/backends/diligent/backend_render.cpp:1292`
   - Clears targets, ensures env resources/skybox, renders shadow pass, renders forward pass, renders debug lines.
4. UI compositing
   - `src/renderer/backends/diligent/backend_ui.cpp:242`
   - Uploads UI buffers, executes draw commands with pipeline/scissor/texture state changes.
5. Resource creation paths
   - `src/renderer/render_system.cpp:354`
   - `src/renderer/backends/diligent/backend_mesh.cpp:83`
   - `src/renderer/backends/diligent/backend_textures.cpp:13`

## Architecture Snapshot

- Renderer backend implementation dominates code volume:
  - `src/renderer`: ~4820 lines
  - `src/physics`: ~1832 lines
  - `src/platform`: ~1163 lines
- Header-heavy areas:
  - `include/karma/renderer`: ~810 lines
  - `include/karma/physics`: ~654 lines

This means most frame-time risk currently clusters in renderer backend behavior and how ECS feeds it.

## Likely Inefficiencies (Prioritized)

1. Stale render instances are never explicitly retired.
   - Evidence:
     - Upsert-only submit path: `src/renderer/backends/diligent/backend_render.cpp:486`
     - Full instance iteration in render: `src/renderer/backends/diligent/backend_render.cpp:1553`
     - No per-entity retire in render system: `src/renderer/render_system.cpp:341`
     - No backend API for removing an instance: `include/karma/renderer/backend.hpp:41`
   - Risk:
     - Growing CPU work and possible stale draw records over long sessions.

2. Mesh/material lifetime leaks on mesh key changes and despawns.
   - Evidence:
     - Mesh replaced without destroying prior mesh handle: `src/renderer/render_system.cpp:373`
     - Mesh destroy only erases mesh record: `src/renderer/backends/diligent/backend_mesh.cpp:324`
     - `createMeshFromFile` allocates material records per imported material: `src/renderer/backends/diligent/backend_mesh.cpp:150`
   - Risk:
     - GPU/CPU resource growth as content changes.

3. Duplicate Assimp imports for the same mesh file.
   - Evidence:
     - Bounds import in render system: `src/renderer/render_system.cpp:199`
     - Full mesh import in backend: `src/renderer/backends/diligent/backend_mesh.cpp:86`
   - Risk:
     - Avoidable blocking disk/parse time during first-use hitch windows.

4. Per-frame ECS `view()` allocations across hot loops.
   - Evidence:
     - `view()` allocates and returns `std::vector<Entity>`: `include/karma/ecs/world.h:78`
     - Repeated use in render loop and helper loops: `src/renderer/render_system.cpp:248`, `src/renderer/render_system.cpp:342`, `src/renderer/render_system.cpp:410`
     - Similar usage in physics/audio: `src/physics/physics_system.cpp:74`, `src/audio/audio_system.cpp:27`
   - Risk:
     - Frame allocator churn and cache misses at scale.

5. Unsorted rendering path with high per-draw state traffic.
   - Evidence:
     - Iteration over unordered instance map: `src/renderer/backends/diligent/backend_render.cpp:1553`
     - Per-draw VB/IB binds, CB map, SRB commit, draw: `src/renderer/backends/diligent/backend_render.cpp:1619`, `src/renderer/backends/diligent/backend_render.cpp:1666`, `src/renderer/backends/diligent/backend_render.cpp:1690`
   - Risk:
     - Excess CPU overhead and pipeline/resource bind churn.

6. Per-draw shader variable lookups for env textures.
   - Evidence:
     - `GetVariableByName` in draw lambda each draw: `src/renderer/backends/diligent/backend_render.cpp:1678`
   - Risk:
     - Avoidable repeated string/hash lookup overhead.

7. Validation-heavy draw flags in normal draw paths.
   - Evidence:
     - Frequent `DRAW_FLAG_VERIFY_ALL`: `src/renderer/backends/diligent/backend_render.cpp:1498`, `src/renderer/backends/diligent/backend_render.cpp:1698`, `src/renderer/backends/diligent/backend_ui.cpp:391`
   - Risk:
     - Non-trivial CPU overhead if left on in release/perf runs.

8. Line debug buffer overflow path does not grow buffer.
   - Evidence:
     - Overflow branch has no reallocation action: `src/renderer/backends/diligent/backend_render.cpp:1727`
     - Initial capacity fixed to 1024 vertices: `src/renderer/backends/diligent/backend_render.cpp:637`
   - Risk:
     - Lost debug lines and potential confusion during diagnostics.

9. Culling projection mismatch between submit and render.
   - Evidence:
     - Culling uses fixed camera aspect `16:9`: `src/renderer/render_system.cpp:259`
     - Actual render uses framebuffer aspect: `src/renderer/backends/diligent/backend_render.cpp:1312`
   - Risk:
     - Incorrect cull decisions (extra overdraw or missing visible meshes).

10. Unused filesystem checks on submission path.
    - Evidence:
      - `std::filesystem::exists` computed but never used: `src/renderer/render_system.cpp:354`, `src/renderer/render_system.cpp:371`
    - Risk:
      - Extra I/O/syscall overhead without behavioral benefit.

11. Full scene-entity synchronization each frame.
    - Evidence:
      - `syncSceneEntities()` scans full entity set every frame: `src/app/engine_app.cpp:126`
    - Risk:
      - Scales poorly with large entity counts.

12. System graph rebuilds schedule each fixed update.
    - Evidence:
      - `buildOrder()` called every `update()`: `include/karma/systems/system_graph.h:36`
      - Dependency reverse lookup scans full node set repeatedly: `include/karma/systems/system_graph.h:106`
    - Risk:
      - Scheduler overhead grows with system count.

13. Input mapping update is nested over actions, bindings, and events.
    - Evidence:
      - Held and pressed checks in nested loops: `src/input/input_system.cpp:66`, `src/input/input_system.cpp:89`
    - Risk:
      - Input CPU cost scales with action count.

## Additional Notes

- The default TODO list in `docs/TODO.md` already aligns with several high-impact items:
  - Instancing
  - Batching/sorting
  - Async asset loading
  - Texture streaming
  - Render graph
  - CPU-side multithreaded render prep
- There are also signs of abandoned or placeholder rendering abstraction:
  - `material_key` is carried in mesh component/record but not wired to a real material lookup in submission path.
  - `exists` checks are present but ignored.

## Shadow Baseline

Current directional shadow pipeline baseline that is verified stable in-scene:

- Light-space depth mapping explicitly maps near/far in `backend_render.cpp`.
- Shader-side receiver bias combines:
  - world-space normal/light offset pre-projection
  - receiver-plane derivative bias
  - slope-scaled texel bias
- Runtime defaults:
  - `shadow_map_size = 2048`
  - `shadow_pcf_radius = 1`
  - `shadow_bias = 0.0006f`
  - `shadow_receiver_bias_scale = 0.75f`
  - `shadow_normal_bias_scale = 1.0f`
  - `shadow_raster_depth_bias = 0`
  - `shadow_raster_slope_bias = 0.0f`

## What Is Already Good

1. Texture cache exists and avoids redundant image decode:
   - `src/renderer/backends/diligent/backend_textures.cpp:123`
2. Mesh bounds cache exists:
   - `src/renderer/render_system.cpp:360`
3. Environment precompute is dirty-gated:
   - `src/renderer/backends/diligent/backend_render.cpp:655`

## Recommended Action Order

1. Fix lifetime correctness first:
   - Add explicit instance retirement.
   - Tie imported material ownership to mesh lifetime or implement material dedup cache.
2. Remove duplicate import work:
   - Single-source bounds from mesh import path or precomputed metadata.
3. Reduce per-frame CPU overhead:
   - Replace allocating `view()` hot usage with non-alloc iteration.
   - Cache system graph order when topology unchanged.
4. Reduce render submission overhead:
   - Sort by pipeline/material/mesh.
   - Cache SRB variable handles.
   - Disable verify flags in non-debug builds.
5. Validate with profiling:
   - Capture CPU and GPU timing after each step to verify actual wins.
