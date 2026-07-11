#include "navmesh_test_utils.h"

#include "karma/assets.h"
#include "karma/assets.h"
#include "karma/prefabs.h"
#include "karma/scenes.h"

#include <algorithm>
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

void testSceneRuntimeLoadsBakedNavigationAndFallsBack() {
  static const bool serializer_registered = [] {
    karma::prefabs::ensureBuiltinComponentSerializers();
    return karma::prefabs::componentSerializerRegistry().registerSerializer(
        karma::prefabs::ComponentSerializer{
            .type_name = "SceneBakeTestNavOwner",
            .has = [](const karma::world::World& world,
                      karma::world::Entity entity) {
              return world.has<karma::components::NavMeshComponent>(entity);
            },
            .serialize = [](const karma::world::World&,
                            karma::world::Entity) {
              return nlohmann::json::object();
            },
            .deserialize = [](karma::world::World& world,
                              karma::world::Entity entity,
                              const nlohmann::json& json) {
              if (!json.is_object()) {
                return false;
              }
              karma::components::NavMeshComponent component{};
              component.build_on_start = false;
              component.rebuild_requested = false;
              world.add(entity, std::move(component));
              return true;
            },
        });
  }();
  assert(serializer_registered);

  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("karma-scene-baked-nav-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  const std::filesystem::path artifact = root / "bakes/plane.knav";
  const std::filesystem::path manifest = root / "bakes/main.kbake.json";
  std::filesystem::create_directories(manifest.parent_path());

  karma::navigation::NavMesh source;
  karma::navigation::NavMeshBuildConfig config{};
  config.agent_radius = 0.2f;
  assert(source.build(makePlaneGeometry(), config));
  const auto source_snapshot = source.snapshot();
  assert(source_snapshot != nullptr && source_snapshot->valid());
  assert(karma::assets::saveNavMeshSnapshot(artifact, *source_snapshot));

  auto write_manifest = [&](const nlohmann::json& json) {
    std::ofstream stream(manifest, std::ios::trunc);
    stream << json.dump(2);
    assert(static_cast<bool>(stream));
  };

  karma::scenes::SceneDocument document{};
  document.name = "Baked navigation runtime";
  document.source_path = root / "scene.kscene.json";
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "root",
      .components = nlohmann::json{
          {"SceneBakeTestNavOwner", nlohmann::json::object()},
      },
  });
  document.bakes.push_back(karma::scenes::SceneBakeDesc{
      .id = "main",
      .path = "bakes/main.kbake.json",
  });
  const std::string scene_fingerprint = karma::scenes::sceneBakeFingerprint(
      document, document.bakes.front());
  const std::string binding_fingerprint = karma::assets::hashString(
      scene_fingerprint + "\nentity:root\nnavmesh");
  write_manifest(nlohmann::json{
      {"schema", "karma.scene_bake"},
      {"version", 2},
      {"scene_fingerprint", scene_fingerprint},
      {"navigation_bindings",
       nlohmann::json::array({nlohmann::json{
           {"owner_id", "entity:root"},
           {"kind", "navmesh"},
           {"path", "bakes/plane.knav"},
           {"source_fingerprint", binding_fingerprint},
       }})},
  });

  {
    karma::world::World world;
    karma::world::Scene scene;
    karma::assets::AssetRegistry assets;
    karma::scenes::SceneInstantiateResult instance =
        karma::scenes::instantiateScene(world, scene, assets, document);
    assert(instance.success);
    const karma::world::Entity owner = instance.find("root");
    assert(instance.navigation_owners_by_id.at("entity:root") == owner);
    const auto& component =
        world.get<karma::components::NavMeshComponent>(owner);
    assert(component.built);
    assert(!component.rebuild_requested);
    karma::navigation::NavQuery query(component.nav_mesh);
    assert(query.findPath({-4.0f, 0.1f, -4.0f},
                          {4.0f, 0.1f, 4.0f}).success());
    assert(karma::scenes::destroyScene(world, scene, instance));
  }

  {
    std::ofstream stream(artifact, std::ios::binary | std::ios::trunc);
    stream << "corrupt";
  }
  {
    karma::world::World world;
    karma::world::Scene scene;
    karma::assets::AssetRegistry assets;
    karma::scenes::SceneInstantiateResult instance =
        karma::scenes::instantiateScene(world, scene, assets, document);
    assert(instance.success);
    const auto& component = world.get<karma::components::NavMeshComponent>(
        instance.find("root"));
    assert(!component.built);
    assert(component.rebuild_requested);
    assert(!instance.diagnostics.empty());
    assert(karma::scenes::destroyScene(world, scene, instance));
  }

  write_manifest(nlohmann::json{
      {"schema", "karma.scene_bake"},
      {"version", 1},
      {"scene_fingerprint", "legacy"},
      {"nav_cache_files", nlohmann::json::array()},
  });
  {
    karma::world::World world;
    karma::world::Scene scene;
    karma::assets::AssetRegistry assets;
    karma::scenes::SceneInstantiateResult instance =
        karma::scenes::instantiateScene(world, scene, assets, document);
    assert(instance.success);
    const auto& component = world.get<karma::components::NavMeshComponent>(
        instance.find("root"));
    assert(!component.built);
    assert(component.rebuild_requested);
    assert(karma::scenes::destroyScene(world, scene, instance));
  }
  std::filesystem::remove_all(root);
}

void testStaticMembershipInheritanceControlsNavigation() {
  static const bool serializer_registered = [] {
    karma::prefabs::ensureBuiltinComponentSerializers();
    return karma::prefabs::componentSerializerRegistry().registerSerializer(
        karma::prefabs::ComponentSerializer{
            .type_name = "SceneBakeTestStaticSurface",
            .has = [](const karma::world::World& world,
                      karma::world::Entity entity) {
              return world.has<karma::components::StaticComponent>(entity) ||
                     world.has<karma::components::NavMeshSurfaceComponent>(
                         entity) ||
                     world.has<karma::components::InstancedMeshComponent>(
                         entity);
            },
            .serialize = [](const karma::world::World&,
                            karma::world::Entity) {
              return nlohmann::json::object();
            },
            .deserialize = [](karma::world::World& world,
                              karma::world::Entity entity,
                              const nlohmann::json& json) {
              if (!json.is_object()) {
                return false;
              }
              if (json.contains("static_enabled")) {
                if (!json["static_enabled"].is_boolean()) {
                  return false;
                }
                karma::components::StaticComponent membership{};
                membership.enabled = json["static_enabled"].get<bool>();
                membership.include_descendants = true;
                membership.flags = karma::components::StaticComponentNavigation;
                world.add(entity, membership);
              }
              if (json.value("surface", false)) {
                world.add(entity,
                          karma::components::NavMeshSurfaceComponent{
                              .mesh_data =
                                  std::make_shared<karma::world::MeshData>(
                                      makePlaneMesh(2.0f)),
                          });
              }
              if (json.value("instanced", false)) {
                karma::components::InstancedMeshComponent instanced{};
                instanced.instances.push_back(
                    karma::components::MeshInstance{
                        .position = {7.0f, 0.0f, 0.0f},
                    });
                world.add(entity, std::move(instanced));
              }
              return true;
            },
        });
  }();
  assert(serializer_registered);

  karma::scenes::SceneDocument document{};
  document.name = "Static inheritance";
  document.entities = {
      karma::scenes::SceneEntity{
          .id = "group",
          .components = nlohmann::json{
              {"SceneBakeTestStaticSurface",
               nlohmann::json{{"static_enabled", true}}},
          },
      },
      karma::scenes::SceneEntity{
          .id = "included",
          .parent_id = "group",
          .components = nlohmann::json{
              {"SceneBakeTestStaticSurface",
               nlohmann::json{{"surface", true}, {"instanced", true}}},
          },
      },
      karma::scenes::SceneEntity{
          .id = "opt_out",
          .parent_id = "group",
          .components = nlohmann::json{
              {"SceneBakeTestStaticSurface",
               nlohmann::json{{"static_enabled", false}, {"surface", true}}},
          },
      },
      karma::scenes::SceneEntity{
          .id = "under_opt_out",
          .parent_id = "opt_out",
          .components = nlohmann::json{
              {"SceneBakeTestStaticSurface",
               nlohmann::json{{"surface", true}}},
          },
      },
  };

  karma::world::World world;
  karma::world::Scene scene;
  karma::assets::AssetRegistry assets;
  karma::scenes::SceneInstantiateResult instance =
      karma::scenes::instantiateScene(world, scene, assets, document);
  assert(instance.success);
  assert(world.has<karma::components::StaticComponent>(
      instance.find("included")));
  assert(!world.get<karma::components::StaticComponent>(
                    instance.find("opt_out"))
              .enabled);
  assert(world.has<karma::components::StaticComponent>(
      instance.find("under_opt_out")));
  assert(!world.get<karma::components::StaticComponent>(
                    instance.find("under_opt_out"))
              .enabled);

  const karma::navigation::NavMeshInputGeometry geometry =
      karma::navigation::collectNavMeshGeometry(world, &assets);
  assert(geometry.vertices.size() == 4u);
  assert(geometry.triangleCount() == 2u);
  assert(std::abs(geometry.vertices.front().x - 5.0f) < 0.001f);

  const karma::scenes::SceneStaticBuildResult metadata =
      karma::scenes::buildSceneStaticMetadata(
          document,
          instance,
          world,
          scene,
          assets,
          karma::scenes::SceneStaticBuildDesc{.build_mesh_bounds = false});
  assert(metadata.success);
  std::vector<std::string> ids;
  for (const auto& transform : metadata.transforms) {
    ids.push_back(transform.static_component_id);
  }
  std::sort(ids.begin(), ids.end());
  assert(std::find(ids.begin(), ids.end(), "entity:group") != ids.end());
  assert(std::find(ids.begin(), ids.end(), "entity:included") != ids.end());
  assert(std::find(ids.begin(), ids.end(), "entity:opt_out") == ids.end());
  assert(std::find(ids.begin(), ids.end(), "entity:under_opt_out") ==
         ids.end());
  assert(karma::scenes::destroyScene(world, scene, instance));
}

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

void testNavigationSystemFollowsPrecomputedPath() {
  karma::world::World world;

  const auto agent_entity = world.createEntity();
  world.add(agent_entity, karma::components::TransformComponent{{-4.0f, 0.0f, -4.0f}});
  karma::components::NavMeshAgentComponent agent;
  agent.speed = 8.0f;
  agent.stopping_distance = 0.05f;
  world.add(agent_entity, std::move(agent));

  karma::navigation::NavPath path;
  path.status = karma::navigation::NavStatus::Success;
  path.points = {
      {-4.0f, 0.0f, -4.0f},
      {-4.0f, 0.0f, 4.0f},
      {4.0f, 0.0f, 4.0f},
  };
  path.point_flags = {
      karma::navigation::NavPathPointFlagStart,
      karma::navigation::NavPathPointFlagNone,
      karma::navigation::NavPathPointFlagEnd,
  };
  assert(karma::navigation::NavigationSystem::requestFollowPath(
      world, agent_entity, path));

  karma::navigation::NavigationSystem system;
  for (int i = 0; i < 200; ++i) {
    system.update(world, 0.1f);
    if (world.get<karma::components::NavMeshAgentComponent>(agent_entity).status ==
        karma::components::NavMeshAgentStatus::Arrived) {
      break;
    }
  }

  const auto& moved_agent =
      world.get<karma::components::NavMeshAgentComponent>(agent_entity);
  const auto& transform =
      world.get<karma::components::TransformComponent>(agent_entity);
  assert(moved_agent.status == karma::components::NavMeshAgentStatus::Arrived);
  assert(system.stats().submitted_requests == 0);
  assert(std::abs(transform.getPosition().x - 4.0f) < 0.25f);
  assert(std::abs(transform.getPosition().z - 4.0f) < 0.25f);
}

void testNavigationSystemFollowPathSkipsPassedPrefix() {
  karma::world::World world;

  const auto agent_entity = world.createEntity();
  world.add(agent_entity, karma::components::TransformComponent{{2.0f, 0.0f, 0.0f}});
  karma::components::NavMeshAgentComponent agent;
  agent.speed = 2.0f;
  agent.stopping_distance = 0.05f;
  world.add(agent_entity, std::move(agent));

  karma::navigation::NavPath path;
  path.status = karma::navigation::NavStatus::Success;
  path.points = {
      {0.0f, 0.0f, 0.0f},
      {1.0f, 0.0f, 0.0f},
      {4.0f, 0.0f, 0.0f},
  };
  path.point_speed_multipliers = {1.0f, 0.5f, 0.25f};
  assert(karma::navigation::NavigationSystem::requestFollowPath(
      world, agent_entity, path));

  const auto& requested_agent =
      world.get<karma::components::NavMeshAgentComponent>(agent_entity);
  assert(requested_agent.path.size() == 2u);
  assert(std::abs(requested_agent.path.front().x - 2.0f) < 0.001f);
  assert(std::abs(requested_agent.path.back().x - 4.0f) < 0.001f);
  assert(requested_agent.path_point_speed_multipliers.size() == 2u);
  assert(std::abs(requested_agent.path_point_speed_multipliers.front() - 0.25f) <
         0.001f);
  assert(std::abs(requested_agent.path_point_speed_multipliers.back() - 0.25f) <
         0.001f);
  assert(requested_agent.next_waypoint == 1u);

  karma::navigation::NavigationSystem system;
  system.update(world, 0.1f);

  const auto& transform =
      world.get<karma::components::TransformComponent>(agent_entity);
  assert(transform.getPosition().x > 2.0f);
}

void testNavigationSystemFollowPathUsesSpeedMultipliers() {
  karma::world::World world;

  const auto agent_entity = world.createEntity();
  world.add(agent_entity,
            karma::components::TransformComponent{{0.0f, 0.0f, 0.0f}});
  karma::components::NavMeshAgentComponent agent;
  agent.speed = 4.0f;
  agent.stopping_distance = 0.001f;
  world.add(agent_entity, std::move(agent));

  karma::navigation::NavPath path;
  path.status = karma::navigation::NavStatus::Success;
  path.points = {
      {0.0f, 0.0f, 0.0f},
      {4.0f, 0.0f, 0.0f},
  };
  path.point_speed_multipliers = {1.0f, 0.5f};
  assert(karma::navigation::NavigationSystem::requestFollowPath(
      world, agent_entity, path));

  karma::navigation::NavigationSystem system;
  system.update(world, 1.0f);

  const auto& transform =
      world.get<karma::components::TransformComponent>(agent_entity);
  const auto& moved_agent =
      world.get<karma::components::NavMeshAgentComponent>(agent_entity);
  assert(std::abs(transform.getPosition().x - 2.0f) < 0.05f);
  assert(moved_agent.status == karma::components::NavMeshAgentStatus::Moving);
}

void testNavigationSystemConsumesTimeThroughShortWaypoints() {
  karma::world::World world;

  const auto agent_entity = world.createEntity();
  world.add(agent_entity,
            karma::components::TransformComponent{{0.0f, 0.0f, 0.0f}});
  karma::components::NavMeshAgentComponent agent;
  agent.speed = 1.0f;
  agent.stopping_distance = 0.15f;
  world.add(agent_entity, std::move(agent));

  karma::navigation::NavPath path;
  path.status = karma::navigation::NavStatus::Success;
  for (int index = 0; index <= 20; ++index) {
    path.points.push_back({static_cast<float>(index) * 0.2f, 0.0f, 0.0f});
  }
  assert(karma::navigation::NavigationSystem::requestFollowPath(
      world, agent_entity, path));

  karma::navigation::NavigationSystem system;
  system.update(world, 1.0f);

  const auto& transform =
      world.get<karma::components::TransformComponent>(agent_entity);
  assert(transform.getPosition().x <= 1.05f);
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
