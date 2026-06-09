# Navigation

Karma navigation is built around Recast/Detour. Recast bakes static triangle
geometry into a navigation mesh, and Detour answers runtime path queries.

The current implementation is a static-world v1. It is suitable for baking
loaded or procedural triangle geometry once, then running path queries and
simple ECS agent movement against that mesh. Direct `NavQuery` calls are
synchronous. ECS agent path requests submitted through `NavigationSystem` are
resolved on a worker thread from immutable navmesh snapshots, then applied back
to components on the main thread.

## Build Flow

1. Collect world-space triangles into `navigation::NavMeshInputGeometry`.
2. Build a `navigation::NavMesh` with `NavMeshBuildConfig`.
3. Create `navigation::NavQuery` from the mesh.
4. Call `findPath`, `findNearestPoint`, `raycast`, sliced path queries, or the
   local spatial helpers.

GLB scenes can be baked directly from `scene::GlbScenePrefab`:

```cpp
const auto prefab = scene::loadGlbScenePrefab(path);
const auto geometry = navigation::collectNavMeshGeometry(prefab);

navigation::NavMesh nav_mesh;
navigation::NavMeshBuildResult result;
if (nav_mesh.build(geometry, {}, &result)) {
  navigation::NavQuery query(nav_mesh);
  navigation::NavPath path = query.findPath(start, end);
}
```

ECS collection now prefers explicit navigation surfaces:

- `NavMeshComponent`: attach to an empty owner entity to hold bake config,
  build state, debug flags, and the runtime `navigation::NavMesh`.
- `NavMeshSurfaceComponent`: attach to render/source entities that should feed
  the bake. It supports CPU `geometry::MeshData`, a mesh key, area IDs, and
  walkable/non-walkable marking.
- `NavOffMeshLinkComponent`: attach to an entity to add a point-to-point
  Detour off-mesh connection during bake.
- `NavMeshAgentComponent`: attach to moving entities. `NavigationSystem`
  consumes destinations, resolves paths asynchronously, and advances the entity
  transform.

## ECS Request Pipeline

`NavigationSystem::requestMoveTo(world, entity, destination)` writes intent onto
the agent component. The system then advances the request through a clear state
pipeline:

1. `Requested`: gameplay has submitted a destination.
2. `PathPending`: the main thread copied the request into the path worker queue.
   If the agent already has a path, it keeps following that path while the
   replacement is pending; `path_pending` remains true even if `status` returns
   to `Moving`.
3. `PathResolved`: the worker returned a valid path and the main thread stored
   it on the agent. Movement starts on the next update.
4. `Moving`: the agent is consuming waypoints.
5. `Arrived`, `PartialPath`, or `Failed`: terminal states for the request.

`NavMeshAgentComponent::path_pending`, `path_resolved`, `current_path_partial`,
`path_request_id`, and `last_path_status` expose the same pipeline in data form.
New requests do not clear the current path. They ignore stale worker results by
request ID, then atomically replace the active path when the latest request
resolves. Navmesh rebakes increment `NavMeshComponent::build_version`, so paths
calculated against an older bake are discarded instead of being applied to the
wrong mesh.

`NavigationSystem::stats()` exposes lightweight diagnostics for this pipeline:
main-thread update/rebuild/submit/move/apply timings, worker queue/solve timing,
request counters, stale-result count, last path status, and whether the worker
had to rebuild its cached Detour query. `karma_navmesh_example` logs these values
whenever a path request is submitted or completed.

If no explicit `NavMeshSurfaceComponent` exists, the collector keeps the legacy
fallback for entities with `MeshColliderComponent`, `MeshComponent`, and
`TransformComponent`, loading mesh data from `MeshComponent::mesh_key`.

Procedural or renderer-owned mesh data can be appended directly:

```cpp
navigation::NavMeshInputGeometry geometry;
navigation::appendGeometry(geometry, mesh_data, position, rotation, scale);
```

Area IDs are assigned per appended triangle or per `NavMeshSurfaceComponent`.
Build config area flags control which polygons a filter can traverse, while
`NavQueryFilter` controls include/exclude flags and per-area costs for each
query or agent. Use `navigation::makeQueryFilter(config)` to seed a query
filter from `NavMeshBuildConfig::area_configs`.

`NavMesh::debugDraw()` and `NavQuery::debugDrawPath()` draw the baked mesh and
paths through `renderer::GraphicsDevice::drawLine`.

## Example

`karma_navmesh_example` is a rendered click-to-move scene using the same
`world.glb`, `tank_final.glb`, HDR environment, and lighting style as
`karma_example`. It bakes the world mesh through an ECS `NavMeshComponent` and
lets left-clicks request async movement on the tank's `NavMeshAgentComponent`. It
renders:

- the Karma example world and tank assets as regular mesh entities
- a small target marker at the most recent accepted destination
- green baked navmesh edges
- yellow remaining-path lines and path point markers

The example deliberately marks the visible world entity with
`NavMeshSurfaceComponent`, so the bake uses the same GLB asset path as the
renderer.

## Defaults

The default agent is 2.0 units tall, 0.6 units wide, can climb 0.9 units, and
walks slopes up to 45 degrees. Tune `NavMeshBuildConfig` per game.

The rendered example overrides these defaults with a smaller agent radius and
cell size so the central-hole layout produces a clean visible detour.

## Tests

`karma_navmesh_tests` covers:

- flat-plane bake and path query
- central-hole detour path
- invalid input failure status
- nearest-point projection
- GLB prefab world-transform geometry collection
- ECS navmesh surface collection
- area flags/query filtering
- off-mesh connection paths
- sliced path queries
- `NavigationSystem` build and agent movement
- the real `examples/assets/world.glb` bake path used by the example

Useful commands:

```bash
cmake -S . -B build -DKARMA_FETCH_DEPS=ON -DKARMA_BUILD_RMLUI_DEMO=OFF
cmake --build build -j2
ctest --test-dir build -R karma_navmesh_tests --output-on-failure
```

## Current Scope

The first engine-facing implementation supports static-world navmesh baking,
synchronous direct Detour queries, async ECS path requests, sliced Detour
queries, area flags/costs, off-mesh links, and simple transform-driven agent
movement. Dynamic tile rebakes, runtime obstacles, serialized navmesh assets,
physics-controller movement, and DetourCrowd avoidance are future work.
