#include "navmesh_test_utils.h"

#include "karma/content/assets/asset_package.h"
#include "karma/content/assets/asset_registry.h"

namespace karma::tests::navigation {

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

void testCrowdAgentCharacterControllerVelocityMode() {
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
  world.add(agent_entity, karma::components::ColliderComponent::box());
  world.add(agent_entity, karma::components::CharacterControllerComponent{});
  karma::components::NavCrowdAgentComponent crowd_agent;
  crowd_agent.crowd_entity = nav_entity;
  crowd_agent.movement_mode =
      karma::components::NavCrowdMovementMode::CharacterControllerVelocity;
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
  const auto& controller = world.get<karma::components::CharacterControllerComponent>(agent_entity);
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
  const std::filesystem::path package_path =
      resolveRepoPath("examples/assets/common_meshes/world");
  assert(std::filesystem::exists(package_path / "assets.package.json"));
  karma::content::AssetRegistry assets;
  std::string diagnostic;
  auto package = karma::content::importAssetPackage(assets, package_path, &diagnostic);
  assert(package.has_value());
  assert(diagnostic.empty());

  karma::ecs::World world;
  const auto world_entity = world.createEntity();
  world.add(world_entity, karma::components::TransformComponent{});
  constexpr const char* kWorldMeshKey = "examples/mesh/world";
  const karma::geometry::MeshData* mesh = assets.findMeshAsset(kWorldMeshKey);
  assert(mesh != nullptr);
  world.add(world_entity, karma::components::NavMeshSurfaceComponent{
                              .mesh_data = std::make_shared<karma::geometry::MeshData>(*mesh),
                              .mesh_asset_key = kWorldMeshKey,
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

}  // namespace karma::tests::navigation
