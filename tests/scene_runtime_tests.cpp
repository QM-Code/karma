#include "karma/scenes.h"
#include "karma/app.h"
#include "karma/foliage.h"
#include "karma/prefabs.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using Json = nlohmann::json;

static_assert(!std::is_move_constructible_v<karma::assets::AssetRegistry>);
static_assert(!std::is_move_assignable_v<karma::assets::AssetRegistry>);

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

std::string namedPrefabJson(const std::string& name) {
  return R"({
  "version": 2,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": ")" + name + R"(",
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

std::string hierarchicalPrefabJson() {
  return R"({
  "version": 2,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "Static Prefab Root",
      "parent": null,
      "components": {
        "TransformComponent": {
          "position": [0, 0, 0],
          "rotation": [0, 0, 0, 1],
          "scale": [1, 1, 1]
        }
      }
    },
    {
      "id": 1,
      "name": "Static Prefab Child",
      "parent": 0,
      "components": {
        "TransformComponent": {
          "position": [0, 1, 0],
          "rotation": [0, 0, 0, 1],
          "scale": [1, 1, 1]
        }
      }
    },
    {
      "id": 2,
      "name": "Static Prefab Grandchild",
      "parent": 1,
      "components": {
        "TransformComponent": {
          "position": [0, 1, 0],
          "rotation": [0, 0, 0, 1],
          "scale": [1, 1, 1]
        }
      }
    }
  ]
})";
}

template <typename Component>
nlohmann::json serializeComponent(Component component, std::string_view type_name) {
  karma::prefabs::ensureBuiltinComponentSerializers();
  const auto* serializer =
      karma::prefabs::componentSerializerRegistry().find(type_name);
  KARMA_REQUIRE(serializer != nullptr);
  karma::world::World world;
  const karma::world::Entity entity = world.createEntity();
  world.add(entity, std::move(component));
  return serializer->serialize(world, entity);
}

std::string startupSceneJson() {
  return R"({
  "version": 1,
  "name": "Startup Scene",
  "asset_packages": [
    {
      "id": "startup_assets",
      "path": "assets.package.json",
      "baked_cache": "bakes/asset_cache/startup_assets",
      "type": "asset_package"
    }
  ],
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

karma::world::MeshData runtimeLightmapMesh(std::string material_key = {}) {
  karma::world::MeshData mesh{};
  mesh.vertices = {
      {-1.0f, 0.0f, -1.0f},
      {1.0f, 0.0f, -1.0f},
      {0.0f, 0.0f, 1.0f},
  };
  mesh.normals.assign(mesh.vertices.size(), glm::vec3{0.0f, 1.0f, 0.0f});
  mesh.uvs = {
      {0.0f, 0.0f},
      {1.0f, 0.0f},
      {0.5f, 1.0f},
  };
  mesh.indices = {0u, 1u, 2u};
  mesh.submeshes.push_back(karma::world::MeshSubmesh{
      .index_offset = 0u,
      .index_count = 3u,
      .material_slot = 0u,
  });
  if (!material_key.empty()) {
    mesh.material_slots.push_back(karma::world::MeshMaterialSlot{
        .name = "surface",
        .default_material_key = std::move(material_key),
    });
  }
  return mesh;
}

karma::scenes::SceneDocument runtimeLightmapDocument(
    const std::filesystem::path& dir) {
  karma::scenes::SceneDocument document{};
  document.name = "Runtime V2 Lightmaps";
  document.source_path = dir / "runtime.kscene.json";
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "material_target",
      .name = "Material Target",
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "default_target",
      .name = "Default Material Target",
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "mixed_light_entity",
      .name = "Mixed Light",
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "baked_light_entity",
      .name = "Baked Light",
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "realtime_light_entity",
      .name = "Realtime Light",
  });
  document.static_components.push_back(karma::scenes::SceneStaticComponent{
      .id = "material_static",
      .entity_id = "material_target",
      .mesh_asset_key = "tests/runtime_lightmaps/source_material",
      .material_asset_key = "tests/runtime_lightmaps/base_material",
      .render = true,
      .lighting = true,
      .casts_shadows = true,
  });
  document.static_components.push_back(karma::scenes::SceneStaticComponent{
      .id = "default_static",
      .entity_id = "default_target",
      .mesh_asset_key = "tests/runtime_lightmaps/source_default",
      .render = true,
      .lighting = true,
      .casts_shadows = true,
  });

  karma::components::LightComponent mixed{};
  mixed.bake_mode = karma::components::LightComponent::BakeMode::Mixed;
  mixed.intensity = 2.0f;
  document.lights.push_back(karma::scenes::SceneLight{
      .id = "mixed",
      .entity_id = "mixed_light_entity",
      .component = mixed,
  });
  karma::components::LightComponent baked{};
  baked.bake_mode = karma::components::LightComponent::BakeMode::Baked;
  baked.intensity = 4.0f;
  document.lights.push_back(karma::scenes::SceneLight{
      .id = "baked",
      .entity_id = "baked_light_entity",
      .component = baked,
  });
  karma::components::LightComponent realtime{};
  realtime.bake_mode = karma::components::LightComponent::BakeMode::Realtime;
  realtime.intensity = 3.0f;
  document.lights.push_back(karma::scenes::SceneLight{
      .id = "realtime",
      .entity_id = "realtime_light_entity",
      .component = realtime,
  });
  document.bakes.push_back(karma::scenes::SceneBakeDesc{
      .id = "runtime_lightmaps",
      .path = "bakes/runtime_lightmaps.kbake.json",
  });
  return document;
}

void testSceneReferenceRootPrecedence() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path scene_root = dir / "scene";
  const std::filesystem::path document_root = dir / "document-root";
  const std::filesystem::path override_root = dir / "override-root";
  const std::filesystem::path prefab_path = "prefabs/item/prefab.json";
  writeText(scene_root / prefab_path, namedPrefabJson("Scene Directory"));
  writeText(document_root / prefab_path, namedPrefabJson("Document Root"));
  writeText(override_root / prefab_path, namedPrefabJson("Instance Override"));

  karma::scenes::SceneDocument document{};
  document.source_path = scene_root / "level.kscene.json";
  document.reference_root = document_root;
  document.prefab_instances.push_back(karma::scenes::ScenePrefabInstance{
      .id = "item",
      .prefab_path = prefab_path,
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "camera_entity",
  });
  karma::components::TerrainComponent terrain{};
  terrain.source = karma::components::TerrainSourceType::SingleImage;
  terrain.height_image = "terrain/height.r32";
  terrain.control_image = "terrain/control.tga";
  terrain.material_layers.push_back(karma::components::TerrainMaterialLayer{
      .albedo_image = "terrain/grass.png",
  });
  terrain.data_maps.push_back(karma::components::TerrainDataMapBinding{
      .name = "flow",
      .image = "terrain/flow.png",
  });
  karma::components::FoliageComponent foliage{};
  foliage.sidecar_path = "foliage/trees.kfoliage";
  foliage.mesh_asset_key = "trees/mesh";
  document.entities.back().components = nlohmann::json{
      {"TerrainComponent", serializeComponent(std::move(terrain), "TerrainComponent")},
      {"FoliageComponent", serializeComponent(std::move(foliage), "FoliageComponent")},
  };
  karma::components::CameraComponent camera{};
  camera.shader_override_vertex_path = "shaders/scene.vert";
  camera.shader_override_fragment_path = "shaders/scene.frag";
  document.cameras.push_back(karma::scenes::SceneCamera{
      .id = "camera",
      .entity_id = "camera_entity",
      .component = camera,
  });

  auto require_root = [&](const karma::scenes::SceneDocument& input_document,
                          const karma::scenes::SceneInstantiateDesc& desc,
                          const std::filesystem::path& expected_root,
                          const std::string& expected_name) {
    karma::assets::AssetRegistry assets;
    karma::world::World world;
    karma::world::Scene scene;
    karma::scenes::SceneInstantiateResult result =
        karma::scenes::instantiateScene(world, scene, assets, input_document, desc);
    KARMA_REQUIRE(result.success);
    const karma::world::Entity prefab = result.prefab_roots_by_id.at("item");
    KARMA_REQUIRE(world.get<karma::components::TagComponent>(prefab).name ==
                  expected_name);
    const karma::world::Entity camera_entity = result.cameras_by_id.at("camera");
    const auto& runtime_camera =
        world.get<karma::components::CameraComponent>(camera_entity);
    KARMA_REQUIRE(runtime_camera.shader_override_vertex_path ==
                  (expected_root / "shaders/scene.vert").lexically_normal());
    KARMA_REQUIRE(runtime_camera.shader_override_fragment_path ==
                  (expected_root / "shaders/scene.frag").lexically_normal());
    const auto& runtime_terrain =
        world.get<karma::components::TerrainComponent>(camera_entity);
    KARMA_REQUIRE(runtime_terrain.height_image ==
                  (expected_root / "terrain/height.r32").lexically_normal());
    KARMA_REQUIRE(runtime_terrain.control_image ==
                  (expected_root / "terrain/control.tga").lexically_normal());
    KARMA_REQUIRE(runtime_terrain.material_layers[0].albedo_image ==
                  (expected_root / "terrain/grass.png").lexically_normal());
    KARMA_REQUIRE(runtime_terrain.data_maps[0].image ==
                  (expected_root / "terrain/flow.png").lexically_normal());
    const auto& runtime_foliage =
        world.get<karma::components::FoliageComponent>(camera_entity);
    KARMA_REQUIRE(runtime_foliage.sidecar_path ==
                  (expected_root / "foliage/trees.kfoliage").lexically_normal());
    KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, result));
  };

  karma::scenes::SceneInstantiateDesc override_desc{};
  override_desc.reference_root = override_root;
  require_root(document, override_desc, override_root, "Instance Override");

  require_root(document, {}, document_root, "Document Root");

  document.reference_root.clear();
  require_root(document, {}, scene_root, "Scene Directory");
}

void testSceneInstanceRejectsWrongWorldTeardown() {
  static_assert(!std::is_copy_assignable_v<karma::world::Scene>);
  static_assert(!std::is_move_assignable_v<karma::world::Scene>);
  static_assert(!std::is_copy_constructible_v<
                karma::scenes::SceneInstantiateResult>);
  static_assert(!std::is_copy_assignable_v<
                karma::scenes::SceneInstantiateResult>);

  karma::assets::AssetRegistry assets;
  karma::world::World owning_world;
  karma::world::Scene owning_scene;
  karma::scenes::SceneDocument document{};
  document.name = "World ownership";
  document.entities.push_back(
      karma::scenes::SceneEntity{.id = "root", .name = "Owned root"});

  karma::scenes::SceneInstantiateResult instance =
      karma::scenes::instantiateScene(
          owning_world, owning_scene, assets, document);
  KARMA_REQUIRE(instance.success);
  KARMA_REQUIRE(instance.world_instance_id == owning_world.instanceId());
  KARMA_REQUIRE(instance.scene_instance_id == owning_scene.instanceId());
  const karma::world::Entity owned = instance.find("root");
  KARMA_REQUIRE(owning_world.isAlive(owned));

  karma::world::World other_world;
  karma::world::Scene other_scene;
  const karma::world::Entity unrelated = other_world.createEntity();
  other_scene.createNode(unrelated);
  KARMA_REQUIRE(unrelated == owned);
  KARMA_REQUIRE(!karma::scenes::destroyScene(
      other_world, other_scene, instance));
  KARMA_REQUIRE(other_world.isAlive(unrelated));
  KARMA_REQUIRE(owning_world.isAlive(owned));
  KARMA_REQUIRE(instance.success);
  KARMA_REQUIRE(instance.world_instance_id == owning_world.instanceId());

  karma::world::Scene unrelated_scene;
  KARMA_REQUIRE(!karma::scenes::destroyScene(
      owning_world, unrelated_scene, instance));
  KARMA_REQUIRE(owning_world.isAlive(owned));
  KARMA_REQUIRE(instance.scene_instance_id == owning_scene.instanceId());

  karma::scenes::SceneInstantiateResult moved_instance(std::move(instance));
  KARMA_REQUIRE(instance.world_instance_id == 0u);
  KARMA_REQUIRE(instance.scene_instance_id == 0u);
  KARMA_REQUIRE(instance.entities.empty());
  bool rejected_live_replacement = false;
  try {
    moved_instance = karma::scenes::SceneInstantiateResult{};
  } catch (const std::logic_error&) {
    rejected_live_replacement = true;
  }
  KARMA_REQUIRE(rejected_live_replacement);

  const uint64_t owning_scene_id = owning_scene.instanceId();
  karma::world::Scene moved_scene(std::move(owning_scene));
  KARMA_REQUIRE(moved_scene.instanceId() == owning_scene_id);
  KARMA_REQUIRE(owning_scene.instanceId() != owning_scene_id);
  KARMA_REQUIRE(karma::scenes::destroyScene(
      owning_world, moved_scene, moved_instance));
  KARMA_REQUIRE(!owning_world.isAlive(owned));
  KARMA_REQUIRE(moved_instance.world_instance_id == 0u);
  KARMA_REQUIRE(moved_instance.scene_instance_id == 0u);
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

void testStaticBoundsRejectFiniteInputOverflow() {
  karma::world::MeshData mesh{};
  mesh.vertices = {{2.0f, 0.0f, 0.0f}};
  karma::assets::AssetRegistry assets;
  KARMA_REQUIRE(assets.registerMeshAsset("tests/static_bounds/overflow", std::move(mesh)));

  karma::scenes::SceneDocument document{};
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "overflow",
      .transform = karma::scenes::SceneTransform{
          .scale = {std::numeric_limits<float>::max(), 1.0f, 1.0f},
      },
  });
  document.static_components.push_back(karma::scenes::SceneStaticComponent{
      .id = "overflow_static",
      .entity_id = "overflow",
      .mesh_asset_key = "tests/static_bounds/overflow",
  });

  karma::world::World world;
  karma::world::Scene scene;
  karma::scenes::SceneInstantiateDesc instantiate_desc{};
  instantiate_desc.instantiate_gltf_scenes = false;
  instantiate_desc.instantiate_prefabs = false;
  karma::scenes::SceneInstantiateResult instance =
      karma::scenes::instantiateScene(world, scene, assets, document, instantiate_desc);
  KARMA_REQUIRE(instance.success);

  const karma::scenes::SceneStaticBuildResult metadata =
      karma::scenes::buildSceneStaticMetadata(document, instance, world, scene, assets);
  KARMA_REQUIRE(!metadata.success);
  KARMA_REQUIRE(metadata.bounds.empty());
  KARMA_REQUIRE(!metadata.diagnostics.empty());
  KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, instance));
}

void testInMemorySceneValidationPreventsPartialInstantiation() {
  karma::scenes::SceneDocument document{};
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "a",
      .parent_id = "b",
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "b",
      .parent_id = "a",
  });

  const karma::scenes::SceneValidationResult validation =
      karma::scenes::validateSceneDocument(document);
  KARMA_REQUIRE(!validation.success());
  KARMA_REQUIRE(!validation.diagnostics.empty());

  karma::assets::AssetRegistry assets;
  karma::world::World world;
  karma::world::Scene scene;
  const karma::scenes::SceneInstantiateResult instance =
      karma::scenes::instantiateScene(world, scene, assets, document);
  KARMA_REQUIRE(!instance.success);
  KARMA_REQUIRE(!instance.diagnostics.empty());
  KARMA_REQUIRE(world.entities().empty());
  KARMA_REQUIRE(scene.nodes().empty());

  karma::scenes::SceneDocument invalid_values{};
  invalid_values.entities.push_back(karma::scenes::SceneEntity{
      .id = "root",
      .transform = karma::scenes::SceneTransform{
          .position = {std::numeric_limits<float>::infinity(), 0.0f, 0.0f},
      },
  });
  invalid_values.cameras.push_back(karma::scenes::SceneCamera{
      .id = "root",
      .entity_id = "root",
  });
  const karma::scenes::SceneValidationResult invalid_validation =
      karma::scenes::validateSceneDocument(invalid_values);
  KARMA_REQUIRE(!invalid_validation.success());
  KARMA_REQUIRE(invalid_validation.diagnostics.size() >= 2u);

  karma::scenes::SceneDocument invalid_ranges{};
  invalid_ranges.entities.push_back(karma::scenes::SceneEntity{.id = "camera_entity"});
  karma::components::CameraComponent camera{};
  camera.near_clip = 10.0f;
  camera.far_clip = 1.0f;
  invalid_ranges.cameras.push_back(karma::scenes::SceneCamera{
      .id = "camera",
      .entity_id = "camera_entity",
      .component = camera,
  });
  karma::components::LightComponent light{};
  light.type = karma::components::LightComponent::Type::Spot;
  light.inner_cone_degrees = 70.0f;
  light.outer_cone_degrees = 20.0f;
  invalid_ranges.lights.push_back(karma::scenes::SceneLight{
      .id = "light",
      .entity_id = "camera_entity",
      .component = light,
  });
  KARMA_REQUIRE(!karma::scenes::validateSceneDocument(invalid_ranges).success());

  karma::scenes::SceneDocument invalid_components{};
  invalid_components.entities.push_back(karma::scenes::SceneEntity{
      .id = "component_entity",
      .components = nlohmann::json{
          {"UnknownGameplayComponent", nlohmann::json::object()}},
  });
  const karma::scenes::SceneValidationResult component_validation =
      karma::scenes::validateSceneDocument(invalid_components);
  KARMA_REQUIRE(!component_validation.success());
  KARMA_REQUIRE(std::any_of(
      component_validation.diagnostics.begin(),
      component_validation.diagnostics.end(),
      [](const std::string& diagnostic) {
        return diagnostic.find("unknown component") != std::string::npos;
      }));
  const karma::scenes::SceneInstantiateResult component_instance =
      karma::scenes::instantiateScene(
          world, scene, assets, invalid_components);
  KARMA_REQUIRE(!component_instance.success);
  KARMA_REQUIRE(world.entities().empty());

  karma::scenes::SceneDocument escaped_prefab{};
  escaped_prefab.prefab_instances.push_back(
      karma::scenes::ScenePrefabInstance{
          .id = "escaped_prefab",
          .prefab_path = "../../outside/prefab.json",
      });
  KARMA_REQUIRE(!karma::scenes::validateSceneDocument(escaped_prefab).success());

  karma::components::TerrainComponent escaped_terrain{};
  escaped_terrain.source =
      karma::components::TerrainSourceType::SingleImage;
  escaped_terrain.height_image = "../../outside/height.r32";
  escaped_terrain.height_format =
      karma::components::TerrainHeightFormat::R32Float;
  escaped_terrain.raw_width = 5u;
  escaped_terrain.raw_height = 5u;
  escaped_terrain.tile_resolution = 5u;
  escaped_terrain.terrain_size = 4.0f;
  karma::scenes::SceneDocument escaped_component{};
  escaped_component.entities.push_back(karma::scenes::SceneEntity{
      .id = "escaped_terrain",
      .components = nlohmann::json{{
          "TerrainComponent",
          serializeComponent(std::move(escaped_terrain), "TerrainComponent")}},
  });
  KARMA_REQUIRE(
      !karma::scenes::validateSceneDocument(escaped_component).success());
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
  document.gltf_scenes.push_back(karma::scenes::SceneAssetRef{
      .id = "tests/static_bounds/gltf",
      .path = "unused.glb",
      .type = "gltf_scene",
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
  world.destroyEntity(prefab_root);
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

void testPhysicsAuthoringComponentsSceneRoundTrip() {
  karma::prefabs::ensureBuiltinComponentSerializers();
  karma::world::World authored_world;
  const karma::world::Entity authored = authored_world.createEntity();
  authored_world.add(authored, karma::components::TransformComponent{});
  authored_world.add(
      authored,
      karma::components::ColliderComponent::box(
          karma::components::BoxColliderShape{
              .center = {0.0f, 0.75f, 0.0f},
              .half_extents = {0.5f, 0.75f, 0.5f},
          }));
  karma::components::RigidbodyComponent body{};
  body.motion_type = karma::components::RigidbodyMotionType::Static;
  body.motion_quality = karma::components::RigidbodyMotionQuality::LinearCast;
  body.allowed_dofs = karma::components::RigidbodyDofPlane2D;
  body.mass = 12.0f;
  body.velocity = {1.0f, 2.0f, 3.0f};
  body.angular_velocity = {4.0f, 5.0f, 6.0f};
  body.is_kinematic = false;
  body.use_gravity = true;
  body.is_trigger = true;
  body.gravity_factor = 0.45f;
  body.linear_damping = 0.12f;
  body.angular_damping = 0.34f;
  body.max_linear_velocity = 111.0f;
  body.max_angular_velocity = 22.0f;
  body.inertia_multiplier = 1.6f;
  body.velocity_solver_steps = 7u;
  body.position_solver_steps = 5u;
  body.allow_sleeping = false;
  body.allow_dynamic_or_kinematic = true;
  body.collide_kinematic_vs_non_dynamic = true;
  body.use_manifold_reduction = false;
  body.apply_gyroscopic_force = true;
  body.enhanced_internal_edge_removal = true;
  authored_world.add(authored, body);
  authored_world.add(
      authored,
      karma::components::PhysicsMaterialComponent{
          .friction = 0.8f,
          .restitution = 0.3f,
      });
  authored_world.add(
      authored,
      karma::components::PhysicsCollisionFilterComponent{
          .layers = 0x24u,
          .collides_with = 0x00FF00FFu,
      });

  auto serialized = [&](std::string_view type_name) {
    const auto* serializer =
        karma::prefabs::componentSerializerRegistry().find(type_name);
    KARMA_REQUIRE(serializer != nullptr);
    return serializer->serialize(authored_world, authored);
  };

  karma::scenes::SceneDocument document{};
  document.name = "Physics Scene Round Trip";
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "complete_body",
      .name = "Complete Body",
      .components = {
          {"ColliderComponent", serialized("ColliderComponent")},
          {"RigidbodyComponent", serialized("RigidbodyComponent")},
          {"PhysicsMaterialComponent", serialized("PhysicsMaterialComponent")},
          {"PhysicsCollisionFilterComponent",
           serialized("PhysicsCollisionFilterComponent")},
      },
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "legacy_body",
      .name = "Legacy Body",
      .components = {
          {"ColliderComponent", serialized("ColliderComponent")},
          {"RigidbodyComponent",
           {
               {"mass", 2.5f},
               {"velocity", nlohmann::json::array({0.0f, 1.0f, 0.0f})},
               {"angular_velocity",
                nlohmann::json::array({0.0f, 0.0f, 1.0f})},
               {"is_kinematic", false},
               {"use_gravity", true},
               {"is_trigger", false},
           }},
      },
  });
  KARMA_REQUIRE(karma::scenes::validateSceneDocument(document).success());

  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path path = dir / "physics.kscene.json";
  KARMA_REQUIRE(karma::scenes::saveSceneDocument(document, path).success());
  const karma::scenes::SceneLoadResult loaded_document =
      karma::scenes::loadSceneDocument(path);
  KARMA_REQUIRE(loaded_document.success());

  karma::assets::AssetRegistry assets;
  karma::world::World world;
  karma::world::Scene scene;
  karma::scenes::SceneInstantiateResult instance =
      karma::scenes::instantiateScene(
          world, scene, assets, *loaded_document.document);
  KARMA_REQUIRE(instance.success);

  const karma::world::Entity complete = instance.find("complete_body");
  KARMA_REQUIRE(world.isAlive(complete));
  const auto& loaded =
      world.get<karma::components::RigidbodyComponent>(complete);
  KARMA_REQUIRE(loaded.motion_type == body.motion_type);
  KARMA_REQUIRE(loaded.motion_quality == body.motion_quality);
  KARMA_REQUIRE(loaded.allowed_dofs == body.allowed_dofs);
  KARMA_REQUIRE(nearlyEqual(loaded.mass, body.mass));
  requireVec3(loaded.velocity, 1.0f, 2.0f, 3.0f);
  requireVec3(loaded.angular_velocity, 4.0f, 5.0f, 6.0f);
  KARMA_REQUIRE(nearlyEqual(loaded.gravity_factor, body.gravity_factor));
  KARMA_REQUIRE(nearlyEqual(loaded.linear_damping, body.linear_damping));
  KARMA_REQUIRE(nearlyEqual(loaded.angular_damping, body.angular_damping));
  KARMA_REQUIRE(nearlyEqual(loaded.max_linear_velocity,
                            body.max_linear_velocity));
  KARMA_REQUIRE(nearlyEqual(loaded.max_angular_velocity,
                            body.max_angular_velocity));
  KARMA_REQUIRE(nearlyEqual(loaded.inertia_multiplier,
                            body.inertia_multiplier));
  KARMA_REQUIRE(loaded.velocity_solver_steps == body.velocity_solver_steps);
  KARMA_REQUIRE(loaded.position_solver_steps == body.position_solver_steps);
  KARMA_REQUIRE(loaded.allow_sleeping == body.allow_sleeping);
  KARMA_REQUIRE(loaded.allow_dynamic_or_kinematic ==
                body.allow_dynamic_or_kinematic);
  KARMA_REQUIRE(loaded.collide_kinematic_vs_non_dynamic ==
                body.collide_kinematic_vs_non_dynamic);
  KARMA_REQUIRE(loaded.use_manifold_reduction == body.use_manifold_reduction);
  KARMA_REQUIRE(loaded.apply_gyroscopic_force == body.apply_gyroscopic_force);
  KARMA_REQUIRE(loaded.enhanced_internal_edge_removal ==
                body.enhanced_internal_edge_removal);
  const auto& material =
      world.get<karma::components::PhysicsMaterialComponent>(complete);
  KARMA_REQUIRE(nearlyEqual(material.friction, 0.8f));
  KARMA_REQUIRE(nearlyEqual(material.restitution, 0.3f));
  const auto& filter =
      world.get<karma::components::PhysicsCollisionFilterComponent>(complete);
  KARMA_REQUIRE(filter.layers == 0x24u);
  KARMA_REQUIRE(filter.collides_with == 0x00FF00FFu);

  const karma::world::Entity legacy = instance.find("legacy_body");
  KARMA_REQUIRE(world.isAlive(legacy));
  const auto& legacy_body =
      world.get<karma::components::RigidbodyComponent>(legacy);
  const karma::components::RigidbodyComponent defaults{};
  KARMA_REQUIRE(nearlyEqual(legacy_body.mass, 2.5f));
  KARMA_REQUIRE(legacy_body.motion_type == defaults.motion_type);
  KARMA_REQUIRE(legacy_body.motion_quality == defaults.motion_quality);
  KARMA_REQUIRE(legacy_body.allowed_dofs == defaults.allowed_dofs);
  KARMA_REQUIRE(nearlyEqual(legacy_body.linear_damping,
                            defaults.linear_damping));
  KARMA_REQUIRE(nearlyEqual(legacy_body.max_linear_velocity,
                            defaults.max_linear_velocity));
  KARMA_REQUIRE(legacy_body.allow_sleeping == defaults.allow_sleeping);
  KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, instance));
  std::filesystem::remove_all(dir);
}

void testSceneContextualEntityReferencesResolveInTwoPasses() {
  karma::scenes::SceneDocument document{};
  document.name = "Contextual Scene References";
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "constraint",
      .name = "Constraint",
      .components = Json{{
          "PhysicsConstraintComponent",
          Json{{"body_a", Json{{"scope", "scene"}, {"id", "body_a"}}},
               {"body_b", Json{{"scope", "scene"}, {"id", "body_b"}}},
               {"kind", "hinge"},
               {"max_friction_torque", 3.5f}}}},
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "body_a",
      .name = "Body A",
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "body_b",
      .name = "Body B",
  });
  document.static_components.push_back(karma::scenes::SceneStaticComponent{
      .id = "body_a_static",
      .entity_id = "body_a",
      .transform = true,
      .render = true,
      .lighting = true,
      .collision = true,
      .navigation = false,
      .casts_shadows = true,
  });

  KARMA_REQUIRE(karma::scenes::validateSceneDocument(document).success());
  karma::scenes::SceneDocument unresolved = document;
  unresolved.entities[0].components["PhysicsConstraintComponent"]["body_b"] =
      Json{{"scope", "scene"}, {"id", "missing"}};
  KARMA_REQUIRE(!karma::scenes::validateSceneDocument(unresolved).success());

  karma::assets::AssetRegistry assets;
  karma::world::World world;
  karma::world::Scene scene;
  karma::scenes::SceneInstantiateResult instance =
      karma::scenes::instantiateScene(world, scene, assets, document);
  KARMA_REQUIRE(instance.success);
  const karma::world::Entity constraint = instance.find("constraint");
  const karma::world::Entity body_a = instance.find("body_a");
  const karma::world::Entity body_b = instance.find("body_b");
  const auto& component =
      world.get<karma::components::PhysicsConstraintComponent>(constraint);
  KARMA_REQUIRE(component.body_a == body_a);
  KARMA_REQUIRE(component.body_b == body_b);
  KARMA_REQUIRE(component.kind ==
                karma::components::PhysicsConstraintKind::Hinge);
  KARMA_REQUIRE(nearlyEqual(component.max_friction_torque, 3.5f));
  const auto& static_component =
      world.get<karma::components::StaticComponent>(body_a);
  KARMA_REQUIRE(static_component.enabled);
  KARMA_REQUIRE(!static_component.include_descendants);
  KARMA_REQUIRE((static_component.flags &
                 karma::components::StaticComponentRender) != 0u);
  KARMA_REQUIRE((static_component.flags &
                 karma::components::StaticComponentLighting) != 0u);
  KARMA_REQUIRE((static_component.flags &
                 karma::components::StaticComponentShadows) != 0u);
  KARMA_REQUIRE((static_component.flags &
                 karma::components::StaticComponentCollision) != 0u);
  KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, instance));
}

void testAuthoredRenderDependenciesResolveOrRollback() {
  const auto live_scene_node_count = [](const karma::world::Scene& scene) {
    return static_cast<size_t>(std::count_if(
        scene.nodes().begin(),
        scene.nodes().end(),
        [](const karma::world::Node& node) {
          return node.id != karma::world::Node::kInvalidId;
        }));
  };
  const Json instance_set_payload = serializeComponent(
      karma::components::InstanceSetComponent{}, "InstanceSetComponent");
  const Json lod_payload = serializeComponent(
      karma::components::LodComponent{
          .levels = {karma::components::LodLevel{
              .start_distance = 40.0f,
              .mesh_asset_key = "trees/far",
          }},
      },
      "LODComponent");

  const auto instanced_payload = [](std::string_view source_id) {
    Json payload = serializeComponent(
        karma::components::InstancedMeshComponent{
            .mesh_asset_key = "grass/cluster",
        },
        "InstancedMeshComponent");
    payload["instance_source"] =
        Json{{"scope", "scene"}, {"id", source_id}};
    return payload;
  };

  const auto expect_dependency_failure =
      [&](const karma::scenes::SceneDocument& document,
          std::string_view diagnostic_text) {
    KARMA_REQUIRE(karma::scenes::validateSceneDocument(document).success());

    karma::assets::AssetRegistry assets;
    karma::world::World world;
    karma::world::Scene scene;
    const karma::world::Entity sentinel = world.createEntity();
    const karma::world::NodeId sentinel_node = scene.createNode(sentinel);
    const size_t world_size = world.entities().size();
    const size_t scene_size = live_scene_node_count(scene);

    const karma::scenes::SceneInstantiateResult result =
        karma::scenes::instantiateScene(world, scene, assets, document);
    KARMA_REQUIRE(!result.success);
    KARMA_REQUIRE(std::any_of(
        result.diagnostics.begin(),
        result.diagnostics.end(),
        [&](const std::string& diagnostic) {
          return diagnostic.find(diagnostic_text) != std::string::npos;
        }));
    KARMA_REQUIRE(result.entities.empty());
    KARMA_REQUIRE(result.entities_by_id.empty());
    KARMA_REQUIRE(world.entities().size() == world_size);
    KARMA_REQUIRE(world.isAlive(sentinel));
    KARMA_REQUIRE(live_scene_node_count(scene) == scene_size);
    KARMA_REQUIRE(scene.findNode(sentinel) == sentinel_node);
  };

  karma::scenes::SceneDocument unresolved_source{};
  unresolved_source.name = "Unresolved instance source";
  unresolved_source.entities.push_back(karma::scenes::SceneEntity{
      .id = "batch",
      .components = Json{{"InstancedMeshComponent",
                          instanced_payload("source")}},
  });
  unresolved_source.entities.push_back(karma::scenes::SceneEntity{
      .id = "source",
  });
  expect_dependency_failure(
      unresolved_source,
      "InstancedMeshComponent has no resolvable InstanceSetComponent source");

  karma::scenes::SceneDocument orphan_lod{};
  orphan_lod.name = "Orphan LOD";
  orphan_lod.entities.push_back(karma::scenes::SceneEntity{
      .id = "valid_instance_set",
      .components = Json{{"InstanceSetComponent", instance_set_payload}},
  });
  orphan_lod.entities.push_back(karma::scenes::SceneEntity{
      .id = "orphan_lod",
      .components = Json{{"LODComponent", lod_payload}},
  });
  expect_dependency_failure(
      orphan_lod,
      "LODComponent has no sibling mesh, instanced mesh, or direct-mesh "
      "foliage render source");

  karma::components::FoliageComponent prefab_foliage{};
  prefab_foliage.sidecar_path = "foliage/prefab.kfoliage";
  prefab_foliage.prefab_path = "trees/tree.prefab.json";
  karma::scenes::SceneDocument prefab_foliage_lod{};
  prefab_foliage_lod.name = "Prefab foliage orphan LOD";
  prefab_foliage_lod.entities.push_back(karma::scenes::SceneEntity{
      .id = "prefab_foliage",
      .components = Json{
          {"FoliageComponent",
           serializeComponent(std::move(prefab_foliage),
                              "FoliageComponent")},
          {"LODComponent", lod_payload},
      },
  });
  expect_dependency_failure(
      prefab_foliage_lod,
      "LODComponent has no sibling mesh, instanced mesh, or direct-mesh "
      "foliage render source");

  karma::scenes::SceneDocument cross_entity_source{};
  cross_entity_source.name = "Cross-entity instance source";
  cross_entity_source.entities.push_back(karma::scenes::SceneEntity{
      .id = "batch",
      .components = Json{{"InstancedMeshComponent",
                          instanced_payload("source")}},
  });
  cross_entity_source.entities.push_back(karma::scenes::SceneEntity{
      .id = "source",
      .components = Json{{"InstanceSetComponent", instance_set_payload}},
  });
  KARMA_REQUIRE(
      karma::scenes::validateSceneDocument(cross_entity_source).success());
  {
    karma::assets::AssetRegistry assets;
    karma::world::World world;
    karma::world::Scene scene;
    const karma::world::Entity sentinel = world.createEntity();
    const karma::world::NodeId sentinel_node = scene.createNode(sentinel);
    const size_t world_size = world.entities().size();
    const size_t scene_size = live_scene_node_count(scene);
    karma::scenes::SceneInstantiateResult result =
        karma::scenes::instantiateScene(
            world, scene, assets, cross_entity_source);
    KARMA_REQUIRE(result.success);
    const karma::world::Entity batch = result.find("batch");
    const karma::world::Entity source = result.find("source");
    KARMA_REQUIRE(world.isAlive(batch));
    KARMA_REQUIRE(world.isAlive(source));
    KARMA_REQUIRE(world.get<karma::components::InstancedMeshComponent>(batch)
                      .instance_source == source);
    KARMA_REQUIRE(world.has<karma::components::InstanceSetComponent>(source));
    KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, result));
    KARMA_REQUIRE(world.entities().size() == world_size);
    KARMA_REQUIRE(world.isAlive(sentinel));
    KARMA_REQUIRE(live_scene_node_count(scene) == scene_size);
    KARMA_REQUIRE(scene.findNode(sentinel) == sentinel_node);
  }

  karma::components::FoliageComponent direct_foliage{};
  direct_foliage.sidecar_path = "foliage/direct.kfoliage";
  direct_foliage.mesh_asset_key = "trees/direct";
  karma::scenes::SceneDocument direct_foliage_lod{};
  direct_foliage_lod.name = "Direct foliage LOD";
  direct_foliage_lod.entities.push_back(karma::scenes::SceneEntity{
      .id = "direct_foliage",
      .components = Json{
          {"FoliageComponent",
           serializeComponent(std::move(direct_foliage),
                              "FoliageComponent")},
          {"LODComponent", lod_payload},
      },
  });
  KARMA_REQUIRE(
      karma::scenes::validateSceneDocument(direct_foliage_lod).success());
  {
    karma::assets::AssetRegistry assets;
    karma::world::World world;
    karma::world::Scene scene;
    karma::scenes::SceneInstantiateResult result =
        karma::scenes::instantiateScene(
            world, scene, assets, direct_foliage_lod);
    KARMA_REQUIRE(result.success);
    KARMA_REQUIRE(world.has<karma::components::FoliageComponent>(
        result.find("direct_foliage")));
    KARMA_REQUIRE(world.has<karma::components::LodComponent>(
        result.find("direct_foliage")));
    KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, result));
  }
}

void testPrefabInstanceStaticMembershipMaterializesDescendants() {
  const std::filesystem::path dir = makeTempDir();
  writeText(dir / "prefab.json", hierarchicalPrefabJson());

  karma::scenes::SceneDocument document{};
  document.name = "Prefab Instance Static Membership";
  document.source_path = dir / "runtime.kscene.json";
  document.prefab_instances.push_back(karma::scenes::ScenePrefabInstance{
      .id = "static_prefab",
      .prefab_path = "prefab.json",
      .static_component = karma::components::StaticComponent{
          .enabled = true,
          .include_descendants = true,
          .flags = karma::components::StaticComponentAll,
      },
  });
  document.prefab_instances.push_back(karma::scenes::ScenePrefabInstance{
      .id = "legacy_prefab",
      .prefab_path = "prefab.json",
  });

  karma::assets::AssetRegistry assets;
  karma::world::World world;
  karma::world::Scene scene;
  karma::scenes::SceneInstantiateResult instance =
      karma::scenes::instantiateScene(world, scene, assets, document);
  KARMA_REQUIRE(instance.success);

  const auto require_static = [&](karma::world::Entity entity) {
    KARMA_REQUIRE(world.has<karma::components::StaticComponent>(entity));
    const auto& membership =
        world.get<karma::components::StaticComponent>(entity);
    KARMA_REQUIRE(membership.enabled);
    KARMA_REQUIRE(membership.include_descendants);
    KARMA_REQUIRE(membership.flags ==
                  karma::components::StaticComponentAll);
  };
  require_static(instance.prefab_roots_by_id.at("static_prefab"));
  require_static(instance.navigation_owners_by_id.at(
      "prefab:static_prefab/node:1"));
  require_static(instance.navigation_owners_by_id.at(
      "prefab:static_prefab/node:2"));

  KARMA_REQUIRE(!world.has<karma::components::StaticComponent>(
      instance.prefab_roots_by_id.at("legacy_prefab")));
  KARMA_REQUIRE(!world.has<karma::components::StaticComponent>(
      instance.navigation_owners_by_id.at("prefab:legacy_prefab/node:1")));

  KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, instance));
  std::filesystem::remove_all(dir);
}

void testRuntimeV2LightmapsApplyTransactionallyAndCleanUp() {
  const std::filesystem::path dir = makeTempDir();
  karma::scenes::SceneDocument document = runtimeLightmapDocument(dir);
  const karma::scenes::SceneBakeDesc& bake = document.bakes.front();

  constexpr std::string_view kSourceMaterialMesh =
      "tests/runtime_lightmaps/source_material";
  constexpr std::string_view kSourceDefaultMesh =
      "tests/runtime_lightmaps/source_default";
  constexpr std::string_view kBaseMaterial =
      "tests/runtime_lightmaps/base_material";
  constexpr std::string_view kDerivedMaterialMesh =
      "tests/runtime_lightmaps/derived_material";
  constexpr std::string_view kDerivedDefaultMesh =
      "tests/runtime_lightmaps/derived_default";
  constexpr std::string_view kIrradiance =
      "tests/runtime_lightmaps/irradiance";
  constexpr std::string_view kDirection =
      "tests/runtime_lightmaps/direction";
  const std::filesystem::path material_mesh_path =
      "bakes/lightmaps/material.kbmesh";
  const std::filesystem::path default_mesh_path =
      "bakes/lightmaps/default.kbmesh";
  const std::filesystem::path irradiance_path =
      "bakes/lightmaps/scene.irradiance.krgba8";
  const std::filesystem::path direction_path =
      "bakes/lightmaps/scene.direction.krgba8";

  karma::assets::AssetRegistry assets;
  karma::world::MeshData source_material =
      runtimeLightmapMesh(std::string(kBaseMaterial));
  karma::world::MeshData source_default = runtimeLightmapMesh();
  // Keep the registry's geometry-content deduplication from aliasing the two
  // fixtures; their material-slot contracts intentionally differ.
  source_default.vertices[0].x = -0.75f;
  KARMA_REQUIRE(assets.registerMeshAsset(std::string(kSourceMaterialMesh),
                                        source_material));
  KARMA_REQUIRE(assets.registerMeshAsset(std::string(kSourceDefaultMesh),
                                        source_default));
  karma::rendering::MaterialAssetDesc base_material{};
  base_material.surface.base_color = {0.25f, 0.5f, 0.75f, 1.0f};
  KARMA_REQUIRE(assets.registerMaterialAsset(std::string(kBaseMaterial),
                                            std::move(base_material)));

  karma::world::MeshData derived_material = source_material;
  derived_material.uvs1 = derived_material.uvs;
  karma::world::MeshData derived_default = source_default;
  derived_default.uvs1 = derived_default.uvs;
  std::string artifact_diagnostic;
  KARMA_REQUIRE(karma::assets::saveBakedMeshArtifact(
      dir / material_mesh_path, derived_material, &artifact_diagnostic));
  KARMA_REQUIRE(artifact_diagnostic.empty());
  KARMA_REQUIRE(karma::assets::saveBakedMeshArtifact(
      dir / default_mesh_path, derived_default, &artifact_diagnostic));
  KARMA_REQUIRE(artifact_diagnostic.empty());
  const karma::assets::Rgba8Image irradiance{
      .width = 2,
      .height = 1,
      .pixels = {64u, 128u, 255u, 255u, 32u, 16u, 8u, 255u},
  };
  const karma::assets::Rgba8Image direction{
      .width = 1,
      .height = 1,
      .pixels = {128u, 255u, 128u, 255u},
  };
  KARMA_REQUIRE(karma::assets::saveBakedRgba8Artifact(
      dir / irradiance_path, irradiance, &artifact_diagnostic));
  KARMA_REQUIRE(artifact_diagnostic.empty());
  KARMA_REQUIRE(karma::assets::saveBakedRgba8Artifact(
      dir / direction_path, direction, &artifact_diagnostic));
  KARMA_REQUIRE(artifact_diagnostic.empty());

  Json manifest{
      {"schema", "karma.scene_bake"},
      {"version", 2},
      {"scene_fingerprint",
       karma::scenes::sceneBakeFingerprint(document, bake)},
      {"produced_assets",
       Json::array({
           Json{{"id", std::string(kDerivedMaterialMesh)},
                {"path", material_mesh_path.generic_string()},
                {"type", "baked_mesh"}},
           Json{{"id", std::string(kDerivedDefaultMesh)},
                {"path", default_mesh_path.generic_string()},
                {"type", "baked_mesh"}},
           Json{{"id", std::string(kIrradiance)},
                {"path", irradiance_path.generic_string()},
                {"type", "baked_irradiance_rgba8"}},
           Json{{"id", std::string(kDirection)},
                {"path", direction_path.generic_string()},
                {"type", "baked_direction_rgba8"}},
       })},
      {"lightmap_bindings",
       Json::array({
           Json{{"target_id", "entity:material_target"},
                {"derived_mesh_asset_key", std::string(kDerivedMaterialMesh)},
                {"irradiance_asset_key", std::string(kIrradiance)},
                {"direction_asset_key", std::string(kDirection)},
                {"uv_scale_offset", Json::array({0.5f, 0.75f, 0.1f, 0.2f})},
                {"intensity", 0.8f},
                {"mixed_light_mask", 1u}},
           Json{{"target_id", "entity:default_target"},
                {"derived_mesh_asset_key", std::string(kDerivedDefaultMesh)},
                {"irradiance_asset_key", std::string(kIrradiance)},
                {"direction_asset_key", std::string(kDirection)},
                {"uv_scale_offset", Json::array({1.0f, 1.0f, 0.0f, 0.0f})},
                {"intensity", 1.0f},
                {"mixed_light_mask", 1u}},
       })},
      {"lighting_output",
       Json{{"generated", true},
            {"mixed_light_ids", Json::array({"scene_light:mixed"})}}},
      {"navigation_bindings", Json::array()},
  };
  writeText(dir / bake.path, manifest.dump(2));

  karma::world::World world;
  karma::world::Scene scene;
  karma::scenes::SceneInstantiateResult instance =
      karma::scenes::instantiateScene(world, scene, assets, document);
  KARMA_REQUIRE(instance.success);
  KARMA_REQUIRE(instance.diagnostics.empty());

  const auto& material_component =
      world.get<karma::components::MeshComponent>(
          instance.find("material_target"));
  KARMA_REQUIRE(material_component.mesh_asset_key == kDerivedMaterialMesh);
  KARMA_REQUIRE(material_component.materials.size() == 1u);
  const std::string inherited_variant_key =
      material_component.materials.front().material_key;
  const auto* inherited_variant =
      assets.findMaterialVariant(inherited_variant_key);
  KARMA_REQUIRE(inherited_variant != nullptr);
  KARMA_REQUIRE(inherited_variant->base_material_key == kBaseMaterial);
  KARMA_REQUIRE(inherited_variant->textures.at("lightmap") == kIrradiance);
  KARMA_REQUIRE(inherited_variant->textures.at("lightmap_direction") ==
                kDirection);
  const auto* lightmap_enabled = std::get_if<bool>(
      &inherited_variant->params.at("lightmap_enabled"));
  KARMA_REQUIRE(lightmap_enabled != nullptr && *lightmap_enabled);
  const auto* lightmap_intensity = std::get_if<float>(
      &inherited_variant->params.at("lightmap_intensity"));
  KARMA_REQUIRE(lightmap_intensity != nullptr);
  KARMA_REQUIRE(nearlyEqual(*lightmap_intensity, 0.8f));
  const auto* uv_scale_offset = std::get_if<glm::vec4>(
      &inherited_variant->params.at("lightmap_uv_scale_offset"));
  KARMA_REQUIRE(uv_scale_offset != nullptr);
  KARMA_REQUIRE(nearlyEqual(uv_scale_offset->x, 0.5f));
  KARMA_REQUIRE(nearlyEqual(uv_scale_offset->y, 0.75f));
  KARMA_REQUIRE(nearlyEqual(uv_scale_offset->z, 0.1f));
  KARMA_REQUIRE(nearlyEqual(uv_scale_offset->w, 0.2f));
  const auto* mixed_mask_low = std::get_if<uint32_t>(
      &inherited_variant->params.at("lightmap_mixed_mask_low"));
  const auto* mixed_mask_high = std::get_if<uint32_t>(
      &inherited_variant->params.at("lightmap_mixed_mask_high"));
  KARMA_REQUIRE(mixed_mask_low != nullptr && *mixed_mask_low == 1u);
  KARMA_REQUIRE(mixed_mask_high != nullptr && *mixed_mask_high == 0u);

  const auto& default_component =
      world.get<karma::components::MeshComponent>(
          instance.find("default_target"));
  KARMA_REQUIRE(default_component.mesh_asset_key == kDerivedDefaultMesh);
  KARMA_REQUIRE(default_component.materials.size() == 1u);
  const auto* default_variant = assets.findMaterialVariant(
      default_component.materials.front().material_key);
  KARMA_REQUIRE(default_variant != nullptr);
  KARMA_REQUIRE(assets.findMaterialAsset(default_variant->base_material_key) !=
                nullptr);

  KARMA_REQUIRE(assets.findMeshAsset(kDerivedMaterialMesh) != nullptr);
  KARMA_REQUIRE(assets.findMeshAsset(kDerivedDefaultMesh) != nullptr);
  KARMA_REQUIRE(assets.findTextureAsset(kIrradiance) != nullptr);
  KARMA_REQUIRE(assets.findTextureAsset(kDirection) != nullptr);
  KARMA_REQUIRE(assets.findTextureAsset(kIrradiance)->semantic ==
                karma::assets::TextureAsset::Semantic::Linear);
  KARMA_REQUIRE(assets.findTextureAsset(kDirection)->semantic ==
                karma::assets::TextureAsset::Semantic::Data);
  KARMA_REQUIRE(instance.generated_mesh_asset_keys.size() == 2u);
  KARMA_REQUIRE(instance.generated_texture_asset_keys.size() == 2u);
  KARMA_REQUIRE(instance.generated_material_asset_keys.size() == 3u);

  const auto& mixed_light = world.get<karma::components::LightComponent>(
      instance.lights_by_id.at("mixed"));
  KARMA_REQUIRE(mixed_light.mixed_bake_mask_bit == 0u);
  const auto& baked_light = world.get<karma::components::LightComponent>(
      instance.lights_by_id.at("baked"));
  KARMA_REQUIRE(nearlyEqual(baked_light.intensity, 0.0f));
  const auto& realtime_light = world.get<karma::components::LightComponent>(
      instance.lights_by_id.at("realtime"));
  KARMA_REQUIRE(nearlyEqual(realtime_light.intensity, 3.0f));
  KARMA_REQUIRE(realtime_light.mixed_bake_mask_bit == UINT32_MAX);

  const std::vector<std::string> generated_mesh_keys =
      instance.generated_mesh_asset_keys;
  const std::vector<std::string> generated_texture_keys =
      instance.generated_texture_asset_keys;
  const std::vector<std::string> generated_material_keys =
      instance.generated_material_asset_keys;
  KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, instance));
  for (const std::string& key : generated_mesh_keys) {
    KARMA_REQUIRE(assets.findMeshAsset(key) == nullptr);
  }
  for (const std::string& key : generated_texture_keys) {
    KARMA_REQUIRE(assets.findTextureAsset(key) == nullptr);
  }
  for (const std::string& key : generated_material_keys) {
    KARMA_REQUIRE(assets.findMaterialAsset(key) == nullptr);
    KARMA_REQUIRE(assets.findMaterialVariant(key) == nullptr);
  }
  KARMA_REQUIRE(assets.findMeshAsset(kSourceMaterialMesh) != nullptr);
  KARMA_REQUIRE(assets.findMeshAsset(kSourceDefaultMesh) != nullptr);
  KARMA_REQUIRE(assets.findMaterialAsset(kBaseMaterial) != nullptr);

  // A declared direction map is part of the same all-or-nothing binding.
  std::filesystem::remove(dir / direction_path);
  karma::scenes::SceneInstantiateResult missing_artifact =
      karma::scenes::instantiateScene(world, scene, assets, document);
  KARMA_REQUIRE(missing_artifact.success);
  KARMA_REQUIRE(std::any_of(
      missing_artifact.diagnostics.begin(),
      missing_artifact.diagnostics.end(),
      [](const std::string& entry) {
        return entry.find("lightmap texture artifact") != std::string::npos;
      }));
  const auto& missing_mesh = world.get<karma::components::MeshComponent>(
      missing_artifact.find("material_target"));
  KARMA_REQUIRE(missing_mesh.mesh_asset_key == kSourceMaterialMesh);
  KARMA_REQUIRE(missing_mesh.materials.size() == 1u);
  KARMA_REQUIRE(missing_mesh.materials.front().material_key == kBaseMaterial);
  KARMA_REQUIRE(world.get<karma::components::LightComponent>(
                    missing_artifact.lights_by_id.at("mixed"))
                    .mixed_bake_mask_bit == UINT32_MAX);
  KARMA_REQUIRE(nearlyEqual(
      world.get<karma::components::LightComponent>(
               missing_artifact.lights_by_id.at("baked"))
          .intensity,
      4.0f));
  KARMA_REQUIRE(missing_artifact.generated_mesh_asset_keys.empty());
  KARMA_REQUIRE(missing_artifact.generated_texture_asset_keys.empty());
  KARMA_REQUIRE(missing_artifact.generated_material_asset_keys.empty());
  KARMA_REQUIRE(assets.findMeshAsset(kDerivedMaterialMesh) == nullptr);
  KARMA_REQUIRE(assets.findTextureAsset(kDirection) == nullptr);
  KARMA_REQUIRE(
      karma::scenes::destroyScene(world, scene, missing_artifact));

  manifest["scene_fingerprint"] = "stale-scene-fingerprint";
  writeText(dir / bake.path, manifest.dump(2));
  karma::scenes::SceneInstantiateResult stale =
      karma::scenes::instantiateScene(world, scene, assets, document);
  KARMA_REQUIRE(stale.success);
  KARMA_REQUIRE(std::any_of(
      stale.diagnostics.begin(), stale.diagnostics.end(),
      [](const std::string& entry) {
        return entry.find("fingerprint is stale") != std::string::npos;
      }));
  KARMA_REQUIRE(world.get<karma::components::MeshComponent>(
                    stale.find("material_target"))
                    .mesh_asset_key == kSourceMaterialMesh);
  KARMA_REQUIRE(nearlyEqual(
      world.get<karma::components::LightComponent>(
               stale.lights_by_id.at("baked"))
          .intensity,
      4.0f));
  KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, stale));
  std::filesystem::remove_all(dir);
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

void testBakedScenePackageSharesSourceOwnershipWithPrefab() {
  const std::filesystem::path dir = makeTempDir();
  writeText(dir / "env.hdr", "placeholder");
  writeText(dir / "prefab.json", simplePrefabJson());
  writeText(dir / "assets.package.json",
            R"({
  "version": 1,
  "assets": [
    {
      "type": "environment_map",
      "key": "tests/scene_runtime/baked_prefab_env",
      "path": "env.hdr"
    }
  ]
})");

  karma::assets::AssetPackageBakeOptions bake_options{};
  bake_options.package_id = "shared_scene_assets";
  bake_options.scene_fingerprint = "baked-prefab-runtime-test";
  const std::filesystem::path baked_dir =
      dir / "bakes/asset_cache/shared_scene_assets";
  std::string diagnostic;
  KARMA_REQUIRE(karma::assets::bakeAssetPackage(dir,
                                                baked_dir,
                                                bake_options,
                                                &diagnostic));
  KARMA_REQUIRE(diagnostic.empty());

  karma::scenes::SceneDocument document{};
  document.name = "Baked Package With Source Prefab";
  document.source_path = dir / "runtime.kscene.json";
  document.asset_packages.push_back(karma::scenes::SceneAssetRef{
      .id = "shared_scene_assets",
      .path = "assets.package.json",
      .baked_cache_path = "bakes/asset_cache/shared_scene_assets",
      .type = "asset_package",
  });
  document.prefab_instances.push_back(karma::scenes::ScenePrefabInstance{
      .id = "shared_prefab",
      .prefab_path = "prefab.json",
  });

  karma::assets::AssetRegistry assets;
  karma::world::World world;
  karma::world::Scene scene;
  karma::scenes::SceneInstantiateResult result =
      karma::scenes::instantiateScene(world, scene, assets, document);
  KARMA_REQUIRE(result.success);
  KARMA_REQUIRE(result.asset_packages.size() == 1u);
  KARMA_REQUIRE(result.prefab_asset_packages.size() == 1u);
  KARMA_REQUIRE(result.asset_packages[0].instance_id != 0u);
  KARMA_REQUIRE(result.asset_packages[0].instance_id ==
                result.prefab_asset_packages[0].instance_id);
  KARMA_REQUIRE(
      assets.findEnvironmentMap("tests/scene_runtime/baked_prefab_env") != nullptr);
  KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, result));
  KARMA_REQUIRE(
      assets.findEnvironmentMap("tests/scene_runtime/baked_prefab_env") == nullptr);
  std::filesystem::remove_all(dir);
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

  karma::assets::AssetPackageBakeOptions bake_options{};
  bake_options.package_id = "startup_assets";
  bake_options.scene_fingerprint = "startup-runtime-test";
  std::string diagnostic;
  KARMA_REQUIRE(karma::assets::bakeAssetPackage(
      dir,
      dir / "bakes/asset_cache/startup_assets",
      bake_options,
      &diagnostic));
  KARMA_REQUIRE(diagnostic.empty());

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
  std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
  testInMemorySceneValidationPreventsPartialInstantiation();
  testSceneReferenceRootPrecedence();
  testSceneInstanceRejectsWrongWorldTeardown();
  testStaticBoundsRejectFiniteInputOverflow();
  testStaticMetadataBuildCapturesTransformsAndBoundsWithoutFreezingRuntime();
  testInstantiateAndDestroyRuntimeScene();
  testPhysicsAuthoringComponentsSceneRoundTrip();
  testSceneContextualEntityReferencesResolveInTwoPasses();
  testAuthoredRenderDependenciesResolveOrRollback();
  testPrefabInstanceStaticMembershipMaterializesDescendants();
  testRuntimeV2LightmapsApplyTransactionallyAndCleanUp();
  testSceneAssetPackageLoadsFromBakedCacheAndReleases();
  testBakedScenePackageSharesSourceOwnershipWithPrefab();
  testEngineConfigStartupSceneAssetLoadsBeforeGameStart();
  return 0;
}
