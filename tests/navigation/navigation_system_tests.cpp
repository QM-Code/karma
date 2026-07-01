#include "navmesh_test_utils.h"

#include "karma/assets.h"
#include "karma/assets.h"

#include <fstream>

namespace karma::tests::navigation {
namespace {

void setEnvVar(const char* name, const char* value) {
#if defined(_WIN32)
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

std::filesystem::path navCacheTestDir() {
  static const std::filesystem::path dir = [] {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("karma-nav-cache-tests-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(root);
    setEnvVar("KARMA_NAV_CACHE", "1");
    setEnvVar("KARMA_NAV_CACHE_DIR", root.string().c_str());
    return root;
  }();
  return dir;
}

karma::world::Entity addPlaneSurface(karma::world::World& world, float half_extent = 5.0f) {
  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data = std::make_shared<karma::world::MeshData>(
                             makePlaneMesh(half_extent)),
                     });
  return surface;
}

karma::world::Entity addCachedNavMesh(karma::world::World& world,
                                    karma::navigation::NavMeshBuildConfig config = {}) {
  const auto nav_entity = world.createEntity();
  config.agent_radius = config.agent_radius > 0.0f ? config.agent_radius : 0.2f;
  karma::components::NavMeshComponent nav_component;
  nav_component.cache.enabled = true;
  nav_component.build_config = std::move(config);
  world.add(nav_entity, std::move(nav_component));
  return nav_entity;
}

std::vector<std::filesystem::path> navCacheFiles(const std::filesystem::path& root,
                                                 const char* extension) {
  std::vector<std::filesystem::path> paths;
  if (!std::filesystem::exists(root)) {
    return paths;
  }
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file() && entry.path().extension() == extension) {
      paths.push_back(entry.path());
    }
  }
  return paths;
}

}  // namespace

void testNavigationSystemBuildsAndMovesAgent() {
  karma::world::World world;

  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data = std::make_shared<karma::world::MeshData>(makePlaneMesh()),
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
  karma::world::World world;

  auto surface_mesh = std::make_shared<karma::world::MeshData>();
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

void testNavigationSystemNavMeshCacheHitAndInvalidation() {
  const std::filesystem::path cache_dir = navCacheTestDir();

  {
    karma::world::World world;
    addPlaneSurface(world);
    karma::navigation::NavMeshBuildConfig config;
    config.agent_radius = 0.2f;
    config.collect_build_debug_draw = true;
    const auto nav_entity = addCachedNavMesh(world, config);

    karma::navigation::NavigationSystem system;
    system.update(world, 0.0f);

    const auto& nav = world.get<karma::components::NavMeshComponent>(nav_entity);
    assert(nav.built);
    assert(system.stats().last_cache_miss);
    assert(system.stats().last_cache_write);
    assert(system.stats().cache_misses == 1);
    assert(system.stats().cache_writes == 1);
    assert(!nav.nav_mesh.config().collect_build_debug_draw);
    assert(nav.nav_mesh.boundsMin().x <= -5.0f);
    assert(nav.nav_mesh.boundsMax().z >= 5.0f);
  }

  {
    karma::world::World world;
    addPlaneSurface(world);
    karma::navigation::NavMeshBuildConfig config;
    config.agent_radius = 0.2f;
    config.collect_build_debug_draw = true;
    const auto nav_entity = addCachedNavMesh(world, config);

    karma::navigation::NavigationSystem system;
    system.update(world, 0.0f);

    const auto& nav = world.get<karma::components::NavMeshComponent>(nav_entity);
    assert(nav.built);
    assert(system.stats().last_cache_hit);
    assert(system.stats().cache_hits == 1);
    assert(nav.last_build_result.status == karma::navigation::NavStatus::Success);
    assert(nav.last_build_result.vertex_count == 4);
    assert(nav.last_build_result.triangle_count == 2);
    assert(nav.nav_mesh.boundsMin().x <= -5.0f);
    assert(nav.nav_mesh.boundsMax().z >= 5.0f);
    karma::navigation::NavQuery query(nav.nav_mesh);
    assert(query.findPath({-4.0f, 0.1f, -4.0f}, {4.0f, 0.1f, 4.0f}).success());
  }

  {
    karma::world::World world;
    addPlaneSurface(world);
    karma::navigation::NavMeshBuildConfig config;
    config.agent_radius = 0.3f;
    const auto nav_entity = addCachedNavMesh(world, config);

    karma::navigation::NavigationSystem system;
    system.update(world, 0.0f);

    assert(world.get<karma::components::NavMeshComponent>(nav_entity).built);
    assert(system.stats().last_cache_miss);
    assert(system.stats().last_cache_write);
  }

  {
    karma::world::World world;
    addPlaneSurface(world, 4.0f);
    karma::navigation::NavMeshBuildConfig config;
    config.agent_radius = 0.2f;
    const auto nav_entity = addCachedNavMesh(world, config);

    karma::navigation::NavigationSystem system;
    system.update(world, 0.0f);

    assert(world.get<karma::components::NavMeshComponent>(nav_entity).built);
    assert(system.stats().last_cache_miss);
    assert(system.stats().last_cache_write);
  }

  const std::vector<std::filesystem::path> files = navCacheFiles(cache_dir, ".knav");
  assert(!files.empty());
  for (const std::filesystem::path& path : files) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "corrupt";
  }

  {
    karma::world::World world;
    addPlaneSurface(world);
    karma::navigation::NavMeshBuildConfig config;
    config.agent_radius = 0.2f;
    config.collect_build_debug_draw = true;
    const auto nav_entity = addCachedNavMesh(world, config);

    karma::navigation::NavigationSystem system;
    system.update(world, 0.0f);

    assert(world.get<karma::components::NavMeshComponent>(nav_entity).built);
    assert(system.stats().last_cache_miss);
    assert(system.stats().last_cache_write);
  }
}

void testNavigationSystemTileCacheCacheHitAndObstacleResync() {
  (void)navCacheTestDir();

  auto setup_world = [](karma::world::World& world, bool with_obstacle) {
    auto surface_mesh = std::make_shared<karma::world::MeshData>();
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

    karma::navigation::NavMeshBuildConfig config;
    config.tile_size = 16;
    config.agent_radius = 0.2f;
    config.agent_height = 1.0f;
    config.agent_max_climb = 0.2f;
    const auto nav_entity = addCachedNavMesh(world, config);
    world.add(nav_entity, karma::components::NavTileCacheComponent{});

    karma::world::Entity obstacle_entity{};
    if (with_obstacle) {
      obstacle_entity = world.createEntity();
      world.add(obstacle_entity, karma::components::TransformComponent{{0.0f, 0.0f, 0.0f}});
      karma::components::NavTileCacheObstacleComponent obstacle;
      obstacle.nav_mesh_entity = nav_entity;
      obstacle.shape = karma::navigation::NavTileCacheObstacleShape::Box;
      obstacle.bounds_min = {-0.5f, -0.2f, -2.0f};
      obstacle.bounds_max = {0.5f, 2.0f, 2.0f};
      world.add(obstacle_entity, obstacle);
    }
    return std::pair{nav_entity, obstacle_entity};
  };

  {
    karma::world::World world;
    const auto [nav_entity, obstacle_entity] = setup_world(world, false);
    (void)obstacle_entity;
    karma::navigation::NavigationSystem system;
    system.update(world, 0.0f);
    assert(world.get<karma::components::NavMeshComponent>(nav_entity).built);
    assert(world.get<karma::components::NavTileCacheComponent>(nav_entity).built);
    assert(system.stats().last_cache_miss);
    assert(system.stats().last_cache_write);
  }

  {
    karma::world::World world;
    const auto [nav_entity, obstacle_entity] = setup_world(world, true);
    karma::navigation::NavigationSystem system;
    for (int i = 0; i < 8; ++i) {
      system.update(world, 0.0f);
    }

    auto& nav = world.get<karma::components::NavMeshComponent>(nav_entity);
    auto& cache = world.get<karma::components::NavTileCacheComponent>(nav_entity);
    assert(nav.built);
    assert(cache.built);
    assert(system.stats().cache_hits >= 1);
    assert(cache.tile_cache.obstacleCount() == 1);

    karma::navigation::NavQuery blocked_query(nav.nav_mesh);
    const karma::navigation::NavPath blocked =
        blocked_query.findPath({-4.0f, 0.1f, 0.0f}, {4.0f, 0.1f, 0.0f});
    assert(blocked.status != karma::navigation::NavStatus::Success || blocked.partial);

    auto& obstacle =
        world.get<karma::components::NavTileCacheObstacleComponent>(obstacle_entity);
    obstacle.remove_requested = true;
    for (int i = 0; i < 8; ++i) {
      system.update(world, 0.0f);
    }

    karma::navigation::NavQuery restored_query(nav.nav_mesh);
    assert(restored_query.findPath({-4.0f, 0.1f, 0.0f},
                                   {4.0f, 0.1f, 0.0f}).success());
  }
}

void testNavigationSystemBuildDebugDrawBypassesCacheOnce() {
  (void)navCacheTestDir();

  karma::world::World world;
  addPlaneSurface(world);
  karma::navigation::NavMeshBuildConfig config;
  config.agent_radius = 0.2f;
  config.collect_build_debug_draw = true;
  const auto nav_entity = addCachedNavMesh(world, config);

  karma::navigation::NavigationSystem system;
  system.update(world, 0.0f);
  auto& nav = world.get<karma::components::NavMeshComponent>(nav_entity);
  assert(nav.built);
  assert(!nav.nav_mesh.config().collect_build_debug_draw);

  assert(karma::navigation::NavigationSystem::requestBuildDebugDraw(world, nav_entity));
  system.update(world, 0.0f);
  assert(nav.built);
  assert(!system.stats().last_cache_hit);
  assert(!system.stats().last_cache_miss);
  assert(nav.nav_mesh.config().collect_build_debug_draw);
  assert(nav.nav_mesh.hasDebugDrawMode(karma::navigation::NavMeshDebugDrawMode::Contours));
  assert(!nav.nav_mesh.debugDrawLines(
      karma::navigation::NavMeshDebugDrawMode::Contours).empty());
}

void testNavigationSystemCrowdAgentComponent() {
  karma::world::World world;

  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data = std::make_shared<karma::world::MeshData>(makePlaneMesh()),
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
  karma::world::World world;

  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data = std::make_shared<karma::world::MeshData>(makePlaneMesh()),
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
  karma::world::World world;

  const auto surface = world.createEntity();
  world.add(surface, karma::components::TransformComponent{});
  world.add(surface, karma::components::NavMeshSurfaceComponent{
                         .mesh_data = std::make_shared<karma::world::MeshData>(makePlaneMesh()),
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
  karma::assets::AssetRegistry assets;
  std::string diagnostic;
  auto package = karma::assets::importAssetPackage(assets, package_path, &diagnostic);
  assert(package.has_value());
  assert(diagnostic.empty());

  karma::world::World world;
  const auto world_entity = world.createEntity();
  world.add(world_entity, karma::components::TransformComponent{});
  constexpr const char* kWorldMeshKey = "examples/mesh/world";
  const karma::world::MeshData* mesh = assets.findMeshAsset(kWorldMeshKey);
  assert(mesh != nullptr);
  world.add(world_entity, karma::components::NavMeshSurfaceComponent{
                              .mesh_data = std::make_shared<karma::world::MeshData>(*mesh),
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
