#include "karma/scenes.h"
#include "karma/app.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

#define KARMA_REQUIRE(expression)                                      \
  do {                                                                \
    if (!(expression)) {                                               \
      std::cerr << "Requirement failed: " << #expression << " at "   \
                << __FILE__ << ":" << __LINE__ << '\n';              \
      std::abort();                                                    \
    }                                                                 \
  } while (false)

std::filesystem::path makeTempDir() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      ("karma_scene_runtime_tests_" + std::to_string(now));
  std::filesystem::create_directories(dir);
  return dir;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << text;
}

bool nearlyEqual(float a, float b) {
  return std::abs(a - b) <= 0.0001f;
}

void requireVec3(const karma::math::Vec3& value,
                 float x,
                 float y,
                 float z) {
  KARMA_REQUIRE(nearlyEqual(value.x, x));
  KARMA_REQUIRE(nearlyEqual(value.y, y));
  KARMA_REQUIRE(nearlyEqual(value.z, z));
}

std::filesystem::path findRepoRoot() {
  std::vector<std::filesystem::path> starts{std::filesystem::current_path()};
  std::filesystem::path source_path = std::filesystem::path(__FILE__);
  if (source_path.is_absolute()) {
    starts.push_back(source_path.parent_path());
  }

  for (std::filesystem::path start : starts) {
    for (std::filesystem::path cursor = start; !cursor.empty(); cursor = cursor.parent_path()) {
      if (std::filesystem::exists(cursor / "examples/assets/world.glb")) {
        return cursor;
      }
      if (cursor == cursor.parent_path()) {
        break;
      }
    }
  }
  return {};
}

std::string simplePrefabJson() {
  return R"({
  "version": 2,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "Runtime Prefab",
      "parent": null,
      "components": {
        "TransformComponent": {
          "position": [0, 0, 0],
          "rotation": [0, 0, 0, 1],
          "scale": [1, 1, 1]
        }
      }
    }
  ]
})";
}

std::string startupSceneJson() {
  return R"({
  "version": 1,
  "name": "Startup Scene",
  "entities": [
    { "id": "root", "name": "Startup Root" },
    { "id": "camera_entity", "parent": "root" },
    { "id": "light_entity", "parent": "root" }
  ],
  "cameras": [
    { "id": "startup_camera", "entity": "camera_entity", "primary": true }
  ],
  "lights": [
    { "id": "startup_light", "entity": "light_entity", "type": "directional", "intensity": 2.0 }
  ]
})";
}

karma::scenes::SceneDocument runtimeDocument(const std::filesystem::path& dir) {
  karma::scenes::SceneDocument document{};
  document.name = "Runtime Fixture";
  document.source_path = dir / "runtime.kscene.json";
  document.asset_packages.push_back(karma::scenes::SceneAssetRef{
      .id = "scene_assets",
      .path = "assets.package.json",
      .type = "asset_package",
  });
  document.gltf_scenes.push_back(karma::scenes::SceneAssetRef{
      .id = "tests/scene_runtime/world",
      .path = "world.glb",
      .type = "gltf_scene",
      .asset_package_id = "scene_assets",
  });

  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "root",
      .name = "Scene Root",
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "camera_entity",
      .name = "Main Camera",
      .parent_id = "root",
      .transform = karma::scenes::SceneTransform{
          .position = {0.0f, 1.0f, -4.0f},
      },
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "light_entity",
      .name = "Key Light",
      .parent_id = "root",
      .transform = karma::scenes::SceneTransform{
          .position = {1.0f, 3.0f, 1.0f},
      },
  });

  document.prefab_instances.push_back(karma::scenes::ScenePrefabInstance{
      .id = "marker_prefab",
      .prefab_path = "prefabs/marker/prefab.json",
      .parent_entity_id = "root",
      .transform = karma::scenes::SceneTransform{
          .position = {2.0f, 0.0f, 0.0f},
      },
  });

  karma::components::CameraComponent camera{};
  camera.is_primary = true;
  camera.fov_y_degrees = 70.0f;
  document.cameras.push_back(karma::scenes::SceneCamera{
      .id = "main_camera",
      .entity_id = "camera_entity",
      .component = camera,
  });

  karma::components::LightComponent light{};
  light.type = karma::components::LightComponent::Type::Directional;
  light.intensity = 3.0f;
  light.casts_shadows = true;
  document.lights.push_back(karma::scenes::SceneLight{
      .id = "sun",
      .entity_id = "light_entity",
      .component = light,
  });
  return document;
}

karma::world::MeshData staticBoundsMesh() {
  karma::world::MeshData mesh{};
  mesh.vertices = {
      {-1.0f, -2.0f, -3.0f},
      {2.0f, 3.0f, 4.0f},
      {0.0f, 1.0f, -1.0f},
  };
  mesh.indices = {0u, 1u, 2u};
  return mesh;
}

void testStaticMetadataBuildCapturesTransformsAndBoundsWithoutFreezingRuntime() {
  karma::assets::AssetRegistry assets;
  KARMA_REQUIRE(assets.registerMeshAsset("tests/static_bounds/mesh", staticBoundsMesh()));

  karma::scenes::SceneDocument document{};
  document.name = "Static Metadata Fixture";
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "root",
      .transform = karma::scenes::SceneTransform{
          .position = {10.0f, 0.0f, 0.0f},
      },
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "static_node",
      .parent_id = "root",
      .transform = karma::scenes::SceneTransform{
          .position = {1.0f, 2.0f, 3.0f},
          .scale = {2.0f, 1.0f, 1.0f},
      },
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "dynamic_node",
      .parent_id = "root",
      .transform = karma::scenes::SceneTransform{
          .position = {5.0f, 0.0f, 0.0f},
      },
  });
  document.static_components.push_back(karma::scenes::SceneStaticComponent{
      .id = "static_render",
      .entity_id = "static_node",
      .mesh_asset_key = "tests/static_bounds/mesh",
      .transform = true,
      .render = true,
  });
  document.static_components.push_back(karma::scenes::SceneStaticComponent{
      .id = "dynamic_render",
      .entity_id = "dynamic_node",
      .mesh_asset_key = "tests/static_bounds/mesh",
      .transform = false,
      .render = true,
  });
  document.static_components.push_back(karma::scenes::SceneStaticComponent{
      .id = "gltf_static_render",
      .entity_id = "static_node",
      .gltf_scene_id = "tests/static_bounds/gltf",
      .mesh_asset_key = "tests/static_bounds/mesh",
      .transform = true,
      .render = true,
  });

  karma::world::World world;
  karma::world::Scene scene;
  karma::scenes::SceneInstantiateDesc instantiate_desc{};
  instantiate_desc.instantiate_gltf_scenes = false;
  instantiate_desc.instantiate_prefabs = false;

  karma::scenes::SceneInstantiateResult instance =
      karma::scenes::instantiateScene(world, scene, assets, document, instantiate_desc);
  KARMA_REQUIRE(instance.success);

  karma::scenes::SceneStaticBuildResult static_metadata =
      karma::scenes::buildSceneStaticMetadata(document, instance, world, scene, assets);
  KARMA_REQUIRE(static_metadata.success);
  KARMA_REQUIRE(static_metadata.transforms.size() == 1);
  KARMA_REQUIRE(static_metadata.bounds.size() == 1);
  KARMA_REQUIRE(static_metadata.skipped_static_components == 2);

  const karma::scenes::SceneStaticTransform& baked_transform =
      static_metadata.transforms.front();
  KARMA_REQUIRE(baked_transform.static_component_id == "static_render");
  requireVec3(baked_transform.local.position, 1.0f, 2.0f, 3.0f);
  requireVec3(baked_transform.world.position, 11.0f, 2.0f, 3.0f);
  requireVec3(baked_transform.world.scale, 2.0f, 1.0f, 1.0f);

  const karma::scenes::SceneStaticBounds& bounds = static_metadata.bounds.front();
  requireVec3(bounds.local_min, -1.0f, -2.0f, -3.0f);
  requireVec3(bounds.local_max, 2.0f, 3.0f, 4.0f);
  requireVec3(bounds.world_min, 9.0f, 0.0f, 0.0f);
  requireVec3(bounds.world_max, 15.0f, 5.0f, 7.0f);
  requireVec3(bounds.world_center, 12.0f, 2.5f, 3.5f);
  KARMA_REQUIRE(bounds.world_radius > 5.2f);
  KARMA_REQUIRE(bounds.world_radius < 5.3f);

  const karma::world::Entity static_entity = instance.find("static_node");
  const karma::world::Entity dynamic_entity = instance.find("dynamic_node");
  KARMA_REQUIRE(world.isAlive(static_entity));
  KARMA_REQUIRE(world.isAlive(dynamic_entity));
  world.get<karma::components::TransformComponent>(static_entity)
      .setLocalPosition({2.0f, 2.0f, 3.0f});
  world.get<karma::components::TransformComponent>(dynamic_entity)
      .setLocalPosition({7.0f, 0.0f, 0.0f});

  karma::world::updateWorldTransforms(world, scene);
  requireVec3(static_metadata.transforms.front().world.position, 11.0f, 2.0f, 3.0f);
  requireVec3(world.get<karma::components::TransformComponent>(static_entity).worldPosition(),
              12.0f,
              2.0f,
              3.0f);
  requireVec3(world.get<karma::components::TransformComponent>(dynamic_entity).worldPosition(),
              17.0f,
              0.0f,
              0.0f);

  KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, instance));
}

void testInstantiateAndDestroyRuntimeScene() {
  const std::filesystem::path repo_root = findRepoRoot();
  KARMA_REQUIRE(!repo_root.empty());
  const std::filesystem::path world_path = repo_root / "examples/assets/world.glb";
  KARMA_REQUIRE(std::filesystem::exists(world_path));

  const std::filesystem::path dir = makeTempDir();
  writeText(dir / "prefabs/marker/prefab.json", simplePrefabJson());
  writeText(dir / "prefabs/marker/assets.package.json",
            R"({
  "version": 1,
  "assets": [
    {
      "type": "environment_map",
      "key": "tests/scene_runtime/prefab_env",
      "path": "prefab_env.hdr"
    }
  ]
})");
  writeText(dir / "assets.package.json",
            std::string(R"({
  "version": 1,
  "assets": [
    {
      "type": "gltf_scene",
      "key": "tests/scene_runtime/world",
      "path": ")") + world_path.generic_string() +
                R"(",
      "import_meshes": true,
      "import_lights": false
    }
  ]
})");

  karma::assets::AssetRegistry assets;
  karma::world::World world;
  karma::world::Scene scene;
  karma::scenes::SceneDocument document = runtimeDocument(dir);

  karma::scenes::SceneInstantiateResult result =
      karma::scenes::instantiateScene(world, scene, assets, document);
  if (!result.success) {
    for (const std::string& diagnostic : result.diagnostics) {
      std::cerr << diagnostic << '\n';
    }
  }
  KARMA_REQUIRE(result.success);
  KARMA_REQUIRE(result.diagnostics.empty());
  KARMA_REQUIRE(result.asset_packages.size() == 1);
  KARMA_REQUIRE(result.prefab_asset_packages.size() == 1);
  KARMA_REQUIRE(assets.findGltfSceneAsset("tests/scene_runtime/world") != nullptr);
  KARMA_REQUIRE(assets.findEnvironmentMap("tests/scene_runtime/prefab_env") != nullptr);

  const karma::world::Entity root = result.find("root");
  const karma::world::Entity gltf_root = result.find("tests/scene_runtime/world");
  const karma::world::Entity prefab_root = result.find("marker_prefab");
  const karma::world::Entity camera_entity = result.find("main_camera");
  const karma::world::Entity light_entity = result.find("sun");
  KARMA_REQUIRE(world.isAlive(root));
  KARMA_REQUIRE(world.isAlive(gltf_root));
  KARMA_REQUIRE(world.isAlive(prefab_root));
  KARMA_REQUIRE(world.isAlive(camera_entity));
  KARMA_REQUIRE(world.isAlive(light_entity));
  KARMA_REQUIRE(world.has<karma::components::CameraComponent>(camera_entity));
  KARMA_REQUIRE(world.has<karma::components::LightComponent>(light_entity));
  KARMA_REQUIRE(scene.get(scene.findNode(prefab_root)).parent == scene.findNode(root));

  std::vector<karma::world::Entity> created_entities = result.entities;
  KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, result));
  KARMA_REQUIRE(!result.success);
  KARMA_REQUIRE(result.entities.empty());
  KARMA_REQUIRE(result.prefab_asset_packages.empty());
  for (const karma::world::Entity entity : created_entities) {
    KARMA_REQUIRE(!world.isAlive(entity));
    KARMA_REQUIRE(scene.findNode(entity) == karma::world::Node::kInvalidId);
  }
  KARMA_REQUIRE(world.entities().empty());
  KARMA_REQUIRE(assets.findGltfSceneAsset("tests/scene_runtime/world") == nullptr);
  KARMA_REQUIRE(assets.findEnvironmentMap("tests/scene_runtime/prefab_env") == nullptr);
}

void testSceneAssetPackageLoadsFromBakedCacheAndReleases() {
  const std::filesystem::path dir = makeTempDir();
  writeText(dir / "env.hdr", "placeholder");
  writeText(dir / "assets.package.json",
            R"({
  "version": 1,
  "assets": [
    {
      "type": "environment_map",
      "key": "tests/scene_runtime/baked_env",
      "path": "env.hdr"
    }
  ]
})");

  karma::assets::AssetPackageBakeOptions bake_options{};
  bake_options.package_id = "scene_assets";
  bake_options.scene_fingerprint = "runtime-test";
  const std::filesystem::path baked_dir = dir / "bakes/asset_cache/scene_assets";
  std::string diagnostic;
  KARMA_REQUIRE(karma::assets::bakeAssetPackage(dir,
                                                baked_dir,
                                                bake_options,
                                                &diagnostic));
  KARMA_REQUIRE(diagnostic.empty());
  std::filesystem::remove(dir / "assets.package.json");

  karma::scenes::SceneDocument document{};
  document.name = "Baked Runtime Fixture";
  document.source_path = dir / "runtime.kscene.json";
  document.asset_packages.push_back(karma::scenes::SceneAssetRef{
      .id = "scene_assets",
      .path = "assets.package.json",
      .baked_cache_path = "bakes/asset_cache/scene_assets",
      .type = "asset_package",
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "root",
      .name = "Root",
  });

  karma::assets::AssetRegistry assets;
  karma::world::World world;
  karma::world::Scene scene;
  karma::scenes::SceneInstantiateResult result =
      karma::scenes::instantiateScene(world, scene, assets, document);
  KARMA_REQUIRE(result.success);
  KARMA_REQUIRE(result.asset_packages.size() == 1u);
  KARMA_REQUIRE(assets.findEnvironmentMap("tests/scene_runtime/baked_env") != nullptr);
  KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, result));
  KARMA_REQUIRE(assets.findEnvironmentMap("tests/scene_runtime/baked_env") == nullptr);
}

class StartupSceneGame final : public karma::app::GameInterface {
 public:
  void onStart() override {
    saw_scene_asset = assets != nullptr &&
                      assets->findSceneAsset("tests/startup/scene") != nullptr;
    if (world != nullptr) {
      world->forEach<karma::components::CameraComponent>(
          [&](karma::world::Entity entity) {
            const auto& camera = world->get<karma::components::CameraComponent>(entity);
            saw_primary_camera = saw_primary_camera || camera.is_primary;
          });
      world->forEach<karma::components::LightComponent>(
          [&](karma::world::Entity entity) {
            const auto& light = world->get<karma::components::LightComponent>(entity);
            saw_directional_light =
                saw_directional_light ||
                light.type == karma::components::LightComponent::Type::Directional;
          });
    }
  }

  void onFixedUpdate(float dt) override { (void)dt; }
  void onUpdate(float dt) override { (void)dt; }
  void onShutdown() override { shutdown_called = true; }

  bool saw_scene_asset = false;
  bool saw_primary_camera = false;
  bool saw_directional_light = false;
  bool shutdown_called = false;
};

void testEngineConfigStartupSceneAssetLoadsBeforeGameStart() {
  const std::filesystem::path dir = makeTempDir();
  writeText(dir / "startup.kscene.json", startupSceneJson());
  writeText(dir / "assets.package.json", R"({
  "version": 1,
  "assets": [
    {
      "type": "scene",
      "key": "tests/startup/scene",
      "path": "startup.kscene.json"
    }
  ]
})");

  karma::app::EngineConfig config{};
  config.loading_splash.enabled = false;
  config.startup_asset_packages.push_back(dir / "assets.package.json");
  config.startup_scene_assets.push_back("tests/startup/scene");

  StartupSceneGame game;
  {
    karma::app::EngineApp app;
    app.start(game, config);
    KARMA_REQUIRE(game.saw_scene_asset);
    KARMA_REQUIRE(game.saw_primary_camera);
    KARMA_REQUIRE(game.saw_directional_light);
  }
  KARMA_REQUIRE(game.shutdown_called);
}

}  // namespace

int main() {
  testStaticMetadataBuildCapturesTransformsAndBoundsWithoutFreezingRuntime();
  testInstantiateAndDestroyRuntimeScene();
  testSceneAssetPackageLoadsFromBakedCacheAndReleases();
  testEngineConfigStartupSceneAssetLoadsBeforeGameStart();
  return 0;
}
