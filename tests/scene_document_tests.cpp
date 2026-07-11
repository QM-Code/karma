#include "karma/assets.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using Json = nlohmann::json;

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
      ("karma_scene_document_tests_" + std::to_string(now));
  std::filesystem::create_directories(dir);
  return dir;
}

void writeJson(const std::filesystem::path& path, const Json& json) {
  std::ofstream stream(path);
  stream << json.dump(2) << '\n';
}

std::string readText(const std::filesystem::path& path) {
  std::ifstream stream(path);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

Json validSceneJson() {
  return Json{
      {"version", 1},
      {"name", "Parser Fixture"},
      {"asset_packages",
       Json::array({Json{{"id", "base_assets"},
                         {"path", "packages/base.assets.package.json"},
                         {"baked_cache", "bakes/asset_cache/base_assets"}}})},
      {"gltf_scenes",
       Json::array({Json{{"id", "city_gltf"},
                         {"path", "models/city.glb"},
                         {"asset_package", "base_assets"}}})},
      {"prefab_instances",
       Json::array({Json{{"id", "torch_instance"},
                         {"prefab", "prefabs/torch/prefab.json"},
                         {"asset_package", "base_assets"},
                         {"parent_entity", "root"},
                         {"transform",
                          Json{{"position", Json::array({1.0f, 2.0f, 3.0f})},
                               {"rotation", Json::array({0.0f, 0.0f, 0.0f, 1.0f})},
                               {"scale", Json::array({1.0f, 1.0f, 1.0f})}}},
                         {"variables", Json{{"variant", "warm"}}},
                         {"static",
                          Json{{"enabled", true},
                               {"include_descendants", true},
                               {"flags",
                                karma::components::StaticComponentLighting |
                                    karma::components::StaticComponentShadows |
                                    karma::components::StaticComponentCollision}}}}})},
      {"entities",
       Json::array({Json{{"id", "root"},
                         {"name", "Root"},
                         {"transform",
                          Json{{"position", Json::array({0.0f, 1.0f, 2.0f})},
                               {"rotation", Json::array({0.0f, 0.0f, 0.0f, 1.0f})},
                               {"scale", Json::array({1.0f, 1.0f, 1.0f})}}},
                         {"components", Json{{"TagComponent", Json::object()}}}},
                   Json{{"id", "camera_entity"}, {"parent", "root"}},
                   Json{{"id", "sun_entity"}, {"parent", "root"}}})},
      {"environment",
       Json{{"entity", "root"},
            {"environment_map", "env/sky"},
            {"environment_map_path", "environment/sky.hdr"},
            {"intensity", 1.5f},
            {"draw_skybox", true}}},
      {"cameras",
       Json::array({Json{{"id", "main_camera"},
                         {"entity", "camera_entity"},
                         {"primary", true},
                         {"frame_graph_key", "render_graph/cinematic"},
                         {"fov_y_degrees", 70.0f}}})},
      {"lights",
       Json::array({Json{{"id", "sun"},
                         {"entity", "sun_entity"},
                         {"type", "directional"},
                         {"color", Json::array({1.0f, 0.95f, 0.8f, 1.0f})},
                         {"intensity", 4.0f},
                         {"casts_shadows", true}}})},
      {"static",
       Json::array({Json{{"id", "city_static"},
                         {"entity", "root"},
                         {"gltf_scene", "city_gltf"},
                         {"transform", true},
                         {"render", true},
                         {"lighting", true},
                         {"collision", true},
                         {"navigation", true},
                         {"casts_shadows", true},
                         {"receives_baked_lighting", true}}})},
      {"bakes",
       Json::array({Json{{"id", "main_bake"},
                         {"path", "bakes/main.kbake.json"},
                         {"static", Json::array({"city_static"})},
                         {"baked_lighting",
                          Json{{"entity", "root"},
                               {"lightmap_asset_key", "city/lightmap"},
                               {"lightmap_path", "bakes/city_lightmap.ktx2"},
                               {"intensity", 0.8f}}}}})},
  };
}

karma::scenes::SceneLoadResult loadTempScene(const Json& json,
                                             const std::string& filename = "scene.kscene.json") {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path path = dir / filename;
  writeJson(path, json);
  return karma::scenes::loadSceneDocument(path);
}

void requireFailed(const Json& json) {
  const karma::scenes::SceneLoadResult result = loadTempScene(json);
  KARMA_REQUIRE(!result.success());
  KARMA_REQUIRE(!result.document.has_value());
  KARMA_REQUIRE(!result.diagnostics.empty());
}

void testValidSceneDocument() {
  const karma::scenes::SceneLoadResult result = loadTempScene(validSceneJson());
  KARMA_REQUIRE(result.success());
  KARMA_REQUIRE(result.document.has_value());

  const karma::scenes::SceneDocument& document = *result.document;
  KARMA_REQUIRE(document.version == karma::scenes::kSceneDocumentVersion);
  KARMA_REQUIRE(document.name == "Parser Fixture");
  KARMA_REQUIRE(document.asset_packages.size() == 1);
  KARMA_REQUIRE(document.asset_packages[0].id == "base_assets");
  KARMA_REQUIRE(document.asset_packages[0].path.generic_string() ==
                "packages/base.assets.package.json");
  KARMA_REQUIRE(document.asset_packages[0].baked_cache_path.generic_string() ==
                "bakes/asset_cache/base_assets");
  KARMA_REQUIRE(document.gltf_scenes.size() == 1);
  KARMA_REQUIRE(document.gltf_scenes[0].asset_package_id == "base_assets");
  KARMA_REQUIRE(document.prefab_instances.size() == 1);
  KARMA_REQUIRE(document.prefab_instances[0].parent_entity_id == "root");
  KARMA_REQUIRE(document.prefab_instances[0].static_component.has_value());
  KARMA_REQUIRE(document.prefab_instances[0].static_component->enabled);
  KARMA_REQUIRE(
      document.prefab_instances[0].static_component->include_descendants);
  KARMA_REQUIRE(
      document.prefab_instances[0].static_component->flags ==
      (karma::components::StaticComponentLighting |
       karma::components::StaticComponentShadows |
       karma::components::StaticComponentCollision));
  KARMA_REQUIRE(document.entities.size() == 3);
  KARMA_REQUIRE(document.environment.has_value());
  KARMA_REQUIRE(document.environment->environment_map_path.generic_string() ==
                "environment/sky.hdr");
  KARMA_REQUIRE(document.environment->component.intensity == 1.5f);
  KARMA_REQUIRE(document.cameras.size() == 1);
  KARMA_REQUIRE(document.cameras[0].component.is_primary);
  KARMA_REQUIRE(document.cameras[0].component.frame_graph_key ==
                "render_graph/cinematic");
  KARMA_REQUIRE(document.lights.size() == 1);
  KARMA_REQUIRE(document.static_components.size() == 1);
  KARMA_REQUIRE(document.static_components[0].transform);
  KARMA_REQUIRE(document.static_components[0].render);
  KARMA_REQUIRE(document.static_components[0].lighting);
  KARMA_REQUIRE(document.static_components[0].collision);
  KARMA_REQUIRE(document.static_components[0].navigation);
  KARMA_REQUIRE(document.static_components[0].receives_baked_lighting);
  KARMA_REQUIRE(document.bakes.size() == 1);
  KARMA_REQUIRE(document.bakes[0].static_component_ids.size() == 1);
  KARMA_REQUIRE(document.bakes[0].baked_lighting.lightmap_path.generic_string() ==
                "bakes/city_lightmap.ktx2");
}

void testPrefabInstanceStaticMembershipCompatibilityAndValidation() {
  const karma::scenes::SceneLoadResult parsed =
      loadTempScene(validSceneJson());
  KARMA_REQUIRE(parsed.success());
  const Json canonical =
      karma::scenes::sceneDocumentToJson(*parsed.document);
  KARMA_REQUIRE(canonical["prefab_instances"][0]["static"] ==
                validSceneJson()["prefab_instances"][0]["static"]);

  Json legacy = validSceneJson();
  legacy["prefab_instances"][0].erase("static");
  const karma::scenes::SceneLoadResult legacy_result = loadTempScene(legacy);
  KARMA_REQUIRE(legacy_result.success());
  KARMA_REQUIRE(
      !legacy_result.document->prefab_instances[0].static_component.has_value());
  KARMA_REQUIRE(!karma::scenes::sceneDocumentToJson(*legacy_result.document)
                     ["prefab_instances"][0]
                         .contains("static"));

  Json invalid_type = validSceneJson();
  invalid_type["prefab_instances"][0]["static"] = true;
  requireFailed(invalid_type);

  Json invalid_flags = validSceneJson();
  invalid_flags["prefab_instances"][0]["static"]["flags"] = 1u << 31u;
  requireFailed(invalid_flags);

  karma::scenes::SceneDocument invalid_document = *parsed.document;
  invalid_document.prefab_instances[0].static_component->flags = 1u << 31u;
  KARMA_REQUIRE(
      !karma::scenes::validateSceneDocument(invalid_document).success());
}

void testBadVersionRejected() {
  Json json = validSceneJson();
  json["version"] = 2;
  requireFailed(json);
}

void testBadPathsRejected() {
  {
    Json json = validSceneJson();
    json["asset_packages"][0]["path"] = "/absolute/assets.package.json";
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["asset_packages"][0]["baked_cache"] = "/absolute/bakes/cache";
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["asset_packages"][0]["baked_cache"] = "bakes/../cache";
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["gltf_scenes"][0]["path"] = "models\\city.glb";
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["prefab_instances"][0]["prefab"] = "../prefabs/torch/prefab.json";
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["environment"]["environment_map_path"] = "environment/../sky.hdr";
    requireFailed(json);
  }
}

void testDuplicateIdsRejected() {
  Json json = validSceneJson();
  json["entities"].push_back(Json{{"id", "root"}});
  requireFailed(json);
}

void testMissingRefsRejected() {
  {
    Json json = validSceneJson();
    json["cameras"][0]["entity"] = "missing_camera_entity";
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["static"][0]["gltf_scene"] = "missing_gltf";
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["bakes"][0]["static"] = Json::array({"missing_static"});
    requireFailed(json);
  }
}

void testHierarchyAndNumericValidation() {
  {
    Json json = validSceneJson();
    json["entities"][1]["parent"] = "sun_entity";
    json["entities"][2]["parent"] = "camera_entity";
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["cameras"][0]["entity_id"] = "root";
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["environment"]["intensity"] = 1e100;
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["environment"]["intensity"] = -0.1f;
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["bakes"][0]["baked_lighting"]["intensity"] = -0.1f;
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["version"] = std::numeric_limits<uint64_t>::max();
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["entities"][0]["transform"]["rotation"] = Json::array({0, 0, 0, 0});
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["cameras"][0]["near_clip"] = 10.0f;
    json["cameras"][0]["far_clip"] = 1.0f;
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["lights"][0]["intensity"] = -1.0f;
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["cameras"][0]["anti_aliasing"] =
        Json{{"mode", "temporal"}, {"msaa_samples", 4}, {"ssaa_scale", 1.0f}};
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["cameras"][0]["shader_user_params"] =
        Json{{"tint", "not a color"}};
    requireFailed(json);
  }
  {
    Json json = validSceneJson();
    json["cameras"][0]["perspective"] = false;
    json["cameras"][0]["ortho_left"] = -20.0f;
    json["cameras"][0]["ortho_right"] = 20.0f;
    json["cameras"][0]["ortho_top"] = 10.0f;
    json["cameras"][0]["ortho_bottom"] = -10.0f;
    const karma::scenes::SceneLoadResult result = loadTempScene(json);
    KARMA_REQUIRE(result.success());
    KARMA_REQUIRE(result.document->cameras[0].component.ortho_left == -20.0f);
    KARMA_REQUIRE(result.document->cameras[0].component.ortho_right == 20.0f);
  }
}

void testCanonicalSaveRoundTripAndAtomicValidation() {
  const karma::scenes::SceneLoadResult parsed = loadTempScene(validSceneJson());
  KARMA_REQUIRE(parsed.success());
  karma::scenes::SceneDocument document = *parsed.document;
  document.source_path = "/machine-specific/original.kscene.json";
  document.reference_root = "/machine-specific/content";
  auto& authored_camera = document.cameras.front().component;
  authored_camera.render_to_texture = true;
  authored_camera.render_target_key = "editor/cinematic";
  authored_camera.anti_aliasing =
      karma::rendering::AntiAliasingSettings::ssaa(2.5f);
  authored_camera.shader_user_params["tint"] =
      karma::math::Color{0.25f, 0.5f, 0.75f, 1.0f};

  const Json canonical = karma::scenes::sceneDocumentToJson(document);
  KARMA_REQUIRE(!canonical.contains("source_path"));
  KARMA_REQUIRE(!canonical.contains("reference_root"));
  KARMA_REQUIRE(canonical["prefab_instances"][0].contains("prefab"));
  KARMA_REQUIRE(!canonical["prefab_instances"][0].contains("path"));

  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path path = dir / "nested/saved.kscene.json";
  karma::scenes::SceneSaveResult saved =
      karma::scenes::saveSceneDocument(document, path);
  KARMA_REQUIRE(saved.success());
  KARMA_REQUIRE(saved.path == path);

  Json disk_json;
  {
    std::ifstream stream(path);
    stream >> disk_json;
  }
  KARMA_REQUIRE(disk_json == canonical);

  const std::filesystem::path load_root = dir / "content-root";
  const karma::scenes::SceneLoadResult round_trip =
      karma::scenes::loadSceneDocument(karma::scenes::SceneLoadDesc{
          .path = path,
          .reference_root = load_root,
      });
  KARMA_REQUIRE(round_trip.success());
  KARMA_REQUIRE(round_trip.document->reference_root == load_root);
  KARMA_REQUIRE(karma::scenes::sceneDocumentToJson(*round_trip.document) ==
                canonical);
  const auto& loaded_camera = round_trip.document->cameras.front().component;
  KARMA_REQUIRE(loaded_camera.render_to_texture);
  KARMA_REQUIRE(loaded_camera.render_target_key == "editor/cinematic");
  KARMA_REQUIRE(loaded_camera.anti_aliasing.mode ==
                karma::rendering::AntiAliasingMode::SSAA);
  KARMA_REQUIRE(loaded_camera.anti_aliasing.ssaa_scale == 2.5f);
  const karma::math::Color& loaded_tint =
      loaded_camera.shader_user_params.at("tint");
  KARMA_REQUIRE(loaded_tint.r == 0.25f && loaded_tint.g == 0.5f &&
                loaded_tint.b == 0.75f && loaded_tint.a == 1.0f);

  const std::string original_contents = readText(path);
  document.asset_packages[0].path = "/machine-specific/assets.package.json";
  const karma::scenes::SceneSaveResult non_portable =
      karma::scenes::saveSceneDocument(document, path);
  KARMA_REQUIRE(!non_portable.success());
  KARMA_REQUIRE(!non_portable.diagnostics.empty());
  KARMA_REQUIRE(readText(path) == original_contents);
  document.asset_packages[0].path = "packages/base.assets.package.json";

  document.entities.push_back(document.entities.front());
  const karma::scenes::SceneSaveResult rejected =
      karma::scenes::saveSceneDocument(document, path);
  KARMA_REQUIRE(!rejected.success());
  KARMA_REQUIRE(!rejected.diagnostics.empty());
  KARMA_REQUIRE(readText(path) == original_contents);

  for (const auto& entry : std::filesystem::directory_iterator(path.parent_path())) {
    KARMA_REQUIRE(entry.path().filename().string().find("saved.kscene.json.tmp.") !=
                  0u);
  }
}

void testSceneAssetPackageImportRegistersAndUnloadsScene() {
  const std::filesystem::path dir = makeTempDir();
  writeJson(dir / "level.kscene.json",
            Json{{"version", 1},
                 {"name", "Package Scene"},
                 {"entities", Json::array({Json{{"id", "root"}}})}});
  writeJson(dir / "assets.package.json",
            Json{{"version", 1},
                 {"assets",
                  Json::array({Json{{"type", "scene"},
                                    {"key", "tests/scenes/package_scene"},
                                    {"path", "level.kscene.json"}}})}});

  karma::assets::AssetRegistry assets;
  std::string diagnostic;
  std::optional<karma::assets::AssetPackageHandle> package =
      karma::assets::importAssetPackage(assets, dir, &diagnostic);
  KARMA_REQUIRE(package.has_value());
  KARMA_REQUIRE(diagnostic.empty());
  KARMA_REQUIRE(package->assets.size() == 1u);
  KARMA_REQUIRE(package->assets[0].type == "scene");
  KARMA_REQUIRE(package->assets[0].key == "tests/scenes/package_scene");

  const karma::assets::SceneAsset* scene =
      assets.findSceneAsset("tests/scenes/package_scene");
  KARMA_REQUIRE(scene != nullptr);
  KARMA_REQUIRE(scene->source_path.filename() == "level.kscene.json");
  KARMA_REQUIRE(scene->document.name == "Package Scene");
  KARMA_REQUIRE(scene->document.entities.size() == 1u);
  KARMA_REQUIRE(scene->document.entities[0].id == "root");

  KARMA_REQUIRE(karma::assets::unloadAssetPackage(assets, *package));
  KARMA_REQUIRE(assets.findSceneAsset("tests/scenes/package_scene") == nullptr);
}

}  // namespace

int main() {
  testValidSceneDocument();
  testPrefabInstanceStaticMembershipCompatibilityAndValidation();
  testBadVersionRejected();
  testBadPathsRejected();
  testDuplicateIdsRejected();
  testMissingRefsRejected();
  testHierarchyAndNumericValidation();
  testCanonicalSaveRoundTripAndAtomicValidation();
  testSceneAssetPackageImportRegistersAndUnloadsScene();
  return 0;
}
