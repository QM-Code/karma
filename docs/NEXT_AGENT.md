# Current Agent Handoff

This repo is in a fast-moving state. Prefer behavior-preserving refactors first,
then tighten architecture once the split points are proven. Check the worktree
before editing and do not revert unrelated user changes.

## Start Here

This file is the consolidated agent handoff. The old one-off handoff and plan
docs under `docs/` were folded here or removed after completion.

There are eight active technical tracks in the current tree:

1. renderer monolith decomposition
2. particle/render performance
3. effect API split / prefab modularization
4. collision/contact/ground-state ECS work
5. local-light / point-shadow validation
6. gltf node animation / skeletal skinning / morph targets
7. static navmesh generation and pathfinding
8. native UI interaction completion and hardening

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
- [docs/NATIVE_UI.md](NATIVE_UI.md)
- [docs/NATIVE_UI_STATUS.md](NATIVE_UI_STATUS.md)
- [docs/DEBUG_EDITOR.md](DEBUG_EDITOR.md) if changing the runtime debug editor

## Native UI Summary

The first-party retained UI is implemented and is the graphical-profile
default. Native authoring is now a hard-cutover JSON5 pair: `.kui.json5`
documents and `.kstyle.json5` themes. Documents use explicit binding,
expression, localization, and action objects; themes use type defaults and
explicitly named styles rather than an authored selector cascade. Packaged UI
uses `ui_document` and `ui_theme` assets.

The second pass includes strict source-mapped schemas, recursive theme imports,
ordered style composition, sandboxed `openFile()`/`openFileController()` roots,
native watching on Linux/Windows with fingerprint fallback, transactional
last-good swaps, an RAII `DocumentController`, reference canvases/anchors,
per-part/nine-slice skinning, cursors, scrollbars, selects/popups/menus,
tabs/trees/tooltips/virtual lists, disclosures, splitters, and floating windows.
The native menu, Tank HUD, and Constraint Lab remain provider-independent.
The `ui_showcase` example is the exhaustive interaction and skinning lab; keep
its loose JSON5 graph and generated medieval nine-slice asset working when
changing authoring, widgets, layout, resources, or hot reload.

Continue native UI work from
[NATIVE_UI_STATUS.md](NATIVE_UI_STATUS.md), which records the source map,
invariants, exact verification commands, capability boundaries, and acceptance
criteria. Focused authoring, binding, document/runtime lifetime, listener,
watcher/reload, focus/transient/widget, layout, style/motion,
paint/presentation, package, and screenshot tests pass. Concrete services now
own runtime DOM policy, document/element/listener lifetime, dependency
indexing, style and active motion, document layout, accessibility,
presentation resources/assembly, and hot reload. The former two-pass
`system.cpp` and `system_api.inc` are gone; normal translation units separate
lifecycle, documents, reconciliation, interaction, input, and frame
orchestration behind a declaration-only private `system_impl.h`. Do not
collapse these services back into System or replace input/frame ordering with
a callback host. Do not add an isolated UI device-reset hook before the engine
has a common graphics-device recreation lifecycle.

## Worktree Caution

Do not assume unrelated modified files are safe to revert.

High-signal areas right now:

- [`src/rendering/renderer/backends/diligent/backend_init.cpp`](../src/rendering/renderer/backends/diligent/backend_init.cpp)
- [`src/rendering/renderer/backends/diligent/backend_render.cpp`](../src/rendering/renderer/backends/diligent/backend_render.cpp)
- [`src/rendering/renderer/backends/diligent/passes/`](../src/rendering/renderer/backends/diligent/passes)
- [`src/rendering/renderer/backends/diligent/resources/`](../src/rendering/renderer/backends/diligent/resources)
- [`src/rendering/renderer/render_system.cpp`](../src/rendering/renderer/render_system.cpp)
- [`src/features/visual/particles/`](../src/features/visual/particles)
- [`src/features/visual/volumes/volume_system.cpp`](../src/features/visual/volumes/volume_system.cpp)
- [`src/features/visual/volumes/volume_runtime_module.cpp`](../src/features/visual/volumes/volume_runtime_module.cpp)
- [`src/content/prefabs/prefab_runtime.cpp`](../src/content/prefabs/prefab_runtime.cpp)
- [`src/content/assets/asset_package.cpp`](../src/content/assets/asset_package.cpp)
- [`src/content/prefabs/component_serializer_registry.cpp`](../src/content/prefabs/component_serializer_registry.cpp)

Recent renderer/local-light work includes:

- point-shadow runtime light budgeting
- explicit `R32_FLOAT` SRVs for directional and point shadow-map arrays
- per-SRB shadow resource binding for actual material/default draw paths
- moving-point-light shadow refresh no longer chunks through the small cache threshold
- reduced point-shadow seam artifacts from hard cubemap-face boundaries
- light-stress sample staged for gradual `1-16` shadowed-light validation with moving markers

If continuing there, start with:

- [docs/ENGINE_USAGE.md](ENGINE_USAGE.md)
- [examples/rendering/light_stress.cpp](../examples/rendering/light_stress.cpp)
- [src/rendering/renderer/backends/diligent/passes/shadows.cpp](../src/rendering/renderer/backends/diligent/passes/shadows.cpp)

## Navigation Summary

Recent navigation work already in the tree:

- Recast/Detour dependency integration behind `KARMA_ENABLE_NAVIGATION`.
- Static navmesh bake and path-query APIs in
  [`include/karma/navigation.h`](../include/karma/navigation.h).

Continue navigation work from [NAVIGATION.md](NAVIGATION.md).
