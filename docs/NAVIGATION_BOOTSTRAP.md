# Navigation Bootstrap

This is the handoff for the current static navmesh/pathfinding pass.

## Current State

- Recast/Detour is integrated behind `KARMA_ENABLE_NAVIGATION` in `CMakeLists.txt`.
- Public API lives under `include/karma/simulation/navigation/`.
- Implementation lives in `src/simulation/navigation/`.
- `karma::navigation::NavMesh` bakes static triangle geometry with Recast and owns the Detour navmesh.
- `karma::navigation::NavQuery` wraps nearest-point, path/straight-path,
  sliced path, raycast, height, wall-distance, random-point, and local surface
  movement queries.
- `karma::navigation::NavigationSystem` builds ECS-owned navmeshes, queues
  `NavMeshAgentComponent` path requests onto a worker thread, and moves agents
  after paths resolve on the main thread.
- Geometry collection supports:
  - direct `renderer::MeshData` via `appendGeometry`
  - `scene::GlbScenePrefab` with world transforms applied
  - explicit ECS `NavMeshSurfaceComponent + TransformComponent` sources, using
    CPU mesh data or a mesh key
  - legacy ECS `MeshColliderComponent + MeshComponent + TransformComponent`
    fallback when no explicit navmesh surfaces exist
- `NavMeshComponent` is the ECS owner for bake config, build state, build
  version, debug flags, and runtime `NavMesh`.
- `NavOffMeshLinkComponent` adds Detour off-mesh connections during baking.
- `NavMeshSurfaceComponent` assigns per-source area IDs and walkability.
- `NavMesh::debugDraw()` stores baked polygon edges during build and draws them with `GraphicsDevice::drawLine`.
- `NavQuery::debugDrawPath()` draws query results with debug lines.
- `NavMeshAgentComponent` stores destination, status, async request flags,
  request ID, query filter, path, and simple transform-movement settings.
- Agent requests now move through `Requested -> PathPending -> PathResolved ->
  Moving -> Arrived/PartialPath/Failed`. New requests keep the current path
  moving until the replacement path resolves, and stale worker results are
  ignored by request ID and navmesh build version.

## Example

`examples/navmesh_example.cpp` is now a real rendered example, not a CLI smoke test.

It creates a rendered navigation scene:

- visible `world.glb` environment, matching `karma_example`
- tank from `tank_final.glb` with `NavMeshAgentComponent`
- hidden target marker that appears at the most recent accepted click destination
- baked navmesh owned by an ECS `NavMeshComponent`
- left-click screen-to-floor picking and async
  `NavigationSystem::requestMoveTo`
- reusable `NavigationSystem` waypoint following over the stored agent path
- green navmesh debug edges and yellow remaining-path debug lines

The example target is `karma_navmesh_example`. It is only added for non-headless builds.

## Tests

`tests/navmesh_tests.cpp` validates:

- flat plane bake and path query
- forced detour around a central hole
- empty geometry failure status
- nearest-point projection
- GLB prefab world-transform collection
- explicit ECS navmesh surface collection
- area flags and query filters
- off-mesh connection traversal
- sliced path queries
- ECS `NavigationSystem` build and agent movement
- real `examples/assets/world.glb` navmesh bake coverage

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

- Static single-navmesh-per-world default; no tiled navmesh or tile cache integration.
- No runtime obstacle carving.
- No DetourCrowd integration.
- No serialized navmesh asset format yet.
- Agent movement writes directly to `TransformComponent`; physics/controller
  integration is future work.
- Renderer-owned `mesh_id` data still cannot be read back from `GraphicsDevice`;
  runtime/procedural nav surfaces should provide CPU `renderer::MeshData`.
- Debug draw stores polygon edges at bake time instead of reading Detour internals, because RecastNavigation v1.6 keeps `dtNavMesh::getTile()` private.

## Good Next Steps

1. Add optional navmesh serialization so static scenes do not need to rebake every startup.
2. Add physics/controller-driven agent movement for games that should not write transforms directly.
3. Add a navigation debug UI panel for bake results, active agents, and filter settings.
4. Add tiled navmesh or `DetourTileCache` only after a real large-world or dynamic-obstacle use case exists.
5. Add DetourCrowd after there is a multi-agent avoidance scene to drive the API.
