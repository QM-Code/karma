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
- [`include/karma/app.h> 9` light transition
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
- static navmesh bake API in `include/karma/navigation.h