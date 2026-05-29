# Navigation

Karma navigation is built around Recast/Detour. Recast bakes static triangle
geometry into a navigation mesh, and Detour answers runtime path queries.

The current implementation is a static-world v1. It is suitable for baking
loaded or procedural triangle geometry once, then running synchronous path
queries against that mesh.

## Build Flow

1. Collect world-space triangles into `navigation::NavMeshInputGeometry`.
2. Build a `navigation::NavMesh` with `NavMeshBuildConfig`.
3. Create `navigation::NavQuery` from the mesh.
4. Call `findPath`, `findNearestPoint`, or `raycast`.

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

ECS collection is also available for entities that have
`MeshColliderComponent`, `MeshComponent`, and `TransformComponent`. This path
loads mesh data from `MeshComponent::mesh_key`.

Procedural or renderer-owned mesh data can be appended directly:

```cpp
navigation::NavMeshInputGeometry geometry;
navigation::appendGeometry(geometry, mesh_data, position, rotation, scale);
```

`NavMesh::debugDraw()` and `NavQuery::debugDrawPath()` draw the baked mesh and
paths through `renderer::GraphicsDevice::drawLine`.

## Example

`karma_navmesh_example` is a minimal rendered scene. It builds a procedural
walkable floor with a central blocker, bakes the floor into a navmesh, computes
a path around the blocker, and renders:

- solid floor/blocker/markers as regular mesh entities
- green baked navmesh edges
- yellow path lines and path point markers

The example deliberately bakes from the same CPU-side `renderer::MeshData` used
to create the visible floor, so it does not depend on GLB assets.

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

Useful commands:

```bash
cmake -S . -B build -DKARMA_FETCH_DEPS=ON -DKARMA_BUILD_RMLUI_DEMO=OFF
cmake --build build -j2
ctest --test-dir build -R karma_navmesh_tests --output-on-failure
```

## Current Scope

The first implementation supports static-world navmesh baking and synchronous
path queries. Dynamic tile rebakes, runtime obstacles, and crowd simulation are
future work.

The current `NavMeshAgentComponent` is only a lightweight state container. There
is no navigation system yet that consumes paths and moves entities.

