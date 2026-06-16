#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

#include "karma/world/components/nav_mesh.h"
#include "karma/world/components/nav_mesh_agent.h"
#include "karma/world/components/nav_crowd.h"
#include "karma/world/components/nav_tile_cache.h"
#include "karma/world/components/collider.h"
#include "karma/world/components/player_controller.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"
#include "karma/simulation/navigation/nav_geometry.h"
#include "karma/simulation/navigation/nav_crowd.h"
#include "karma/simulation/navigation/nav_mesh.h"
#include "karma/simulation/navigation/nav_query.h"
#include "karma/simulation/navigation/nav_tile_cache.h"
#include "karma/simulation/navigation/navigation_system.h"
#include "karma/content/importers/glb_scene_import.h"
#include "karma/content/navigation/nav_tile_cache.h"

#ifdef NDEBUG
#undef assert
#define assert(expr)                                                            \
  do {                                                                          \
    if (!(expr)) {                                                              \
      std::cerr << "assertion failed: " #expr << " at " << __FILE__ << ":"     \
                << __LINE__ << "\n";                                           \
      std::abort();                                                             \
    }                                                                           \
  } while (false)
#endif

namespace {

std::filesystem::path resolveRepoPath(const std::filesystem::path& relative) {
  std::filesystem::path cwd = std::filesystem::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    const std::filesystem::path candidate = cwd / relative;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    if (!cwd.has_parent_path()) {
      break;
    }
    cwd = cwd.parent_path();
  }
  return relative;
}

karma::geometry::MeshData makePlaneMesh(float half_extent = 5.0f) {
  karma::geometry::MeshData mesh;
  mesh.vertices = {
      {-half_extent, 0.0f, -half_extent},
      {half_extent, 0.0f, -half_extent},
      {half_extent, 0.0f, half_extent},
      {-half_extent, 0.0f, half_extent},
  };
  mesh.indices = {0, 2, 1, 0, 3, 2};
  return mesh;
}

karma::navigation::NavMeshInputGeometry makePlaneGeometry(float half_extent = 5.0f) {
  karma::navigation::NavMeshInputGeometry geometry;
  karma::navigation::appendGeometry(geometry, makePlaneMesh(half_extent));
  return geometry;
}

void appendQuad(karma::geometry::MeshData& mesh,
                const karma::math::Vec3& a,
                const karma::math::Vec3& b,
                const karma::math::Vec3& c,
                const karma::math::Vec3& d) {
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({a.x, a.y, a.z});
  mesh.vertices.push_back({b.x, b.y, b.z});
  mesh.vertices.push_back({c.x, c.y, c.z});
  mesh.vertices.push_back({d.x, d.y, d.z});
  mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
}

karma::navigation::NavMeshInputGeometry makeRingGeometry() {
  karma::geometry::MeshData mesh;
  appendQuad(mesh, {-5.0f, 0.0f, -5.0f}, {-1.0f, 0.0f, -5.0f},
             {-1.0f, 0.0f, 5.0f}, {-5.0f, 0.0f, 5.0f});
  appendQuad(mesh, {1.0f, 0.0f, -5.0f}, {5.0f, 0.0f, -5.0f},
             {5.0f, 0.0f, 5.0f}, {1.0f, 0.0f, 5.0f});
  appendQuad(mesh, {-1.0f, 0.0f, -5.0f}, {1.0f, 0.0f, -5.0f},
             {1.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, -1.0f});
  appendQuad(mesh, {-1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f},
             {1.0f, 0.0f, 5.0f}, {-1.0f, 0.0f, 5.0f});

  karma::navigation::NavMeshInputGeometry geometry;
  karma::navigation::appendGeometry(geometry, mesh);
  return geometry;
}

karma::navigation::NavMeshInputGeometry makeCorridorGeometry() {
  karma::geometry::MeshData mesh;
  appendQuad(mesh,
             {-5.0f, 0.0f, -1.0f},
             {5.0f, 0.0f, -1.0f},
             {5.0f, 0.0f, 1.0f},
             {-5.0f, 0.0f, 1.0f});

  karma::navigation::NavMeshInputGeometry geometry;
  karma::navigation::appendGeometry(geometry, mesh);
  return geometry;
}

void updateTileCacheUntilReady(karma::navigation::NavTileCache& tile_cache,
                               karma::navigation::NavMesh& nav_mesh) {
  for (int i = 0; i < 16; ++i) {
    bool up_to_date = false;
    assert(tile_cache.update(0.0f, nav_mesh, &up_to_date));
    if (up_to_date) {
      return;
    }
  }
  assert(false && "tile cache did not become ready");
}

void testFlatPlaneBuildAndPath() {
  karma::navigation::NavMesh nav_mesh;
  karma::navigation::NavMeshBuildResult build_result;
  const bool built = nav_mesh.build(makePlaneGeometry(), {}, &build_result);
  assert(built);
  assert(build_result.status == karma::navigation::NavStatus::Success);
  assert(build_result.polygon_count > 0);

  karma::navigation::NavQuery query(nav_mesh);
  const karma::navigation::NavPath path =
      query.findPath({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f});
  assert(path.success());
  assert(path.points.size() >= 2);

  const std::shared_ptr<const karma::navigation::NavMeshSnapshot> snapshot =
      nav_mesh.snapshot();
  assert(snapshot != nullptr);
  karma::navigation::NavQuery snapshot_query(*snapshot);
  const karma::navigation::NavPath snapshot_path =
      snapshot_query.findPath({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f});
  assert(snapshot_path.success());
  assert(snapshot_path.points.size() >= 2);
}

void testHoleForcesDetour() {
  karma::navigation::NavMeshBuildConfig config;
  config.agent_radius = 0.2f;

  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(makeRingGeometry(), config));

  karma::navigation::NavQuery query(nav_mesh);
  const karma::navigation::NavPath path =
      query.findPath({-4.0f, 0.1f, 0.0f}, {4.0f, 0.1f, 0.0f});
  assert(path.success());
  assert(path.points.size() > 2);
}

void testInvalidInputFailsCleanly() {
  karma::navigation::NavMesh nav_mesh;
  karma::navigation::NavMeshBuildResult build_result;
  const bool built = nav_mesh.build({}, {}, &build_result);
  assert(!built);
  assert(build_result.status == karma::navigation::NavStatus::EmptyInput);
}

void testNearestPoint() {
  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(makePlaneGeometry()));

  karma::navigation::NavQuery query(nav_mesh);
  karma::math::Vec3 nearest;
  assert(query.findNearestPoint({0.0f, 3.0f, 0.0f}, nearest));
  assert(std::abs(nearest.y) <= 0.25f);
}

void testNearestPolyFlagsAndSnapshotRefresh() {
  karma::navigation::NavMesh nav_mesh;
  karma::navigation::NavMeshBuildConfig config;
  config.default_poly_flags = karma::navigation::kNavPolyFlagWalk;
  assert(nav_mesh.build(makePlaneGeometry(), config));

  karma::navigation::NavQuery query(nav_mesh);
  uint64_t poly_ref = 0;
  karma::math::Vec3 nearest{};
  assert(query.findNearestPoly({0.0f, 0.2f, 0.0f}, poly_ref, &nearest));
  assert(poly_ref != 0);

  karma::math::Vec3 center{};
  assert(nav_mesh.polyCenter(poly_ref, center));
  assert(std::isfinite(center.x));
  assert(std::isfinite(center.y));
  assert(std::isfinite(center.z));

  uint16_t flags = 0;
  assert(nav_mesh.getPolyFlags(poly_ref, flags));
  assert((flags & karma::navigation::kNavPolyFlagWalk) != 0);
  assert(nav_mesh.setPolyFlags(poly_ref, static_cast<uint16_t>(flags | (1u << 4u))));
  assert(nav_mesh.getPolyFlags(poly_ref, flags));
  assert((flags & (1u << 4u)) != 0);

  const std::shared_ptr<const karma::navigation::NavMeshSnapshot> snapshot = nav_mesh.snapshot();
  assert(snapshot != nullptr);
  karma::navigation::NavQuery snapshot_query(*snapshot);
  karma::navigation::NavQueryFilter filter;
  filter.exclude_flags = 1u << 4u;
  const karma::navigation::NavPath blocked =
      snapshot_query.findPath({0.0f, 0.2f, 0.0f},
                              {1.0f, 0.2f, 0.0f},
                              {2.0f, 4.0f, 2.0f},
                              256,
                              filter);
  assert(!blocked.success());
}

void testGlbPrefabCollectionAppliesWorldTransform() {
  karma::scene::GlbScenePrefab prefab;
  prefab.root_node = 0;
  prefab.nodes.resize(1);
  prefab.nodes[0].world_position = {10.0f, 2.0f, -3.0f};
  prefab.nodes[0].world_scale = {2.0f, 1.0f, 2.0f};
  prefab.nodes[0].primitives.push_back({
      .name = "Plane",
      .mesh = makePlaneMesh(1.0f),
  });

  const karma::navigation::NavMeshInputGeometry geometry =
      karma::navigation::collectNavMeshGeometry(prefab);
  assert(geometry.vertices.size() == 4);
  assert(geometry.indices.size() == 6);
  assert(std::abs(geometry.vertices[0].x - 8.0f) < 0.001f);
  assert(std::abs(geometry.vertices[0].y - 2.0f) < 0.001f);
  assert(std::abs(geometry.vertices[0].z + 5.0f) < 0.001f);
}

void testWorldSurfaceCollectionUsesNavMeshSurfaceArea() {
  karma::ecs::World world;
  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .area = 2,
                         .mesh_data = std::make_shared<karma::geometry::MeshData>(makePlaneMesh()),
                     });

  const karma::navigation::NavMeshInputGeometry geometry =
      karma::navigation::collectNavMeshGeometry(world);
  assert(geometry.triangleCount() == 2);
  assert(geometry.triangle_areas.size() == 2);
  assert(geometry.triangle_areas[0] == 2);
  assert(geometry.triangle_areas[1] == 2);
}

void testAreaFlagsFilterQueries() {
  karma::navigation::NavMeshInputGeometry geometry;
  karma::navigation::appendGeometry(geometry,
                                    makePlaneMesh(),
                                    {},
                                    {},
                                    {1.0f, 1.0f, 1.0f},
                                    2);

  karma::navigation::NavMeshBuildConfig config;
  config.area_configs.push_back({
      .area = 2,
      .flags = static_cast<uint16_t>(1u << 4u),
      .cost = 2.0f,
  });

  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(geometry, config));
  karma::navigation::NavQuery query(nav_mesh);

  karma::navigation::NavQueryFilter matching_filter;
  matching_filter.include_flags = static_cast<uint16_t>(1u << 4u);
  const karma::navigation::NavPath path =
      query.findPath({-4.0f, 0.1f, -4.0f},
                     {4.0f, 0.1f, 4.0f},
                     {2.0f, 4.0f, 2.0f},
                     256,
                     matching_filter);
  assert(path.success());

  karma::navigation::NavQueryFilter rejected_filter;
  rejected_filter.include_flags = karma::navigation::kNavPolyFlagWalk;
  const karma::navigation::NavPath rejected_path =
      query.findPath({-4.0f, 0.1f, -4.0f},
                     {4.0f, 0.1f, 4.0f},
                     {2.0f, 4.0f, 2.0f},
                     256,
                     rejected_filter);
  assert(!rejected_path.success());
}

void testConvexVolumeMarksArea() {
  karma::navigation::NavMeshInputGeometry geometry = makePlaneGeometry();
  geometry.convex_volumes.push_back({
      .vertices = {{-5.0f, 0.0f, -5.0f},
                   {5.0f, 0.0f, -5.0f},
                   {5.0f, 0.0f, 5.0f},
                   {-5.0f, 0.0f, 5.0f}},
      .min_y = -1.0f,
      .max_y = 1.0f,
      .area = 2,
  });

  karma::navigation::NavMeshBuildConfig config;
  config.area_configs.push_back({
      .area = 2,
      .flags = static_cast<uint16_t>(1u << 4u),
      .cost = 1.0f,
  });

  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(geometry, config));
  karma::navigation::NavQuery query(nav_mesh);

  karma::navigation::NavQueryFilter matching_filter;
  matching_filter.include_flags = static_cast<uint16_t>(1u << 4u);
  assert(query.findPath({-4.0f, 0.1f, -4.0f},
                        {4.0f, 0.1f, 4.0f},
                        {2.0f, 4.0f, 2.0f},
                        256,
                        matching_filter).success());

  karma::navigation::NavQueryFilter rejected_filter;
  rejected_filter.include_flags = karma::navigation::kNavPolyFlagWalk;
  assert(!query.findPath({-4.0f, 0.1f, -4.0f},
                         {4.0f, 0.1f, 4.0f},
                         {2.0f, 4.0f, 2.0f},
                         256,
                         rejected_filter).success());
}

void testPartitionModesAndTiledSnapshot() {
  for (const karma::navigation::NavMeshPartitionType partition :
       {karma::navigation::NavMeshPartitionType::Watershed,
        karma::navigation::NavMeshPartitionType::Monotone,
        karma::navigation::NavMeshPartitionType::Layers}) {
    karma::navigation::NavMeshBuildConfig config;
    config.partition_type = partition;
    config.agent_radius = 0.2f;
    karma::navigation::NavMesh nav_mesh;
    assert(nav_mesh.build(makeRingGeometry(), config));
    karma::navigation::NavQuery query(nav_mesh);
    assert(query.findPath({-4.0f, 0.1f, 0.0f}, {4.0f, 0.1f, 0.0f}).success());
  }

  karma::navigation::NavMeshBuildConfig tiled_config;
  tiled_config.build_mode = karma::navigation::NavMeshBuildMode::Tiled;
  tiled_config.partition_type = karma::navigation::NavMeshPartitionType::Layers;
  tiled_config.tile_size = 16;
  tiled_config.agent_radius = 0.2f;

  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(makePlaneGeometry(), tiled_config));
  assert(nav_mesh.lastBuildResult().polygon_count > 0);

  const std::shared_ptr<const karma::navigation::NavMeshSnapshot> snapshot = nav_mesh.snapshot();
  assert(snapshot != nullptr);
  karma::navigation::NavQuery snapshot_query(*snapshot);
  assert(snapshot_query.findPath({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f}).success());

  karma::navigation::NavMesh loaded;
  assert(loaded.loadSnapshot(*snapshot));
  karma::navigation::NavQuery loaded_query(loaded);
  karma::math::Vec3 loaded_nearest;
  assert(loaded_query.findNearestPoint({0.0f, 1.0f, 0.0f}, loaded_nearest));
}

void testBuildDebugDrawArtifacts() {
  karma::navigation::NavMeshBuildConfig config;
  config.agent_radius = 0.2f;
  config.collect_build_debug_draw = true;

  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(makePlaneGeometry(), config));
  assert(nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::NavMeshEdges));
  assert(nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::NavMesh));
  assert(nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::Voxels));
  assert(nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::CompactRegions));
  assert(nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::Contours));
  assert(nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::PolyMeshDetail));
  assert(!nav_mesh.debugDrawLines(karma::navigation::NavMeshDebugDrawMode::Contours).empty());
  assert(karma::navigation::navMeshDebugDrawModeName(
             karma::navigation::NavMeshDebugDrawMode::WalkableVoxels)[0] != '\0');
}

void testAdvancedQueryHelpers() {
  karma::navigation::NavMesh nav_mesh;
  karma::navigation::NavMeshBuildConfig config;
  config.agent_radius = 0.2f;
  assert(nav_mesh.build(makePlaneGeometry(), config));

  karma::navigation::NavQuery query(nav_mesh);
  const karma::navigation::NavPath smooth =
      query.findSmoothPath({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f});
  assert(smooth.success());
  assert(smooth.points.size() > 2);

  karma::math::Vec3 random_point;
  assert(query.findRandomPoint(random_point));
  assert(query.findRandomPointAroundCircle({0.0f, 0.1f, 0.0f}, 2.0f, random_point));

  const karma::navigation::NavPolyQueryResult circle =
      query.findPolysAroundCircle({0.0f, 0.1f, 0.0f}, 2.0f);
  assert(circle.success());
  assert(!circle.polys.empty());

  const karma::navigation::NavPolyQueryResult shape =
      query.findPolysAroundShape({0.0f, 0.1f, 0.0f},
                                 {{-1.0f, 0.1f, -1.0f},
                                  {1.0f, 0.1f, -1.0f},
                                  {1.0f, 0.1f, 1.0f},
                                  {-1.0f, 0.1f, 1.0f}});
  assert(shape.success());
  assert(!shape.polys.empty());

  const karma::navigation::NavPolyQueryResult local =
      query.findLocalNeighbourhood({0.0f, 0.1f, 0.0f}, 2.0f);
  assert(local.success());
  assert(!local.polys.empty());

  const karma::navigation::NavWallSegments walls =
      query.getPolyWallSegments({0.0f, 0.1f, 0.0f});
  assert(walls.success());
  assert(!walls.segments.empty());

  const karma::navigation::NavPolyQueryResult aabb =
      query.queryPolygons({0.0f, 0.1f, 0.0f}, {2.0f, 2.0f, 2.0f});
  assert(aabb.success());
  assert(!aabb.polys.empty());

  const uint64_t poly_ref = aabb.polys.front();
  unsigned char area = 0;
  assert(nav_mesh.getPolyArea(poly_ref, area));
  assert(area == karma::navigation::kNavAreaDefault);
  assert(nav_mesh.setPolyArea(poly_ref, 2));
  assert(nav_mesh.getPolyArea(poly_ref, area));
  assert(area == 2);
  assert(nav_mesh.setPolyArea(poly_ref, karma::navigation::kNavAreaDefault));

  karma::navigation::NavPolyRefParts parts;
  assert(nav_mesh.decodePolyRef(poly_ref, parts));
  assert(parts.salt != 0);

  const auto mesh_tiles = nav_mesh.tiles();
  assert(!mesh_tiles.empty());
  assert(nav_mesh.tileRefAt(mesh_tiles.front().x, mesh_tiles.front().y, mesh_tiles.front().layer) ==
         mesh_tiles.front().ref);

  karma::navigation::NavTileStateSnapshot tile_state;
  assert(nav_mesh.storeTileState(mesh_tiles.front().ref, tile_state));
  uint16_t old_flags = 0;
  assert(nav_mesh.getPolyFlags(poly_ref, old_flags));
  assert(nav_mesh.setPolyFlags(poly_ref, static_cast<uint16_t>(old_flags | (1u << 5u))));
  assert(nav_mesh.restoreTileState(tile_state));
  uint16_t restored_flags = 0;
  assert(nav_mesh.getPolyFlags(poly_ref, restored_flags));
  assert(restored_flags == old_flags);

  const karma::navigation::NavClosestPointResult closest =
      query.closestPointOnPoly(poly_ref, {0.2f, 2.0f, 0.2f});
  assert(closest.success());
  const karma::navigation::NavClosestPointResult boundary =
      query.closestPointOnPolyBoundary(poly_ref, {10.0f, 0.1f, 10.0f});
  assert(boundary.success());

  const karma::navigation::NavRaycastResult ray =
      query.raycastDetailed({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f});
  assert(ray.success());
  assert(!ray.visited_polys.empty());

  if (aabb.polys.size() >= 2) {
    const karma::navigation::NavPortalPoints portal =
        query.portalPoints(aabb.polys[0], aabb.polys[1]);
    if (portal.success()) {
      karma::math::Vec3 midpoint;
      assert(query.edgeMidPoint(aabb.polys[0], aabb.polys[1], midpoint));
    }
  }
}

void testTileCacheDynamicObstacleBlocksAndRestoresPath() {
  karma::navigation::NavMeshBuildConfig config;
  config.tile_size = 16;
  config.agent_radius = 0.2f;
  config.agent_height = 1.0f;
  config.agent_max_climb = 0.2f;

  karma::navigation::NavMesh nav_mesh;
  karma::navigation::NavTileCache tile_cache;
  karma::navigation::NavTileCacheBuildResult result;
  const bool built = tile_cache.build(nav_mesh, makeCorridorGeometry(), config, {}, &result);
  if (!built) {
    std::cerr << "tile cache build failed: " << result.message << "\n";
  }
  assert(built);
  assert(result.status == karma::navigation::NavStatus::Success);
  assert(result.layer_count > 0);
  assert(tile_cache.tileCount() > 0);

  karma::navigation::NavQuery query(nav_mesh);
  assert(query.findPath({-4.0f, 0.1f, 0.0f}, {4.0f, 0.1f, 0.0f}).success());

  uint64_t obstacle_ref = 0;
  assert(tile_cache.addBoxObstacle({-0.5f, -0.2f, -2.0f},
                                   {0.5f, 2.0f, 2.0f},
                                   &obstacle_ref));
  assert(obstacle_ref != 0);
  updateTileCacheUntilReady(tile_cache, nav_mesh);
  assert(tile_cache.obstacleCount() == 1);
  const auto obstacles = tile_cache.obstacles();
  assert(!obstacles.empty());
  assert(obstacles[0].shape == karma::navigation::NavTileCacheObstacleShape::Box);
  assert(obstacles[0].state == karma::navigation::NavTileCacheObstacleState::Processed);

  karma::navigation::NavQuery blocked_query(nav_mesh);
  const karma::navigation::NavPath blocked =
      blocked_query.findPath({-4.0f, 0.1f, 0.0f}, {4.0f, 0.1f, 0.0f});
  assert(blocked.status != karma::navigation::NavStatus::Success || blocked.partial);

  assert(tile_cache.removeObstacle(obstacle_ref));
  updateTileCacheUntilReady(tile_cache, nav_mesh);
  karma::navigation::NavQuery restored_query(nav_mesh);
  assert(restored_query.findPath({-4.0f, 0.1f, 0.0f}, {4.0f, 0.1f, 0.0f}).success());
}

void testTileCacheBoxObstacleDiagnostics() {
  karma::navigation::NavMeshBuildConfig config;
  config.tile_size = 16;
  config.agent_radius = 0.2f;

  karma::navigation::NavMesh nav_mesh;
  karma::navigation::NavTileCache tile_cache;
  assert(tile_cache.build(nav_mesh, makePlaneGeometry(), config));

  uint64_t box_ref = 0;
  uint64_t oriented_ref = 0;
  uint64_t cylinder_ref = 0;
  assert(tile_cache.addBoxObstacle({-4.5f, -0.2f, -4.5f},
                                   {-3.5f, 1.8f, -3.5f},
                                   &box_ref));
  assert(tile_cache.addOrientedBoxObstacle({4.0f, 0.8f, 4.0f},
                                           {0.5f, 1.0f, 0.4f},
                                           0.5f,
                                           &oriented_ref));
  assert(tile_cache.addCylinderObstacle({0.0f, 0.0f, 4.0f}, 0.4f, 2.0f, &cylinder_ref));
  updateTileCacheUntilReady(tile_cache, nav_mesh);

  const auto obstacles = tile_cache.obstacles();
  bool saw_box = false;
  bool saw_oriented_box = false;
  bool saw_cylinder = false;
  for (const auto& obstacle : obstacles) {
    saw_box = saw_box || (obstacle.ref == box_ref &&
                          obstacle.shape == karma::navigation::NavTileCacheObstacleShape::Box);
    saw_oriented_box = saw_oriented_box ||
                       (obstacle.ref == oriented_ref &&
                        obstacle.shape == karma::navigation::NavTileCacheObstacleShape::OrientedBox);
    saw_cylinder = saw_cylinder ||
                   (obstacle.ref == cylinder_ref &&
                    obstacle.shape == karma::navigation::NavTileCacheObstacleShape::Cylinder);
  }
  assert(saw_box);
  assert(saw_oriented_box);
  assert(saw_cylinder);
  assert(tile_cache.tileCapacity() >= tile_cache.tileCount());
  const auto tiles = tile_cache.tiles();
  assert(!tiles.empty());
  assert(tiles.front().bounds_max.x > tiles.front().bounds_min.x);
  assert(tiles.front().bounds_max.z > tiles.front().bounds_min.z);
}

void testTileCacheSnapshotAndContentRoundTrip() {
  karma::navigation::NavMeshBuildConfig config;
  config.tile_size = 16;
  config.agent_radius = 0.2f;

  for (const karma::navigation::NavTileCacheCompression compression :
       {karma::navigation::NavTileCacheCompression::None,
        karma::navigation::NavTileCacheCompression::FastLz}) {
    karma::navigation::NavMesh nav_mesh;
    karma::navigation::NavTileCache tile_cache;
    karma::navigation::NavTileCacheBuildConfig cache_config;
    cache_config.compression = compression;
    assert(tile_cache.build(nav_mesh, makePlaneGeometry(), config, cache_config));

    const karma::navigation::NavTileCacheSnapshot snapshot = tile_cache.snapshot(nav_mesh);
    assert(snapshot.valid());

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (compression == karma::navigation::NavTileCacheCompression::None
             ? "karma_nav_none.kntc"
             : "karma_nav_fastlz.kntc");
    assert(karma::content::saveNavTileCacheSnapshot(path, snapshot));
    const karma::navigation::NavTileCacheSnapshot loaded_snapshot =
        karma::content::loadNavTileCacheSnapshot(path);
    assert(loaded_snapshot.valid());
    std::filesystem::remove(path);

    karma::navigation::NavMesh loaded_mesh;
    karma::navigation::NavTileCache loaded_cache;
    assert(loaded_cache.loadSnapshot(loaded_mesh, loaded_snapshot));
    assert(loaded_cache.tileCount() == tile_cache.tileCount());
    karma::navigation::NavQuery loaded_query(loaded_mesh);
    assert(loaded_query.findPath({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f}).success());

    uint64_t obstacle_ref = 0;
    assert(loaded_cache.addCylinderObstacle({0.0f, 0.0f, 0.0f}, 0.5f, 2.0f, &obstacle_ref));
    updateTileCacheUntilReady(loaded_cache, loaded_mesh);
    assert(loaded_cache.obstacleCount() == 1);
  }
}

void testCrowdMovesAgentToTarget() {
  karma::navigation::NavMeshBuildConfig nav_config;
  nav_config.agent_radius = 0.2f;
  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(makePlaneGeometry(), nav_config));

  karma::navigation::NavCrowd crowd;
  karma::navigation::NavCrowdConfig crowd_config;
  crowd_config.max_agents = 8;
  crowd_config.max_agent_radius = 0.4f;
  assert(crowd.init(nav_mesh, crowd_config));

  karma::navigation::NavCrowdAgentParams params;
  params.radius = 0.2f;
  params.height = 1.0f;
  params.max_speed = 2.5f;
  params.max_acceleration = 12.0f;
  const int agent_id = crowd.addAgent({-4.0f, 0.1f, 0.0f}, params);
  assert(agent_id >= 0);
  assert(crowd.activeAgentCount() == 1);
  assert(crowd.requestMoveTarget(agent_id, {4.0f, 0.1f, 0.0f}));

  karma::navigation::NavCrowdAgentInfo info;
  for (int i = 0; i < 80; ++i) {
    crowd.update(0.1f);
    assert(crowd.agentInfo(agent_id, info));
    if (std::abs(info.position.x - 4.0f) < 0.5f) {
      break;
    }
  }
  assert(info.active);
  assert(info.state == karma::navigation::NavCrowdAgentState::Walking);
  assert(std::abs(info.position.x - 4.0f) < 0.75f);

  assert(crowd.requestMoveVelocity(agent_id, {-1.0f, 0.0f, 0.0f}));
  crowd.update(0.2f);
  assert(crowd.agentInfo(agent_id, info));
  assert(info.target_state == karma::navigation::NavCrowdTargetState::Velocity);

  const karma::navigation::NavCrowdDebugSnapshot debug = crowd.debugSnapshot({
      .enabled = true,
      .all_agents = true,
  });
  assert(!debug.empty());
  assert(debug.agents.front().agent_id == agent_id);
  assert(!debug.agents.front().corridor_polys.empty());
}

void testOffMeshConnectionBridgesGap() {
  karma::geometry::MeshData mesh;
  appendQuad(mesh, {-5.0f, 0.0f, -2.0f}, {-1.0f, 0.0f, -2.0f},
             {-1.0f, 0.0f, 2.0f}, {-5.0f, 0.0f, 2.0f});
  appendQuad(mesh, {1.0f, 0.0f, -2.0f}, {5.0f, 0.0f, -2.0f},
             {5.0f, 0.0f, 2.0f}, {1.0f, 0.0f, 2.0f});

  karma::navigation::NavMeshInputGeometry geometry;
  karma::navigation::appendGeometry(geometry, mesh);
  geometry.off_mesh_connections.push_back({
      .start = {-1.15f, 0.0f, 0.0f},
      .end = {1.15f, 0.0f, 0.0f},
      .radius = 0.5f,
      .area = karma::navigation::kNavAreaDefault,
      .flags = karma::navigation::kNavPolyFlagWalk | karma::navigation::kNavPolyFlagOffMesh,
      .bidirectional = true,
      .user_id = 7,
  });

  karma::navigation::NavMeshBuildConfig config;
  config.agent_radius = 0.2f;
  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(geometry, config));

  karma::navigation::NavQuery query(nav_mesh);
  const karma::navigation::NavPath path =
      query.findPath({-4.0f, 0.1f, 0.0f}, {4.0f, 0.1f, 0.0f});
  assert(path.success());
  bool saw_off_mesh = false;
  for (uint8_t flags : path.point_flags) {
    saw_off_mesh = saw_off_mesh ||
                   ((flags & karma::navigation::NavPathPointFlagOffMeshConnection) != 0);
  }
  assert(saw_off_mesh);
}

void testSlicedPathCompletes() {
  karma::navigation::NavMesh nav_mesh;
  assert(nav_mesh.build(makePlaneGeometry()));

  karma::navigation::NavQuery query(nav_mesh);
  const karma::navigation::NavStatus begin_status =
      query.beginSlicedPath({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f});
  assert(begin_status == karma::navigation::NavStatus::InProgress ||
         begin_status == karma::navigation::NavStatus::Success);

  bool done = begin_status == karma::navigation::NavStatus::Success;
  for (int i = 0; i < 64 && !done; ++i) {
    const karma::navigation::NavStatus status = query.updateSlicedPath(1, done);
    assert(status == karma::navigation::NavStatus::InProgress ||
           status == karma::navigation::NavStatus::Success ||
           status == karma::navigation::NavStatus::PartialPath);
  }
  assert(done);

  const karma::navigation::NavPath path = query.finalizeSlicedPath();
  assert(path.success());
  assert(path.points.size() >= 2);
}

void testNavigationSystemBuildsAndMovesAgent() {
  karma::ecs::World world;

  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data = std::make_shared<karma::geometry::MeshData>(makePlaneMesh()),
                     });

  const auto nav_entity = world.createEntity();
  karma::components::NavMeshComponent nav_component;
  nav_component.build_config.agent_radius = 0.2f;
  world.add(nav_entity, std::move(nav_component));

  const auto agent_entity = world.createEntity();
  world.add(agent_entity, karma::components::TransformComponent{{-4.0f, 0.0f, -4.0f}});
  karma::components::NavMeshAgentComponent agent;
  agent.speed = 8.0f;
  agent.stopping_distance = 0.05f;
  agent.nav_mesh_entity = nav_entity;
  world.add(agent_entity, std::move(agent));

  karma::navigation::NavigationSystem system;
  system.update(world, 0.0f);
  assert(world.get<karma::components::NavMeshComponent>(nav_entity).built);
  assert(karma::navigation::NavigationSystem::requestMoveTo(
      world, agent_entity, {4.0f, 0.0f, 4.0f}));
  assert(world.get<karma::components::NavMeshAgentComponent>(agent_entity).status ==
         karma::components::NavMeshAgentStatus::Requested);

  system.update(world, 0.0f);
  const auto submitted_status =
      world.get<karma::components::NavMeshAgentComponent>(agent_entity).status;
  assert(submitted_status == karma::components::NavMeshAgentStatus::PathPending ||
         submitted_status == karma::components::NavMeshAgentStatus::PathResolved ||
         submitted_status == karma::components::NavMeshAgentStatus::Moving ||
         submitted_status == karma::components::NavMeshAgentStatus::Arrived);

  for (int i = 0; i < 200; ++i) {
    system.update(world, 0.1f);
    if (world.get<karma::components::NavMeshAgentComponent>(agent_entity).status ==
        karma::components::NavMeshAgentStatus::Arrived) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  const auto& transform = world.get<karma::components::TransformComponent>(agent_entity);
  const auto& moved_agent = world.get<karma::components::NavMeshAgentComponent>(agent_entity);
  const karma::navigation::NavigationSystemStats& stats = system.stats();
  assert(moved_agent.status == karma::components::NavMeshAgentStatus::Arrived);
  assert(stats.submitted_requests >= 1);
  assert(stats.completed_requests >= 1);
  assert(stats.last_path_status == karma::navigation::NavStatus::Success ||
         stats.last_path_status == karma::navigation::NavStatus::PartialPath);
  assert(std::abs(transform.getPosition().x - 4.0f) < 0.25f);
  assert(std::abs(transform.getPosition().z - 4.0f) < 0.25f);
}

void testNavigationSystemTileCacheObstacleComponent() {
  karma::ecs::World world;

  auto surface_mesh = std::make_shared<karma::geometry::MeshData>();
  appendQuad(*surface_mesh,
             {-5.0f, 0.0f, -1.0f},
             {5.0f, 0.0f, -1.0f},
             {5.0f, 0.0f, 1.0f},
             {-5.0f, 0.0f, 1.0f});

  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data = surface_mesh,
                     });

  const auto nav_entity = world.createEntity();
  karma::components::NavMeshComponent nav_component;
  nav_component.build_config.tile_size = 16;
  nav_component.build_config.agent_radius = 0.2f;
  nav_component.build_config.agent_height = 1.0f;
  nav_component.build_config.agent_max_climb = 0.2f;
  world.add(nav_entity, std::move(nav_component));
  world.add(nav_entity, karma::components::NavTileCacheComponent{});

  const auto obstacle_entity = world.createEntity();
  world.add(obstacle_entity, karma::components::TransformComponent{{0.0f, 0.0f, 0.0f}});
  karma::components::NavTileCacheObstacleComponent obstacle;
  obstacle.nav_mesh_entity = nav_entity;
  obstacle.shape = karma::navigation::NavTileCacheObstacleShape::Box;
  obstacle.bounds_min = {-0.5f, -0.2f, -2.0f};
  obstacle.bounds_max = {0.5f, 2.0f, 2.0f};
  world.add(obstacle_entity, obstacle);

  karma::navigation::NavigationSystem system;
  for (int i = 0; i < 8; ++i) {
    system.update(world, 0.0f);
  }

  auto& nav_component_ref = world.get<karma::components::NavMeshComponent>(nav_entity);
  auto& cache_component_ref = world.get<karma::components::NavTileCacheComponent>(nav_entity);
  assert(nav_component_ref.built);
  assert(cache_component_ref.built);
  assert(cache_component_ref.tile_cache.obstacleCount() == 1);

  karma::navigation::NavQuery blocked_query(nav_component_ref.nav_mesh);
  const karma::navigation::NavPath blocked =
      blocked_query.findPath({-4.0f, 0.1f, 0.0f}, {4.0f, 0.1f, 0.0f});
  assert(blocked.status != karma::navigation::NavStatus::Success || blocked.partial);

  auto& obstacle_ref =
      world.get<karma::components::NavTileCacheObstacleComponent>(obstacle_entity);
  obstacle_ref.remove_requested = true;
  for (int i = 0; i < 8; ++i) {
    system.update(world, 0.0f);
  }

  karma::navigation::NavQuery restored_query(nav_component_ref.nav_mesh);
  assert(restored_query.findPath({-4.0f, 0.1f, 0.0f},
                                 {4.0f, 0.1f, 0.0f}).success());
}

void testNavigationSystemCrowdAgentComponent() {
  karma::ecs::World world;

  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data = std::make_shared<karma::geometry::MeshData>(makePlaneMesh()),
                     });

  const auto nav_entity = world.createEntity();
  karma::components::NavMeshComponent nav_component;
  nav_component.build_config.agent_radius = 0.2f;
  world.add(nav_entity, std::move(nav_component));
  karma::components::NavCrowdComponent crowd_component;
  crowd_component.config.max_agents = 8;
  crowd_component.config.max_agent_radius = 0.4f;
  crowd_component.debug_request.enabled = true;
  world.add(nav_entity, std::move(crowd_component));

  const auto agent_entity = world.createEntity();
  world.add(agent_entity, karma::components::TransformComponent{{-4.0f, 0.1f, 0.0f}});
  karma::components::NavCrowdAgentComponent crowd_agent;
  crowd_agent.crowd_entity = nav_entity;
  crowd_agent.params.radius = 0.2f;
  crowd_agent.params.height = 1.0f;
  crowd_agent.params.max_speed = 2.5f;
  crowd_agent.params.max_acceleration = 12.0f;
  crowd_agent.stopping_distance = 0.5f;
  world.add(agent_entity, crowd_agent);

  karma::navigation::NavigationSystem system;
  system.update(world, 0.0f);
  assert(world.get<karma::components::NavMeshComponent>(nav_entity).built);
  assert(world.get<karma::components::NavCrowdComponent>(nav_entity).built);
  assert(karma::navigation::NavigationSystem::requestCrowdMoveTo(
      world, agent_entity, {4.0f, 0.1f, 0.0f}));

  for (int i = 0; i < 100; ++i) {
    system.update(world, 0.1f);
    const auto& agent =
        world.get<karma::components::NavCrowdAgentComponent>(agent_entity);
    if (agent.reached_destination) {
      break;
    }
  }

  const auto& transform = world.get<karma::components::TransformComponent>(agent_entity);
  const auto& agent = world.get<karma::components::NavCrowdAgentComponent>(agent_entity);
  const auto& crowd = world.get<karma::components::NavCrowdComponent>(nav_entity);
  assert(agent.agent_id >= 0);
  assert(agent.state == karma::navigation::NavCrowdAgentState::Walking);
  assert(agent.reached_destination);
  assert(!crowd.debug_snapshot.empty());
  assert(std::abs(transform.getPosition().x - 4.0f) < 0.75f);
}

void testCrowdAgentPlayerControllerVelocityMode() {
  karma::ecs::World world;

  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data = std::make_shared<karma::geometry::MeshData>(makePlaneMesh()),
                     });

  const auto nav_entity = world.createEntity();
  karma::components::NavMeshComponent nav_component;
  nav_component.build_config.agent_radius = 0.2f;
  world.add(nav_entity, std::move(nav_component));
  karma::components::NavCrowdComponent crowd_component;
  crowd_component.config.max_agents = 8;
  crowd_component.config.max_agent_radius = 0.4f;
  world.add(nav_entity, std::move(crowd_component));

  const auto agent_entity = world.createEntity();
  const karma::math::Vec3 start{-4.0f, 0.1f, 0.0f};
  world.add(agent_entity, karma::components::TransformComponent{start});
  world.add(agent_entity, karma::components::BoxColliderComponent{});
  world.add(agent_entity, karma::components::PlayerControllerComponent{});
  karma::components::NavCrowdAgentComponent crowd_agent;
  crowd_agent.crowd_entity = nav_entity;
  crowd_agent.movement_mode = karma::components::NavCrowdMovementMode::PlayerControllerVelocity;
  crowd_agent.params.radius = 0.2f;
  crowd_agent.params.height = 1.0f;
  crowd_agent.params.max_speed = 2.5f;
  crowd_agent.params.max_acceleration = 12.0f;
  world.add(agent_entity, crowd_agent);

  karma::navigation::NavigationSystem system;
  system.update(world, 0.0f);
  assert(karma::navigation::NavigationSystem::requestCrowdMoveTo(
      world, agent_entity, {4.0f, 0.1f, 0.0f}));
  for (int i = 0; i < 20; ++i) {
    system.update(world, 0.1f);
  }

  const auto& transform = world.get<karma::components::TransformComponent>(agent_entity);
  const auto& controller = world.get<karma::components::PlayerControllerComponent>(agent_entity);
  assert(std::abs(transform.getPosition().x - start.x) < 0.001f);
  assert(std::abs(transform.getPosition().z - start.z) < 0.001f);
  assert(std::abs(controller.desiredVelocity().x) > 0.001f ||
         std::abs(controller.desiredVelocity().z) > 0.001f);
}

void testReplacementRequestKeepsCurrentPathMoving() {
  karma::ecs::World world;

  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data = std::make_shared<karma::geometry::MeshData>(makePlaneMesh()),
                     });

  const auto nav_entity = world.createEntity();
  karma::components::NavMeshComponent nav_component;
  nav_component.build_config.agent_radius = 0.2f;
  world.add(nav_entity, std::move(nav_component));

  const auto agent_entity = world.createEntity();
  world.add(agent_entity, karma::components::TransformComponent{{-4.0f, 0.0f, -4.0f}});
  karma::components::NavMeshAgentComponent agent;
  agent.speed = 1.0f;
  agent.stopping_distance = 0.05f;
  agent.nav_mesh_entity = nav_entity;
  world.add(agent_entity, std::move(agent));

  karma::navigation::NavigationSystem system;
  system.update(world, 0.0f);
  assert(karma::navigation::NavigationSystem::requestMoveTo(
      world, agent_entity, {4.0f, 0.0f, 4.0f}));

  for (int i = 0; i < 200; ++i) {
    system.update(world, 0.0f);
    const auto& pending_agent =
        world.get<karma::components::NavMeshAgentComponent>(agent_entity);
    if (!pending_agent.path.empty() &&
        pending_agent.next_waypoint < pending_agent.path.size()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  const auto& active_agent =
      world.get<karma::components::NavMeshAgentComponent>(agent_entity);
  assert(!active_agent.path.empty());
  assert(active_agent.next_waypoint < active_agent.path.size());

  const auto before_request =
      world.get<karma::components::TransformComponent>(agent_entity).getPosition();
  assert(karma::navigation::NavigationSystem::requestMoveTo(
      world, agent_entity, {4.0f, 0.0f, -4.0f}));
  const auto& requested_agent =
      world.get<karma::components::NavMeshAgentComponent>(agent_entity);
  assert(!requested_agent.path.empty());
  assert(requested_agent.next_waypoint < requested_agent.path.size());

  system.update(world, 0.1f);

  const auto after_update =
      world.get<karma::components::TransformComponent>(agent_entity).getPosition();
  const auto& moved_agent =
      world.get<karma::components::NavMeshAgentComponent>(agent_entity);
  assert(std::abs(after_update.x - before_request.x) > 0.001f ||
         std::abs(after_update.z - before_request.z) > 0.001f);
  assert(moved_agent.status != karma::components::NavMeshAgentStatus::Failed);
}

void testExampleWorldGlbCanBake() {
  const std::filesystem::path world_path =
      resolveRepoPath("examples/assets/world.glb");
  assert(std::filesystem::exists(world_path));

  karma::ecs::World world;
  const auto world_entity = world.createEntity();
  world.add(world_entity, karma::components::TransformComponent{});
  world.add(world_entity, karma::components::NavMeshSurfaceComponent{
                              .mesh_key = world_path.string(),
                          });

  const karma::navigation::NavMeshInputGeometry geometry =
      karma::navigation::collectNavMeshGeometry(world);
  assert(geometry.triangleCount() > 0);

  karma::navigation::NavMeshBuildConfig config;
  config.cell_size = 0.25f;
  config.cell_height = 0.1f;
  config.agent_height = 1.8f;
  config.agent_radius = 0.55f;
  config.agent_max_climb = 0.7f;
  config.region_min_size = 6.0f;
  config.region_merge_size = 18.0f;

  karma::navigation::NavMesh nav_mesh;
  karma::navigation::NavMeshBuildResult result;
  assert(nav_mesh.build(geometry, config, &result));
  assert(result.polygon_count > 0);

  karma::navigation::NavQuery query(nav_mesh);
  karma::math::Vec3 nearest;
  assert(query.findNearestPoint({0.0f, 0.0f, 0.0f}, nearest, {5.0f, 10.0f, 5.0f}));
}

}  // namespace

int main() {
  testFlatPlaneBuildAndPath();
  testHoleForcesDetour();
  testInvalidInputFailsCleanly();
  testNearestPoint();
  testNearestPolyFlagsAndSnapshotRefresh();
  testGlbPrefabCollectionAppliesWorldTransform();
  testWorldSurfaceCollectionUsesNavMeshSurfaceArea();
  testAreaFlagsFilterQueries();
  testConvexVolumeMarksArea();
  testPartitionModesAndTiledSnapshot();
  testBuildDebugDrawArtifacts();
  testAdvancedQueryHelpers();
  testTileCacheDynamicObstacleBlocksAndRestoresPath();
  testTileCacheBoxObstacleDiagnostics();
  testTileCacheSnapshotAndContentRoundTrip();
  testCrowdMovesAgentToTarget();
  testOffMeshConnectionBridgesGap();
  testSlicedPathCompletes();
  testNavigationSystemBuildsAndMovesAgent();
  testNavigationSystemTileCacheObstacleComponent();
  testNavigationSystemCrowdAgentComponent();
  testCrowdAgentPlayerControllerVelocityMode();
  testReplacementRequestKeepsCurrentPathMoving();
  testExampleWorldGlbCanBake();
  std::cout << "navmesh tests passed\n";
  return 0;
}
