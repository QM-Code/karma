#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

#include "karma/world/components/nav_mesh.h"
#include "karma/world/components/nav_mesh_agent.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"
#include "karma/simulation/navigation/nav_geometry.h"
#include "karma/simulation/navigation/nav_mesh.h"
#include "karma/simulation/navigation/navigation_system.h"
#include "karma/content/importers/glb_scene_import.h"

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

karma::renderer::MeshData makePlaneMesh(float half_extent = 5.0f) {
  karma::renderer::MeshData mesh;
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

void appendQuad(karma::renderer::MeshData& mesh,
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
  karma::renderer::MeshData mesh;
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
  assert(std::abs(nearest.y) < 0.05f);
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
                         .mesh_data = std::make_shared<karma::renderer::MeshData>(makePlaneMesh()),
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

void testOffMeshConnectionBridgesGap() {
  karma::renderer::MeshData mesh;
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
                         .mesh_data = std::make_shared<karma::renderer::MeshData>(makePlaneMesh()),
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

void testReplacementRequestKeepsCurrentPathMoving() {
  karma::ecs::World world;

  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data = std::make_shared<karma::renderer::MeshData>(makePlaneMesh()),
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
  testGlbPrefabCollectionAppliesWorldTransform();
  testWorldSurfaceCollectionUsesNavMeshSurfaceArea();
  testAreaFlagsFilterQueries();
  testOffMeshConnectionBridgesGap();
  testSlicedPathCompletes();
  testNavigationSystemBuildsAndMovesAgent();
  testReplacementRequestKeepsCurrentPathMoving();
  testExampleWorldGlbCanBake();
  std::cout << "navmesh tests passed\n";
  return 0;
}
