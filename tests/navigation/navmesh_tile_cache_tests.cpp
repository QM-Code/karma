#include "navmesh_test_utils.h"

namespace karma::tests::navigation {

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
    assert(karma::assets::saveNavTileCacheSnapshot(path, snapshot));
    const karma::navigation::NavTileCacheSnapshot loaded_snapshot =
        karma::assets::loadNavTileCacheSnapshot(path);
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

}  // namespace karma::tests::navigation
