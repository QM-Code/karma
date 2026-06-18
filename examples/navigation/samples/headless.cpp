#include "demo_data.h"
#include "karma/headless.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace karma::demo {
namespace {

navigation::NavQueryFilter filterFor(const QueryCase& query) {
  navigation::NavQueryFilter filter;
  filter.include_flags = query.include_flags;
  filter.exclude_flags = query.exclude_flags;
  return filter;
}

bool buildNavMesh(const navigation::NavMeshInputGeometry& geometry,
                  const navigation::NavMeshBuildConfig& config,
                  navigation::NavMesh& nav_mesh,
                  std::string_view label) {
  navigation::NavMeshBuildResult result;
  if (!nav_mesh.build(geometry, config, &result)) {
    std::cerr << label << " build failed: "
              << navigation::navStatusName(result.status) << " - "
              << result.message << "\n";
    return false;
  }
  std::cout << label << ": polygons=" << result.polygon_count
            << " triangles=" << result.triangle_count << "\n";
  return true;
}

int runPathCases(const navigation::NavMesh& nav_mesh,
                 const std::vector<QueryCase>& cases,
                 bool smooth) {
  navigation::NavQuery query(nav_mesh);
  int success_count = 0;
  int total = 0;
  for (const QueryCase& test : cases) {
    if (test.kind != "pf") {
      continue;
    }
    ++total;
    const navigation::NavQueryFilter filter = filterFor(test);
    const navigation::NavPath path = smooth
        ? query.findSmoothPath(test.start, test.end, {2.0f, 4.0f, 2.0f}, {}, 1024, filter)
        : query.findPath(test.start, test.end, {2.0f, 4.0f, 2.0f}, 256, filter);
    if (path.success() && !path.points.empty()) {
      ++success_count;
    }
  }
  std::cout << "  path cases: " << success_count << "/" << total
            << (smooth ? " smooth" : " straight") << "\n";
  return success_count;
}

int runRaycastCases(const navigation::NavMesh& nav_mesh,
                    const std::vector<QueryCase>& cases) {
  navigation::NavQuery query(nav_mesh);
  int success_count = 0;
  int total = 0;
  for (const QueryCase& test : cases) {
    if (test.kind != "rc") {
      continue;
    }
    ++total;
    const navigation::NavPath hit =
        query.raycast(test.start, test.end, {2.0f, 4.0f, 2.0f}, 256, filterFor(test));
    if (hit.success() && !hit.points.empty()) {
      ++success_count;
    }
  }
  std::cout << "  raycast cases: " << success_count << "/" << total << "\n";
  return success_count;
}

bool runTesterQueries(navigation::NavMesh& nav_mesh, const QueryCase& test) {
  navigation::NavQuery query(nav_mesh);
  math::Vec3 nearest;
  math::Vec3 target;
  math::Vec3 moved;
  float height = 0.0f;
  float wall_distance = 0.0f;
  math::Vec3 wall_hit;
  math::Vec3 wall_normal;
  math::Vec3 random_point;

  std::vector<std::string_view> failures;
  auto record = [&](std::string_view name, bool passed) {
    if (!passed) {
      failures.push_back(name);
    }
    return passed;
  };

  const bool nearest_ok = record("findNearestPoint", query.findNearestPoint(test.start, nearest));
  const math::Vec3 anchor = nearest_ok ? nearest : test.start;
  const bool target_ok = record("findNearestTarget", query.findNearestPoint(test.end, target));
  if (!target_ok) {
    target = test.end;
  }

  record("moveAlongSurface", query.moveAlongSurface(anchor, target, moved));
  record("findHeight", query.findHeight(anchor, height));
  record("findDistanceToWall",
         query.findDistanceToWall(anchor, 8.0f, wall_distance, &wall_hit, &wall_normal));
  record("findRandomPoint", query.findRandomPoint(random_point));
  record("findRandomPointAroundCircle",
         query.findRandomPointAroundCircle(anchor, 8.0f, random_point));
  record("findPolysAroundCircle", query.findPolysAroundCircle(anchor, 8.0f).success());
  record("findPolysAroundShape",
         query.findPolysAroundShape(anchor,
                                    {{anchor.x - 1.0f, anchor.y, anchor.z - 1.0f},
                                     {anchor.x + 1.0f, anchor.y, anchor.z - 1.0f},
                                     {anchor.x + 1.0f, anchor.y, anchor.z + 1.0f},
                                     {anchor.x - 1.0f, anchor.y, anchor.z + 1.0f}})
             .success());
  record("findLocalNeighbourhood", query.findLocalNeighbourhood(anchor, 8.0f).success());
  record("getPolyWallSegments", query.getPolyWallSegments(anchor).success());

  const uint32_t pruned = nav_mesh.pruneUnreachable(anchor, static_cast<uint16_t>(1u << 15u));
  const bool ok = failures.empty();
  std::cout << "  tester queries: " << (ok ? "ok" : "failed")
            << " pruned=" << pruned << "\n";
  if (!ok) {
    std::cerr << "  failed queries:";
    for (std::string_view failure : failures) {
      std::cerr << ' ' << failure;
    }
    std::cerr << "\n";
  }
  return ok;
}

bool runSoloMeshExample(const TestCaseFile& nav_tests, const MeshGeometry& mesh) {
  std::cout << "\n[Recast Solo Mesh]\n";
  navigation::NavMesh nav_mesh;
  if (!buildNavMesh(mesh.geometry,
                    recastBuildConfig(navigation::NavMeshBuildMode::Solo),
                    nav_mesh,
                    "solo")) {
    return false;
  }

  const int straight_success = runPathCases(nav_mesh, nav_tests.queries, false);
  const int smooth_success = runPathCases(nav_mesh, nav_tests.queries, true);

  const auto snapshot = nav_mesh.snapshot();
  navigation::NavMesh loaded;
  const bool snapshot_ok = snapshot != nullptr && loaded.loadSnapshot(*snapshot);
  std::cout << "  snapshot reload: " << (snapshot_ok ? "ok" : "failed") << "\n";
  return straight_success > 0 && smooth_success > 0 && snapshot_ok;
}

bool runTileMeshExample(const TestCaseFile& ray_tests, const MeshGeometry& mesh) {
  std::cout << "\n[Recast Tile Mesh]\n";
  navigation::NavMeshBuildConfig config = recastBuildConfig(navigation::NavMeshBuildMode::Tiled);
  config.partition_type = navigation::NavMeshPartitionType::Layers;

  navigation::NavMesh nav_mesh;
  if (!buildNavMesh(mesh.geometry, config, nav_mesh, "tiled")) {
    return false;
  }

  const int ray_success = runRaycastCases(nav_mesh, ray_tests.queries);
  const QueryCase* first_case = nullptr;
  for (const QueryCase& query : ray_tests.queries) {
    if (query.kind == "rc") {
      first_case = &query;
      break;
    }
  }
  bool tile_ops_ok = false;
  if (first_case != nullptr) {
    navigation::NavMeshBuildResult tile_result;
    tile_ops_ok = nav_mesh.rebuildTile(mesh.geometry, first_case->start, &tile_result) &&
                  nav_mesh.removeTile(first_case->start);
    if (tile_ops_ok) {
      tile_ops_ok = nav_mesh.rebuildTile(mesh.geometry, first_case->start, &tile_result);
    }
  }
  std::cout << "  tile edit: " << (tile_ops_ok ? "ok" : "failed") << "\n";
  return ray_success > 0 && tile_ops_ok;
}

bool runTempObstaclesExample(const TestCaseFile& nav_tests, const MeshGeometry& mesh) {
  std::cout << "\n[Recast Temp Obstacles]\n";
  navigation::NavMeshBuildConfig nav_config = recastBuildConfig(navigation::NavMeshBuildMode::Tiled);
  nav_config.partition_type = navigation::NavMeshPartitionType::Layers;

  navigation::NavMesh nav_mesh;
  navigation::NavTileCache tile_cache;
  navigation::NavTileCacheBuildResult build_result;
  if (!tile_cache.build(nav_mesh, mesh.geometry, nav_config, {}, &build_result)) {
    std::cerr << "tile cache build failed: " << build_result.message << "\n";
    return false;
  }
  std::cout << "  cache tiles=" << build_result.tile_count
            << " layers=" << build_result.layer_count
            << " bytes=" << build_result.compressed_bytes << "\n";

  const QueryCase* first_path = nullptr;
  for (const QueryCase& query : nav_tests.queries) {
    if (query.kind == "pf") {
      first_path = &query;
      break;
    }
  }
  const Bounds bounds = computeBounds(mesh.geometry);
  const math::Vec3 center = first_path != nullptr
      ? midpoint(first_path->start, first_path->end)
      : centerOf(bounds);

  uint64_t cylinder = 0;
  uint64_t box = 0;
  uint64_t oriented_box = 0;
  const bool added =
      tile_cache.addCylinderObstacle(center, 1.0f, 2.0f, &cylinder) &&
      tile_cache.addBoxObstacle({center.x - 1.0f, center.y - 0.5f, center.z - 1.0f},
                                {center.x + 1.0f, center.y + 2.0f, center.z + 1.0f},
                                &box) &&
      tile_cache.addOrientedBoxObstacle({center.x + 3.0f, center.y + 0.5f, center.z},
                                        {0.8f, 1.2f, 0.8f},
                                        0.5f,
                                        &oriented_box);
  bool up_to_date = false;
  for (int i = 0; i < 16 && !up_to_date; ++i) {
    tile_cache.update(0.0f, nav_mesh, &up_to_date);
  }
  std::cout << "  obstacles=" << tile_cache.obstacleCount()
            << " up_to_date=" << (up_to_date ? "true" : "false") << "\n";

  tile_cache.removeObstacle(cylinder);
  tile_cache.removeObstacle(box);
  tile_cache.removeObstacle(oriented_box);
  up_to_date = false;
  for (int i = 0; i < 16 && !up_to_date; ++i) {
    tile_cache.update(0.0f, nav_mesh, &up_to_date);
  }
  std::cout << "  clear obstacles up_to_date=" << (up_to_date ? "true" : "false") << "\n";
  return added && up_to_date;
}

bool runCrowdExample(const TestCaseFile& nav_tests, const MeshGeometry& mesh) {
  std::cout << "\n[Recast Crowd]\n";
  navigation::NavMesh nav_mesh;
  if (!buildNavMesh(mesh.geometry,
                    recastBuildConfig(navigation::NavMeshBuildMode::Tiled),
                    nav_mesh,
                    "crowd navmesh")) {
    return false;
  }

  const QueryCase* first_path = nullptr;
  for (const QueryCase& query : nav_tests.queries) {
    if (query.kind == "pf") {
      first_path = &query;
      break;
    }
  }
  if (first_path == nullptr) {
    return false;
  }

  navigation::NavCrowd crowd;
  navigation::NavCrowdConfig config;
  config.max_agents = 16;
  config.max_agent_radius = 0.8f;
  if (!crowd.init(nav_mesh, config)) {
    return false;
  }

  navigation::NavCrowdAgentParams agent_params;
  agent_params.radius = 0.6f;
  agent_params.height = 2.0f;
  agent_params.max_speed = 3.5f;
  agent_params.max_acceleration = 8.0f;
  agent_params.update_flags |= navigation::NavCrowdUpdateFlagSeparation;
  std::vector<int> agents;
  for (int i = 0; i < 6; ++i) {
    const math::Vec3 offset{static_cast<float>(i % 3) * 0.8f, 0.0f,
                            static_cast<float>(i / 3) * 0.8f};
    const int id = crowd.addAgent({first_path->start.x + offset.x,
                                   first_path->start.y,
                                   first_path->start.z + offset.z},
                                  agent_params);
    if (id >= 0 && crowd.requestMoveTarget(id, first_path->end)) {
      agents.push_back(id);
    }
  }
  for (int step = 0; step < 80; ++step) {
    crowd.update(0.1f);
  }

  int active = 0;
  for (const int id : agents) {
    navigation::NavCrowdAgentInfo info;
    if (crowd.agentInfo(id, info) && info.active) {
      ++active;
    }
  }
  std::cout << "  active agents=" << active
            << " capacity=" << crowd.agentCapacity() << "\n";
  return active == static_cast<int>(agents.size()) && active > 0;
}

bool runDebugExample(const TestCaseFile& nav_tests,
                     const MeshGeometry& nav_mesh,
                     const MeshGeometry& undulating) {
  std::cout << "\n[Recast Debug]\n";
  bool ok = true;
  for (const navigation::NavMeshPartitionType partition :
       {navigation::NavMeshPartitionType::Watershed,
        navigation::NavMeshPartitionType::Monotone,
        navigation::NavMeshPartitionType::Layers}) {
    navigation::NavMeshBuildConfig config = recastBuildConfig(navigation::NavMeshBuildMode::Solo);
    config.partition_type = partition;
    navigation::NavMesh mesh;
    const bool built = buildNavMesh(undulating.geometry, config, mesh, "debug partition");
    ok = ok && built;
  }

  navigation::NavMeshInputGeometry marked = nav_mesh.geometry;
  const Bounds bounds = computeBounds(marked);
  const math::Vec3 center = centerOf(bounds);
  marked.convex_volumes.push_back({
      .vertices = {{center.x - 4.0f, center.y, center.z - 4.0f},
                   {center.x + 4.0f, center.y, center.z - 4.0f},
                   {center.x + 4.0f, center.y, center.z + 4.0f},
                   {center.x - 4.0f, center.y, center.z + 4.0f}},
      .min_y = bounds.min.y - 1.0f,
      .max_y = bounds.max.y + 1.0f,
      .area = 2,
  });
  marked.off_mesh_connections.push_back({
      .start = {center.x - 8.0f, center.y, center.z},
      .end = {center.x + 8.0f, center.y, center.z},
      .radius = 1.0f,
      .area = navigation::kNavAreaDefault,
      .flags = navigation::kNavPolyFlagWalk | navigation::kNavPolyFlagOffMesh,
      .bidirectional = true,
      .user_id = 42,
  });

  navigation::NavMesh mesh;
  ok = ok && buildNavMesh(marked,
                          recastBuildConfig(navigation::NavMeshBuildMode::Solo),
                          mesh,
                          "debug volumes/offmesh");
  if (ok) {
    QueryCase query;
    query.start = center;
    query.end = {center.x + 4.0f, center.y, center.z + 4.0f};
    for (const QueryCase& candidate : nav_tests.queries) {
      if (candidate.kind == "pf") {
        query = candidate;
        break;
      }
    }
    ok = runTesterQueries(mesh, query) && ok;
  }
  return ok;
}

bool runAllExamples(std::string_view scenario) {
  const auto nav_tests = loadTestCase(recastAssetPath("test_cases/nav_mesh_test.txt"));
  const auto ray_tests = loadTestCase(recastAssetPath("test_cases/raycast_test.txt"));
  if (!nav_tests || !ray_tests) {
    std::cerr << "Failed to load Recast test case files.\n";
    return false;
  }

  const MeshGeometry nav_mesh = loadMeshGeometry("nav_test.obj");
  const MeshGeometry dungeon = loadMeshGeometry("dungeon.obj");
  const MeshGeometry undulating = loadMeshGeometry("undulating.obj");
  if (nav_mesh.geometry.empty() || dungeon.geometry.empty() || undulating.geometry.empty()) {
    std::cerr << "Failed to load copied Recast OBJ mesh assets.\n";
    return false;
  }

  bool ok = true;
  auto should_run = [&](std::string_view name) {
    return scenario.empty() || scenario == "all" || scenario == name;
  };
  if (should_run("solo")) {
    ok = runSoloMeshExample(*nav_tests, nav_mesh) && ok;
  }
  if (should_run("tile")) {
    ok = runTileMeshExample(*ray_tests, nav_mesh) && ok;
  }
  if (should_run("temp-obstacles")) {
    ok = runTempObstaclesExample(*nav_tests, dungeon) && ok;
  }
  if (should_run("crowd")) {
    ok = runCrowdExample(*nav_tests, nav_mesh) && ok;
  }
  if (should_run("debug")) {
    ok = runDebugExample(*nav_tests, nav_mesh, undulating) && ok;
  }
  return ok;
}

}  // namespace
}  // namespace karma::demo

int main(int argc, char** argv) {
  const std::string_view scenario = argc > 1 ? std::string_view{argv[1]} : std::string_view{"all"};
  const bool ok = karma::demo::runAllExamples(scenario);
  std::cout << "\nRecast navigation examples " << (ok ? "passed" : "failed") << "\n";
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
