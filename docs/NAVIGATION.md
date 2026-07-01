# Navigation

Karma navigation is built around Recast/Detour. Recast bakes triangle geometry
into solo or tiled navmeshes, Detour answers runtime path queries, DetourTileCache
updates dynamic obstacle tiles, and DetourCrowd provides local steering and
avoidance.

The public API follows the engine layers:

- `world` owns ECS data contracts such as `NavMeshComponent`,
  `NavTileCacheObstacleComponent`, and `NavCrowdAgentComponent`.
- `simulation/navigation` owns Recast/Detour wrappers such as `NavMesh`,
  `NavQuery`, `NavTileCache`, `NavCrowd`, and `NavigationSystem`.
- `content` imports GLB/mesh data into shared geometry contracts.
- `runtime` wires systems together; it does not own navigation data types.

Direct `NavQuery`, `NavTileCache`, and `NavCrowd` calls are synchronous.
ECS path requests submitted through `NavigationSystem` are resolved from immutable
navmesh snapshots on a worker thread, while tile-cache and crowd updates run on
the main thread because they mutate live Detour state and entity transforms.

Public headers are split by API role:

- `karma/navigation.h`: runtime navigation API and ECS components.
- `karma/assets.h`: snapshot asset load/save helpers.
- `karma/world.h`: scene/entity storage used by geometry collection.

Minimal query workflow:

```cpp
auto level = assets.loadGltfScene("game/level");

world::World world;
world::Scene scene;
auto imported =
    world::instantiateGltfSceneAsset(world, scene, assets, *level);

const auto geometry = navigation::collectNavMeshGeometry(world, &assets);

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
  the bake. It supports CPU `world::MeshData`, a mesh key, area IDs, and
  walkable/non-walkable marking.
- `NavOffMeshLinkComponent`: attach to an entity to add a point-to-point
  Detour off-mesh connection during bake.
- `NavConvexVolumeComponent`: attach to mark a vertical convex area volume
  during bake.
- `NavMeshAgentComponent`: attach to moving entities. `NavigationSystem`
  consumes destinations, resolves paths asynchronously, and advances the entity
  transform.
- `NavTileCacheComponent`: attach beside `NavMeshComponent` to bake a Detour
  tile cache and allow dynamic obstacle updates.
- `NavTileCacheObstacleComponent`: attach to obstacle entities to add/remove
  cylinder, AABB, or oriented-box temporary obstacles.
- `NavCrowdComponent`: attach beside `NavMeshComponent` to own a DetourCrowd
  instance.
- `NavCrowdAgentComponent`: attach to entities whose transforms should be
  controlled by DetourCrowd steering.

## Baking

`NavMeshBuildConfig::build_mode` selects `Solo` or `Tiled`. Tiled builds expose
tile rebuild/removal APIs and are required for DetourTileCache. Recast
partitioning is selected with `NavMeshPartitionType::Watershed`, `Monotone`, or
`Layers`.

`NavMeshInputGeometry` supports:

- triangles and optional per-triangle area IDs
- authored off-mesh links
- convex area volumes

`NavMesh::snapshot()` serializes solo or tiled navmeshes for worker-thread
queries. `NavMesh::loadSnapshot()` rehydrates the snapshot into a live Detour
mesh. Low-level state helpers expose polygon flags/areas, polygon reference
decoding, active tile metadata, tile state store/restore, and off-mesh
connection endpoint lookup without exposing Detour types.

`NavMeshComponent::cache.enabled` opts an ECS bake into persistent navmesh
caching. Cache keys fingerprint collected geometry, source mask, build config
excluding `collect_build_debug_draw`, and tile-cache config when a
`NavTileCacheComponent` is present. Static navmeshes are stored as
`navmesh/<fingerprint>.knav`; tile-cache builds are stored as
`tilecache/<fingerprint>.kntc`. `KARMA_NAV_CACHE=0` disables the cache,
`KARMA_NAV_CACHE_DIR` selects the root, and `KARMA_NAV_CACHE_FLUSH=1` clears it
before first use. Without an explicit root, navigation uses
`KARMA_ASSET_CACHE_DIR/navigation` when available, then the platform user cache
root under `karma/navigation`.

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

Crowd agents use a separate synchronous pipeline. Add `NavCrowdComponent` to the
navmesh entity, add `NavCrowdAgentComponent` to controlled entities, then call
`NavigationSystem::requestCrowdMoveTo(...)` or
`NavigationSystem::requestCrowdVelocity(...)`. The system initializes the crowd,
adds/removes Detour agents, submits move requests, advances `NavCrowd`, and
writes agent positions/velocities back to transforms.

`NavigationSystem::stats()` exposes lightweight diagnostics for this pipeline:
main-thread update/rebuild/submit/move/apply timings, worker queue/solve timing,
request counters, stale-result count, last path status, and whether the worker
had to rebuild its cached Detour query. `navigation_navmesh` logs these values
whenever a path request is submitted or completed.

If no explicit `NavMeshSurfaceComponent` exists, the collector keeps the legacy
fallback for entities with a mesh `ColliderComponent`, `MeshComponent`, and
`TransformComponent`, using vertices and indices embedded in the mesh collider
shape.

Procedural or renderer-owned mesh data can be appended directly:

```cpp
navigation::NavMeshInputGeometry geometry;
navigation::appendGeometry(geometry, mesh_data, position, rotation, scale);
```

Area IDs are assigned per appended triangle or per `NavMeshSurfaceComponent`.
Build config area flags control which polygons a filter can traverse, while
`NavQueryFilter` controls include/exclude flags and per-area costs for each
query, path agent, or crowd filter slot. Use `navigation::makeQueryFilter(config)`
to seed a query filter from `NavMeshBuildConfig::area_configs`.

`NavQuery` exposes Detour path, raycast, detailed raycast, AABB polygon query,
sliced path, partial sliced finalization, Dijkstra path extraction,
closed-list checks, nearest-point, closest-point, height, wall-distance,
random-point, smooth-path, local-neighbourhood, polys-around-circle,
polys-around-shape, wall-segment, portal, and edge-midpoint helpers.

Set `NavMeshBuildConfig::collect_build_debug_draw` when tools need RecastDemo
build-intermediate views. Successful builds then populate `NavMeshDebugDrawMode`
line layers for voxels, walkable voxels, compact heightfields, distance fields,
regions, region connections, raw/processed contours, poly mesh, and detail mesh.
`NavMeshComponent::debug_draw_mode` selects which layer the ECS
`NavigationSystem` submits.
When component caching is enabled, normal bakes suppress build-debug capture so
cache misses do not pay the Recast intermediate capture cost.
`NavigationSystem::requestBuildDebugDraw(world, nav_entity)` performs one
cache-bypassing rebuild that captures those layers.

## Dynamic Obstacles

`NavTileCache` builds compressed tile layers from `NavMeshInputGeometry` and
initializes a tiled `NavMesh`. Runtime code can add or remove temporary
obstacles:

```cpp
navigation::NavTileCache tile_cache;
navigation::NavMesh nav_mesh;
navigation::NavTileCacheBuildResult result;

if (tile_cache.build(nav_mesh, geometry, nav_config, {}, &result)) {
  uint64_t obstacle = 0;
  tile_cache.addBoxObstacle({-0.5f, 0.0f, -2.0f},
                            {0.5f, 2.0f, 2.0f},
                            &obstacle);

  bool up_to_date = false;
  while (!up_to_date) {
    tile_cache.update(0.0f, nav_mesh, &up_to_date);
  }
}
```

Tile caches can be persisted as opaque `.kntc` assets:

```cpp
navigation::NavTileCacheSnapshot snapshot = tile_cache.snapshot(nav_mesh);
assets::saveNavTileCacheSnapshot("level.kntc", snapshot);

navigation::NavMesh loaded_mesh;
navigation::NavTileCache loaded_cache;
loaded_cache.loadSnapshot(loaded_mesh,
                          assets::loadNavTileCacheSnapshot("level.kntc"));
```

`NavTileCacheBuildConfig::compression` accepts `NavTileCacheCompression::FastLz`
or `None`. The serialized asset stores the mode, compressed tile layers, cache
params, navmesh params, build geometry needed for later tile rebuilds, and a
navmesh snapshot for immediate queries after load.

In ECS, put `NavTileCacheComponent` on the navmesh entity and
`NavTileCacheObstacleComponent` on obstacle entities. The system owns obstacle
handles and refreshes navmesh snapshots after tile updates.

## Crowds

`NavCrowd` wraps DetourCrowd local steering. It supports per-agent radius,
height, speed, acceleration, collision range, optimization range, separation,
update flags, query filter slots, obstacle avoidance quality slots, move
targets, velocity targets, and per-agent diagnostics. `NavCrowd::debugSnapshot`
captures requested corridor polygons, corners, collision segments, and
neighbour links from active Detour agents. `NavCrowdComponent::debug_request`
enables the same capture through ECS and stores the latest result in
`debug_snapshot`.

```cpp
navigation::NavCrowd crowd;
navigation::NavCrowdConfig crowd_config;
crowd_config.max_agents = 32;
crowd_config.max_agent_radius = 0.6f;

if (crowd.init(nav_mesh, crowd_config)) {
  const int agent = crowd.addAgent(start, {});
  crowd.requestMoveTarget(agent, goal);
  crowd.update(dt);
}
```

For ECS, put `NavCrowdComponent` on the navmesh entity and
`NavCrowdAgentComponent` on controlled entities. `NavCrowdMovementMode::Transform`
writes DetourCrowd positions to entity transforms.
`NavCrowdMovementMode::CharacterControllerVelocity` leaves transforms under
physics authority and writes horizontal crowd velocity
to `CharacterControllerComponent::setDesiredVelocity`. Physics owns one backend
character controller per ECS `CharacterControllerComponent`.

`NavMesh::debugDraw()` and `NavQuery::debugDrawPath()` draw the baked mesh,
captured Recast debug layers, and paths through
`rendering::GraphicsDevice::drawLine`.

## Example

`navigation_navmesh` is a rendered click-to-move scene using the same
`world.glb`, `tank_final.glb`, HDR environment, and lighting style as
`gameplay_tank`. It bakes the world mesh through an ECS `NavMeshComponent` and
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

## Recast Examples

`navigation_samples_headless` is a headless parity runner for the upstream
RecastDemo samples and tools. It uses copied assets in
`examples/assets/navigation/recast` and exposes scenarios through Karma's public
navigation API rather than RecastDemo internals:

- `solo`: solo navmesh bake, Recast path test cases, smooth paths, snapshots
- `tile`: tiled/layer bake, Recast raycast test cases, tile rebuild/removal
- `temp-obstacles`: DetourTileCache build, cylinder/AABB/oriented-box obstacles
- `crowd`: DetourCrowd initialization, agents, steering requests, updates
- `debug`: partition modes, convex volumes, off-mesh links, pruning, build debug
  layers, and advanced Detour query helpers

Run all scenarios with:

```bash
cmake --build build/headless --target navigation_samples_headless --parallel 1
./build/headless/examples/navigation/samples/headless all
```

The graphical RecastDemo recreations are split into one Karma binary per
upstream sample class:

- `navigation_solo_mesh`
- `navigation_tile_mesh`
- `navigation_temp_obstacles`
- `navigation_debug`

Each binary renders the matching copied OBJ asset, owns its own ECS scene, and
uses Karma's ImGui adapter for the sample settings, tools, debug draw modes,
save/load actions, tile/cache controls, off-mesh links, convex volumes, crowd
agents, and Recast build-debug layers exposed by that sample.

Build them with:

```bash
cmake --build build/portable --target \
  navigation_solo_mesh \
  navigation_tile_mesh \
  navigation_temp_obstacles \
  navigation_debug \
  --parallel 1
```

`navigation_samples_gallery` remains as an extra combined gallery for quick
side-by-side smoke checks. Run it with `all`, `solo`, `tile`,
`temp-obstacles`, `crowd`, or `debug`.

## Tests

`karma_navmesh_tests` covers:

- flat-plane bake and path query
- central-hole detour path
- invalid input failure status
- nearest-point projection
- GLB prefab world-transform geometry collection
- ECS navmesh surface collection
- area flags/query filtering
- convex area volumes
- partition modes and tiled snapshots
- advanced Detour query helpers
- dynamic tile-cache obstacles and tile diagnostics
- DetourCrowd wrapper movement and ECS crowd agents
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

The engine-facing implementation supports solo and tiled navmesh baking,
snapshots, direct Detour queries, async ECS path requests, sliced queries, area
flags/costs, convex volumes, off-mesh links, dynamic tile-cache obstacles,
DetourCrowd steering, and transform-driven ECS movement. Serialized standalone
tile-cache assets and physics-controller crowd movement are not implemented yet.
