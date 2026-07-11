#include "karma/scenes.h"

#include "scene_document_parser.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <system_error>
#include <string_view>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace karma::scenes {

namespace {

using Json = nlohmann::json;

bool fail(SceneLoadResult& result, std::string message) {
  result.diagnostics.push_back(std::move(message));
  result.document.reset();
  return false;
}

bool hasSceneDocumentExtension(const std::filesystem::path& path) {
  const std::string filename = path.filename().string();
  constexpr std::string_view suffix = ".kscene.json";
  return filename.size() >= suffix.size() &&
         filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool readJsonFile(const std::filesystem::path& path, Json& out, SceneLoadResult& result) {
  std::ifstream stream(path);
  if (!stream) {
    return fail(result, "failed to open scene document: " + path.string());
  }
  try {
    stream >> out;
  } catch (const std::exception& e) {
    return fail(result, std::string("failed to parse scene document JSON: ") + e.what());
  }
  if (!out.is_object()) {
    return fail(result, "scene document root must be an object");
  }
  return true;
}

Json vec3Json(const math::Vec3& value) {
  return Json::array({value.x, value.y, value.z});
}

Json quatJson(const math::Quat& value) {
  return Json::array({value.x, value.y, value.z, value.w});
}

Json colorJson(const math::Color& value) {
  return Json::array({value.r, value.g, value.b, value.a});
}

Json transformJson(const SceneTransform& transform) {
  return Json{
      {"position", vec3Json(transform.position)},
      {"rotation", quatJson(transform.rotation)},
      {"scale", vec3Json(transform.scale)},
  };
}

std::string portablePath(const std::filesystem::path& path) {
  return path.generic_string();
}

void setNonEmpty(Json& object, std::string_view field, const std::string& value) {
  if (!value.empty()) {
    object[std::string(field)] = value;
  }
}

void setNonEmpty(Json& object,
                 std::string_view field,
                 const std::filesystem::path& value) {
  if (!value.empty()) {
    object[std::string(field)] = portablePath(value);
  }
}

Json assetRefJson(const SceneAssetRef& asset) {
  Json json{
      {"id", asset.id},
      {"path", portablePath(asset.path)},
      {"type", asset.type},
  };
  setNonEmpty(json, "baked_cache", asset.baked_cache_path);
  setNonEmpty(json, "asset_package", asset.asset_package_id);
  return json;
}

Json prefabInstanceJson(const ScenePrefabInstance& prefab) {
  Json json{
      {"id", prefab.id},
      {"prefab", portablePath(prefab.prefab_path)},
      {"transform", transformJson(prefab.transform)},
      {"variables", prefab.variables},
  };
  setNonEmpty(json, "asset_package", prefab.asset_package_id);
  setNonEmpty(json, "parent_entity", prefab.parent_entity_id);
  if (prefab.static_component.has_value()) {
    const components::StaticComponent& membership = *prefab.static_component;
    json["static"] = Json{
        {"enabled", membership.enabled},
        {"include_descendants", membership.include_descendants},
        {"flags", membership.flags},
    };
  }
  return json;
}

Json entityJson(const SceneEntity& entity) {
  Json json{
      {"id", entity.id},
      {"transform", transformJson(entity.transform)},
      {"components", entity.components},
  };
  setNonEmpty(json, "name", entity.name);
  setNonEmpty(json, "parent", entity.parent_id);
  return json;
}

Json environmentJson(const SceneEnvironment& environment) {
  Json json{
      {"intensity", environment.component.intensity},
      {"draw_skybox", environment.component.draw_skybox},
      {"enabled", environment.component.enabled},
  };
  setNonEmpty(json, "id", environment.id);
  setNonEmpty(json, "entity", environment.entity_id);
  setNonEmpty(json, "environment_map", environment.environment_map_asset_id);
  setNonEmpty(json, "environment_map_path", environment.environment_map_path);
  return json;
}

std::string antiAliasingModeName(rendering::AntiAliasingMode mode) {
  switch (mode) {
    case rendering::AntiAliasingMode::MSAA:
      return "msaa";
    case rendering::AntiAliasingMode::SSAA:
      return "ssaa";
    case rendering::AntiAliasingMode::None:
      return "none";
  }
  return "none";
}

Json cameraJson(const SceneCamera& camera) {
  const components::CameraComponent& component = camera.component;
  Json shader_params = Json::object();
  for (const auto& [name, color] : component.shader_user_params) {
    shader_params[name] = colorJson(color);
  }
  Json json{
      {"id", camera.id},
      {"entity", camera.entity_id},
      {"perspective", component.perspective},
      {"render_shadows", component.render_shadows},
      {"fov_y_degrees", component.fov_y_degrees},
      {"near_clip", component.near_clip},
      {"far_clip", component.far_clip},
      {"ortho_left", component.ortho_left},
      {"ortho_right", component.ortho_right},
      {"ortho_top", component.ortho_top},
      {"ortho_bottom", component.ortho_bottom},
      {"primary", component.is_primary},
      {"render_to_texture", component.render_to_texture},
      {"anti_aliasing",
       Json{{"mode", antiAliasingModeName(component.anti_aliasing.mode)},
            {"msaa_samples", component.anti_aliasing.msaa_samples},
            {"ssaa_scale", component.anti_aliasing.ssaa_scale}}},
      {"shader_user_params", std::move(shader_params)},
  };
  setNonEmpty(json, "render_target_key", component.render_target_key);
  setNonEmpty(json, "frame_graph_key", component.frame_graph_key);
  setNonEmpty(json,
              "shader_override_vertex_path",
              component.shader_override_vertex_path);
  setNonEmpty(json,
              "shader_override_fragment_path",
              component.shader_override_fragment_path);
  return json;
}

std::string lightTypeName(components::LightComponent::Type type) {
  switch (type) {
    case components::LightComponent::Type::Directional:
      return "directional";
    case components::LightComponent::Type::Point:
      return "point";
    case components::LightComponent::Type::Spot:
      return "spot";
  }
  return {};
}

std::string lightBakeModeName(components::LightComponent::BakeMode mode) {
  switch (mode) {
    case components::LightComponent::BakeMode::Realtime:
      return "realtime";
    case components::LightComponent::BakeMode::Mixed:
      return "mixed";
    case components::LightComponent::BakeMode::Baked:
      return "baked";
  }
  return {};
}

Json lightJson(const SceneLight& light) {
  const components::LightComponent& component = light.component;
  return Json{
      {"id", light.id},
      {"entity", light.entity_id},
      {"type", lightTypeName(component.type)},
      {"bake_mode", lightBakeModeName(component.bake_mode)},
      {"color", colorJson(component.color)},
      {"intensity", component.intensity},
      {"range", component.range},
      {"inner_cone_degrees", component.inner_cone_degrees},
      {"outer_cone_degrees", component.outer_cone_degrees},
      {"casts_shadows", component.casts_shadows},
      {"shadow_extent", component.shadow_extent},
  };
}

Json staticComponentJson(const SceneStaticComponent& component) {
  Json json{
      {"id", component.id},
      {"entity", component.entity_id},
      {"transform", component.transform},
      {"render", component.render},
      {"lighting", component.lighting},
      {"collision", component.collision},
      {"navigation", component.navigation},
      {"casts_shadows", component.casts_shadows},
      {"receives_baked_lighting", component.receives_baked_lighting},
  };
  setNonEmpty(json, "gltf_scene", component.gltf_scene_id);
  setNonEmpty(json, "mesh", component.mesh_asset_key);
  setNonEmpty(json, "material", component.material_asset_key);
  return json;
}

Json bakedLightingJson(const BakedLightingComponent& lighting) {
  Json json{
      {"intensity", lighting.intensity},
      {"enabled", lighting.enabled},
  };
  setNonEmpty(json, "entity", lighting.entity_id);
  setNonEmpty(json, "lightmap_asset_key", lighting.lightmap_asset_key);
  setNonEmpty(json, "lightmap_path", lighting.lightmap_path);
  return json;
}

Json lightmapBakeSettingsJson(const SceneLightmapBakeSettings& settings) {
  return Json{
      {"enabled", settings.enabled},
      {"generate_uv1", settings.generate_uv1},
      {"texels_per_unit", settings.texels_per_unit},
      {"max_atlas_size", settings.max_atlas_size},
      {"padding", settings.padding},
      {"dilation", settings.dilation},
      {"sky_samples", settings.sky_samples},
      {"ao_max_distance", settings.ao_max_distance},
      {"directional", settings.directional},
  };
}

Json navigationBakeSettingsJson(const SceneNavigationBakeSettings& settings) {
  return Json{{"enabled", settings.enabled}};
}

Json bakeJson(const SceneBakeDesc& bake) {
  Json static_ids = Json::array();
  for (const std::string& id : bake.static_component_ids) {
    static_ids.push_back(id);
  }
  Json nav_caches = Json::array();
  for (const std::filesystem::path& path : bake.nav_cache_paths) {
    nav_caches.push_back(portablePath(path));
  }

  Json json{
      {"id", bake.id},
      {"enabled", bake.enabled},
      {"load_at_runtime", bake.load_at_runtime},
      {"lighting", lightmapBakeSettingsJson(bake.lighting)},
      {"navigation", navigationBakeSettingsJson(bake.navigation)},
      {"static", std::move(static_ids)},
      {"nav_cache", std::move(nav_caches)},
  };
  setNonEmpty(json, "path", bake.path);
  if (!bake.baked_lighting.entity_id.empty() ||
      !bake.baked_lighting.lightmap_asset_key.empty() ||
      !bake.baked_lighting.lightmap_path.empty() ||
      bake.baked_lighting.intensity != 1.0f || !bake.baked_lighting.enabled) {
    json["baked_lighting"] = bakedLightingJson(bake.baked_lighting);
  }
  return json;
}

template <typename Value, typename Serializer>
Json jsonArray(const std::vector<Value>& values, Serializer&& serializer) {
  Json array = Json::array();
  for (const Value& value : values) {
    array.push_back(serializer(value));
  }
  return array;
}

std::filesystem::path temporaryScenePath(const std::filesystem::path& path) {
  static std::atomic<uint64_t> sequence{0u};
  const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::string filename = path.filename().string() + ".tmp." +
                               std::to_string(timestamp) + "." +
                               std::to_string(sequence.fetch_add(1u));
  return path.parent_path() / filename;
}

void removeTemporaryFile(const std::filesystem::path& path) {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

}  // namespace

SceneLoadResult loadSceneDocument(const SceneLoadDesc& desc) {
  SceneLoadResult result{};
  if (desc.path.empty()) {
    fail(result, "scene document path must not be empty");
    return result;
  }
  if (desc.require_kscene_json_extension && !hasSceneDocumentExtension(desc.path)) {
    fail(result, "scene document path must end with .kscene.json: " + desc.path.string());
    return result;
  }

  Json root;
  if (!readJsonFile(desc.path, root, result)) {
    return result;
  }

  try {
    detail::parseSceneDocument(root, desc.path, result);
    if (result.document.has_value()) {
      result.document->reference_root = desc.reference_root;
    }
  } catch (const std::exception& e) {
    fail(result, std::string("failed to validate scene document: ") + e.what());
  }
  return result;
}

SceneLoadResult loadSceneDocument(const std::filesystem::path& path) {
  return loadSceneDocument(SceneLoadDesc{.path = path});
}

Json sceneDocumentToJson(const SceneDocument& document) {
  Json root{
      {"version", document.version},
      {"name", document.name},
      {"asset_packages",
       jsonArray(document.asset_packages,
                 [](const SceneAssetRef& value) { return assetRefJson(value); })},
      {"gltf_scenes",
       jsonArray(document.gltf_scenes,
                 [](const SceneAssetRef& value) { return assetRefJson(value); })},
      {"prefab_instances",
       jsonArray(document.prefab_instances,
                 [](const ScenePrefabInstance& value) {
                   return prefabInstanceJson(value);
                 })},
      {"entities",
       jsonArray(document.entities,
                 [](const SceneEntity& value) { return entityJson(value); })},
      {"cameras",
       jsonArray(document.cameras,
                 [](const SceneCamera& value) { return cameraJson(value); })},
      {"lights",
       jsonArray(document.lights,
                 [](const SceneLight& value) { return lightJson(value); })},
      {"static",
       jsonArray(document.static_components,
                 [](const SceneStaticComponent& value) {
                   return staticComponentJson(value);
                 })},
      {"bakes",
       jsonArray(document.bakes,
                 [](const SceneBakeDesc& value) { return bakeJson(value); })},
  };
  if (document.environment.has_value()) {
    root["environment"] = environmentJson(*document.environment);
  }
  return root;
}

SceneSaveResult saveSceneDocument(const SceneDocument& document,
                                  const SceneSaveDesc& desc) {
  SceneSaveResult result{.path = desc.path};
  if (desc.path.empty()) {
    result.diagnostics.push_back("scene document save path must not be empty");
    return result;
  }
  if (desc.require_kscene_json_extension && !hasSceneDocumentExtension(desc.path)) {
    result.diagnostics.push_back(
        "scene document save path must end with .kscene.json: " +
        desc.path.string());
    return result;
  }

  SceneValidationResult validation = validateSceneDocument(document);
  if (!validation.success()) {
    result.diagnostics = std::move(validation.diagnostics);
    return result;
  }

  Json json;
  try {
    json = sceneDocumentToJson(document);
  } catch (const std::exception& error) {
    result.diagnostics.push_back(std::string("failed to serialize scene document: ") +
                                 error.what());
    return result;
  }

  SceneLoadResult serialized_validation{};
  try {
    detail::parseSceneDocument(json, desc.path, serialized_validation);
  } catch (const std::exception& error) {
    result.diagnostics.push_back(
        std::string("failed to validate serialized scene document: ") +
        error.what());
    return result;
  }
  if (!serialized_validation.success()) {
    result.diagnostics = std::move(serialized_validation.diagnostics);
    return result;
  }

  std::error_code ec;
  const std::filesystem::path parent = desc.path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      result.diagnostics.push_back("failed to create scene document directory '" +
                                   parent.string() + "': " + ec.message());
      return result;
    }
  }

  const std::filesystem::path temporary_path = temporaryScenePath(desc.path);
  try {
    std::ofstream stream(temporary_path, std::ios::binary | std::ios::trunc);
    if (!stream) {
      result.diagnostics.push_back("failed to open temporary scene document '" +
                                   temporary_path.string() + "' for writing");
      return result;
    }
    stream << json.dump(2) << '\n';
    stream.flush();
    if (!stream) {
      removeTemporaryFile(temporary_path);
      result.diagnostics.push_back("failed to write temporary scene document '" +
                                   temporary_path.string() + "'");
      return result;
    }
    stream.close();
    if (!stream) {
      removeTemporaryFile(temporary_path);
      result.diagnostics.push_back("failed to close temporary scene document '" +
                                   temporary_path.string() + "'");
      return result;
    }
  } catch (const std::exception& error) {
    removeTemporaryFile(temporary_path);
    result.diagnostics.push_back(std::string("failed to write scene document: ") +
                                 error.what());
    return result;
  }

#if defined(_WIN32)
  if (!MoveFileExW(temporary_path.c_str(),
                   desc.path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    ec = std::error_code(static_cast<int>(GetLastError()),
                         std::system_category());
  }
#else
  std::filesystem::rename(temporary_path, desc.path, ec);
#endif
  if (ec) {
    removeTemporaryFile(temporary_path);
    result.diagnostics.push_back("failed to atomically replace scene document '" +
                                 desc.path.string() + "': " + ec.message());
  }
  return result;
}

SceneSaveResult saveSceneDocument(const SceneDocument& document,
                                  const std::filesystem::path& path) {
  return saveSceneDocument(document, SceneSaveDesc{.path = path});
}

}  // namespace karma::scenes
