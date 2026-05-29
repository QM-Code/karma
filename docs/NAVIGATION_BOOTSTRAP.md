# Navigation Bootstrap

This is the handoff for the current static navmesh/pathfinding pass.

## Current State

- Recast/Detour is integrated behind `KARMA_ENABLE_NAVIGATION` in `CMakeLists.txt`.
- Public API lives under `include/karma/navigation/`.
- Implementation lives in `src/navigation/`.
- `karma::navigation::NavMesh` bakes static triangle geometry with Recast and owns the Detour navmesh.
- `karma::navigation::NavQuery` wraps nearest-point, path, straight-path, and raycast queries.
- Geometry collection supports:
  - direct `renderer::MeshData` via `appendGeometry`
  - `scene::GlbScenePrefab` with world transforms applied
  - ECS entities with `MeshColliderComponent + MeshComponent + TransformComponent`, loading from `MeshComponent::mesh_key`
- `NavMesh::debugDraw()` stores baked polygon edges during build and draws them with `GraphicsDevice::drawLine`.
- `NavQuery::debugDrawPath()` draws query results with debug lines.
- `NavMeshAgentComponent` exists as a small state container only; no system consumes it yet.

## Example

`examples/navmesh_example.cpp` is now a real rendered example, not a CLI smoke test.

It creates a minimal procedural scene:

- walkable floor made from four rectangular slabs, leaving a central hole/blocker
- visible center blocker cube
- start and goal marker cubes
- baked navmesh from the floor mesh
- a Detour path from left to right around the blocker
- green navmesh debug edges and yellow path debug lines

The example target is `karma_navmesh_example`. It is only added for non-headless builds.

## Tests

`tests/navmesh_tests.cpp` validates:

- flat plane bake and path query
- forced detour around a central hole
- empty geometry failure status
- nearest-point projection
- GLB prefab world-transform collection

The test target is `karma_navmesh_tests`.

## Verified Commands

The default all-target build currently fails in this environment when `KARMA_BUILD_RMLUI_DEMO=ON`, because fetched RmlUi 6.0 picks up `/usr/local/lib/libfreetype.a` version 2.4.9 and then fails on newer FreeType symbols such as `FT_LOAD_COLOR`.

This configuration was verified:

```bash
cmake -S . -B build -DKARMA_FETCH_DEPS=ON -DKARMA_BUILD_RMLUI_DEMO=OFF
cmake --build build -j2
ctest --test-dir build -R karma_navmesh_tests --output-on-failure
```

These targets were produced:

- `build/karma_navmesh_example`
- `build/karma_navmesh_tests`

The fully default configure/build was also attempted with:

```bash
cmake -S . -B build -DKARMA_FETCH_DEPS=ON
cmake --build build -j2
```

It stopped in RmlUi/FreeType before reaching all examples. This appears unrelated to the navigation code.

## Known Limitations

- Static single-mesh bake only; no tiled navmesh or tile cache integration.
- No runtime obstacle carving.
- No DetourCrowd integration.
- No serialized navmesh asset format yet.
- No path-following system yet.
- `collectNavMeshGeometry(const ecs::World&)` depends on `MeshComponent::mesh_key`; it cannot read renderer-owned `mesh_id` data back from `GraphicsDevice`.
- Debug draw stores polygon edges at bake time instead of reading Detour internals, because RecastNavigation v1.6 keeps `dtNavMesh::getTile()` private.

## Good Next Steps

1. Add a `NavigationSystem` that consumes `NavMeshAgentComponent` and moves entities along `NavPath` waypoints.
2. Decide where a world/navmesh owner should live: app-level runtime module, scene-level service, or an ECS singleton component.
3. Add optional navmesh serialization so static scenes do not need to rebake every startup.
4. Add area costs and include/exclude flags once gameplay surfaces need distinct walkable materials.
5. Add tiled navmesh or `DetourTileCache` only after a real large-world or dynamic-obstacle use case exists.
6. Add an in-engine debug UI panel after ownership is settled.

