#include "scene_document_parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace karma::scenes {

namespace {

using Json = nlohmann::json;

bool fail(SceneLoadResult& result, std::string message) {
  result.diagnostics.push_back(std::move(message));
  result.document.reset();
  return false;
}

bool isPortableRelativePath(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  if (value.find('\\') != std::string_view::npos ||
      value.find('\0') != std::string_view::npos) {
    return false;
  }
  if (value.front() == '/') {
    return false;
  }
  if (value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) &&
      value[1] == ':') {
    return false;
  }

  std::size_t start = 0;
  while (start <= value.size()) {
    const std::size_t end = value.find('/', start);
    const std::string_view segment =
        end == std::string_view::npos ? value.substr(start) : value.substr(start, end - start);
    if (segment == "..") {
      return false;
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return true;
}

bool readStringField(const Json& object,
                     std::string_view field,
                     std::string& out,
                     SceneLoadResult& result,
                     bool required = false) {
  const auto it = object.find(std::string(field));
  if (it == object.end()) {
    if (required) {
      return fail(result, "scene field '" + std::string(field) + "' is required");
    }
    return true;
  }
  if (!it->is_string()) {
    return fail(result, "scene field '" + std::string(field) + "' must be a string");
  }
  out = it->get<std::string>();
  if (required && out.empty()) {
    return fail(result, "scene field '" + std::string(field) + "' must not be empty");
  }
  return true;
}

bool readStringAliasField(const Json& object,
                          std::initializer_list<std::string_view> fields,
                          std::string& out,
                          SceneLoadResult& result,
                          bool required = false) {
  bool found = false;
  for (const std::string_view field : fields) {
    const auto it = object.find(std::string(field));
    if (it == object.end()) {
      continue;
    }
    if (!it->is_string()) {
      return fail(result, "scene field '" + std::string(field) + "' must be a string");
    }
    const std::string value = it->get<std::string>();
    if (!value.empty() && !out.empty() && value != out) {
      return fail(result, "scene alias fields contain conflicting values");
    }
    if (!value.empty() || out.empty()) {
      out = value;
    }
    found = true;
  }
  if (required && (!found || out.empty())) {
    return fail(result, "scene requires one non-empty string reference field");
  }
  return true;
}

bool readBoolField(const Json& object, std::string_view field, bool& out, SceneLoadResult& result) {
  const auto it = object.find(std::string(field));
  if (it == object.end()) {
    return true;
  }
  if (!it->is_boolean()) {
    return fail(result, "scene field '" + std::string(field) + "' must be a boolean");
  }
  out = it->get<bool>();
  return true;
}

bool readFloatField(const Json& object,
                    std::string_view field,
                    float& out,
                    SceneLoadResult& result) {
  const auto it = object.find(std::string(field));
  if (it == object.end()) {
    return true;
  }
  if (!it->is_number()) {
    return fail(result, "scene field '" + std::string(field) + "' must be a number");
  }
  const double value = it->get<double>();
  if (!std::isfinite(value) ||
      value < -static_cast<double>(std::numeric_limits<float>::max()) ||
      value > static_cast<double>(std::numeric_limits<float>::max())) {
    return fail(result, "scene field '" + std::string(field) + "' must be finite");
  }
  out = static_cast<float>(value);
  return true;
}

bool readUint32Field(const Json& object,
                     std::string_view field,
                     uint32_t& out,
                     SceneLoadResult& result) {
  const auto it = object.find(std::string(field));
  if (it == object.end()) {
    return true;
  }
  if (!it->is_number_unsigned() && !it->is_number_integer()) {
    return fail(result, "scene field '" + std::string(field) +
                            "' must be a non-negative integer");
  }
  uint64_t value = 0u;
  if (it->is_number_unsigned()) {
    value = it->get<uint64_t>();
  } else {
    const int64_t signed_value = it->get<int64_t>();
    if (signed_value < 0) {
      return fail(result, "scene field '" + std::string(field) +
                              "' is outside the uint32 range");
    }
    value = static_cast<uint64_t>(signed_value);
  }
  if (value > std::numeric_limits<uint32_t>::max()) {
    return fail(result, "scene field '" + std::string(field) +
                            "' is outside the uint32 range");
  }
  out = static_cast<uint32_t>(value);
  return true;
}

bool readRelativePathField(const Json& object,
                           std::string_view field,
                           std::filesystem::path& out,
                           SceneLoadResult& result,
                           bool required = false) {
  std::string value;
  if (!readStringField(object, field, value, result, required)) {
    return false;
  }
  if (value.empty()) {
    return !required;
  }
  if (!isPortableRelativePath(value)) {
    return fail(result, "scene path field '" + std::string(field) +
                            "' must be a relative portable path without '..': " + value);
  }
  out = std::filesystem::path(value);
  return true;
}

bool readRelativePathAliasField(const Json& object,
                                std::initializer_list<std::string_view> fields,
                                std::filesystem::path& out,
                                SceneLoadResult& result,
                                bool required = false) {
  std::string value;
  if (!readStringAliasField(object, fields, value, result, required)) {
    return false;
  }
  if (value.empty()) {
    return !required;
  }
  if (!isPortableRelativePath(value)) {
    return fail(result, "scene path must be a relative portable path without '..': " + value);
  }
  out = std::filesystem::path(value);
  return true;
}

bool registerId(std::unordered_set<std::string>& ids,
                std::string_view kind,
                const std::string& id,
                SceneLoadResult& result) {
  if (id.empty()) {
    return fail(result, std::string(kind) + " id must not be empty");
  }
  if (!ids.insert(id).second) {
    return fail(result, "duplicate scene id: " + id);
  }
  return true;
}

bool requireRef(const std::unordered_set<std::string>& ids,
                std::string_view kind,
                const std::string& id,
                SceneLoadResult& result) {
  if (id.empty()) {
    return true;
  }
  if (ids.find(id) == ids.end()) {
    return fail(result, "missing " + std::string(kind) + " reference: " + id);
  }
  return true;
}

bool readVec3Value(const Json& value, math::Vec3& out) {
  if (!value.is_array() || value.size() != 3) {
    return false;
  }
  std::array<float, 3> values{};
  for (size_t index = 0; index < values.size(); ++index) {
    if (!value[index].is_number()) {
      return false;
    }
    const double scalar = value[index].get<double>();
    if (!std::isfinite(scalar) ||
        std::abs(scalar) > static_cast<double>(std::numeric_limits<float>::max())) {
      return false;
    }
    values[index] = static_cast<float>(scalar);
  }
  out = math::Vec3{values[0], values[1], values[2]};
  return true;
}

bool readQuatValue(const Json& value, math::Quat& out) {
  if (!value.is_array() || value.size() != 4) {
    return false;
  }
  std::array<float, 4> values{};
  for (size_t index = 0; index < values.size(); ++index) {
    if (!value[index].is_number()) {
      return false;
    }
    const double scalar = value[index].get<double>();
    if (!std::isfinite(scalar) ||
        std::abs(scalar) > static_cast<double>(std::numeric_limits<float>::max())) {
      return false;
    }
    values[index] = static_cast<float>(scalar);
  }
  out = math::Quat{values[0], values[1], values[2], values[3]};
  return true;
}

bool readColorValue(const Json& value, math::Color& out) {
  if (!value.is_array() || (value.size() != 3 && value.size() != 4)) {
    return false;
  }
  std::array<float, 4> values{0.0f, 0.0f, 0.0f, 1.0f};
  for (size_t index = 0; index < value.size(); ++index) {
    if (!value[index].is_number()) {
      return false;
    }
    const double scalar = value[index].get<double>();
    if (!std::isfinite(scalar) ||
        std::abs(scalar) > static_cast<double>(std::numeric_limits<float>::max())) {
      return false;
    }
    values[index] = static_cast<float>(scalar);
  }
  out = math::Color{values[0], values[1], values[2], values[3]};
  return true;
}

bool readVec3Field(const Json& object,
                   std::string_view field,
                   math::Vec3& out,
                   SceneLoadResult& result) {
  const auto it = object.find(std::string(field));
  if (it == object.end()) {
    return true;
  }
  if (!readVec3Value(*it, out)) {
    return fail(result, "scene field '" + std::string(field) + "' must be a vec3 array");
  }
  return true;
}

bool readQuatField(const Json& object,
                   std::string_view field,
                   math::Quat& out,
                   SceneLoadResult& result) {
  const auto it = object.find(std::string(field));
  if (it == object.end()) {
    return true;
  }
  if (!readQuatValue(*it, out)) {
    return fail(result, "scene field '" + std::string(field) + "' must be a quat array");
  }
  return true;
}

bool readColorField(const Json& object,
                    std::string_view field,
                    math::Color& out,
                    SceneLoadResult& result) {
  const auto it = object.find(std::string(field));
  if (it == object.end()) {
    return true;
  }
  if (!readColorValue(*it, out)) {
    return fail(result, "scene field '" + std::string(field) + "' must be a color array");
  }
  return true;
}

bool readCameraAntiAliasing(const Json& camera_json,
                            rendering::AntiAliasingSettings& out,
                            SceneLoadResult& result) {
  const auto it = camera_json.find("anti_aliasing");
  if (it == camera_json.end()) return true;
  if (!it->is_object()) {
    return fail(result, "scene camera anti_aliasing must be an object");
  }
  std::string mode;
  if (!readStringField(*it, "mode", mode, result) ||
      !readUint32Field(*it, "msaa_samples", out.msaa_samples, result) ||
      !readFloatField(*it, "ssaa_scale", out.ssaa_scale, result)) {
    return false;
  }
  if (mode.empty() || mode == "none") {
    out.mode = rendering::AntiAliasingMode::None;
  } else if (mode == "msaa") {
    out.mode = rendering::AntiAliasingMode::MSAA;
  } else if (mode == "ssaa") {
    out.mode = rendering::AntiAliasingMode::SSAA;
  } else {
    return fail(result,
                "scene camera anti_aliasing mode must be none, msaa, or ssaa");
  }
  return true;
}

bool readCameraShaderParams(
    const Json& camera_json,
    std::unordered_map<std::string, math::Color>& out,
    SceneLoadResult& result) {
  const auto it = camera_json.find("shader_user_params");
  if (it == camera_json.end()) return true;
  if (!it->is_object()) {
    return fail(result, "scene camera shader_user_params must be an object");
  }
  for (auto parameter = it->begin(); parameter != it->end(); ++parameter) {
    math::Color color{};
    if (parameter.key().empty() || !readColorValue(parameter.value(), color)) {
      return fail(result,
                  "scene camera shader_user_params entries require a non-empty "
                  "name and finite color array");
    }
    out[parameter.key()] = color;
  }
  return true;
}

bool readTransform(const Json& object, SceneTransform& out, SceneLoadResult& result) {
  const Json* transform = &object;
  const auto transform_it = object.find("transform");
  if (transform_it != object.end()) {
    if (!transform_it->is_object()) {
      return fail(result, "scene field 'transform' must be an object");
    }
    transform = &*transform_it;
  }
  return readVec3Field(*transform, "position", out.position, result) &&
         readQuatField(*transform, "rotation", out.rotation, result) &&
         readVec3Field(*transform, "scale", out.scale, result);
}

bool readAssetArray(const Json& root,
                    std::string_view field,
                    std::string_view default_type,
                    std::unordered_set<std::string>& all_ids,
                    std::unordered_set<std::string>& typed_ids,
                    std::vector<SceneAssetRef>& out,
                    SceneLoadResult& result) {
  const auto it = root.find(std::string(field));
  if (it == root.end()) {
    return true;
  }
  if (!it->is_array()) {
    return fail(result, "scene field '" + std::string(field) + "' must be an array");
  }

  for (const Json& entry_json : *it) {
    SceneAssetRef entry{};
    entry.type = std::string(default_type);
    if (entry_json.is_string()) {
      const std::string value = entry_json.get<std::string>();
      if (!isPortableRelativePath(value)) {
        return fail(result, "scene asset path must be a relative portable path without '..': " +
                                value);
      }
      entry.path = value;
      entry.id = entry.path.stem().string();
    } else if (entry_json.is_object()) {
      if (!readStringField(entry_json, "id", entry.id, result, true) ||
          !readRelativePathField(entry_json, "path", entry.path, result, true) ||
          !readRelativePathField(entry_json,
                                 "baked_cache",
                                 entry.baked_cache_path,
                                 result) ||
          !readStringField(entry_json, "type", entry.type, result) ||
          !readStringField(entry_json, "asset_package", entry.asset_package_id, result) ||
          !readStringField(entry_json, "asset_package_id", entry.asset_package_id, result)) {
        return false;
      }
    } else {
      return fail(result, "scene field '" + std::string(field) +
                              "' entries must be objects or strings");
    }

    if (!registerId(all_ids, default_type, entry.id, result) ||
        !typed_ids.insert(entry.id).second) {
      if (result.diagnostics.empty()) {
        return fail(result, "duplicate " + std::string(default_type) + " id: " + entry.id);
      }
      return false;
    }
    out.push_back(std::move(entry));
  }
  return true;
}

bool readEntities(const Json& root,
                  std::unordered_set<std::string>& all_ids,
                  std::unordered_set<std::string>& entity_ids,
                  std::vector<SceneEntity>& out,
                  SceneLoadResult& result) {
  const auto it = root.find("entities");
  if (it == root.end()) {
    return true;
  }
  if (!it->is_array()) {
    return fail(result, "scene field 'entities' must be an array");
  }
  for (const Json& entity_json : *it) {
    if (!entity_json.is_object()) {
      return fail(result, "scene entity entries must be objects");
    }
    SceneEntity entity{};
    if (!readStringField(entity_json, "id", entity.id, result, true) ||
        !readStringField(entity_json, "name", entity.name, result) ||
        !readStringField(entity_json, "parent", entity.parent_id, result) ||
        !readStringField(entity_json, "parent_id", entity.parent_id, result) ||
        !readTransform(entity_json, entity.transform, result)) {
      return false;
    }
    const auto components_it = entity_json.find("components");
    if (components_it != entity_json.end()) {
      if (!components_it->is_object()) {
        return fail(result, "scene entity components must be an object");
      }
      entity.components = *components_it;
    }
    if (!registerId(all_ids, "entity", entity.id, result) ||
        !entity_ids.insert(entity.id).second) {
      if (result.diagnostics.empty()) {
        return fail(result, "duplicate entity id: " + entity.id);
      }
      return false;
    }
    out.push_back(std::move(entity));
  }
  return true;
}

bool readPrefabInstances(const Json& root,
                         std::unordered_set<std::string>& all_ids,
                         std::vector<ScenePrefabInstance>& out,
                         SceneLoadResult& result) {
  const auto it = root.find("prefab_instances");
  if (it == root.end()) {
    return true;
  }
  if (!it->is_array()) {
    return fail(result, "scene field 'prefab_instances' must be an array");
  }
  for (const Json& instance_json : *it) {
    if (!instance_json.is_object()) {
      return fail(result, "scene prefab instance entries must be objects");
    }
    ScenePrefabInstance instance{};
    if (!readStringField(instance_json, "id", instance.id, result, true) ||
        !readRelativePathAliasField(instance_json,
                                    {"prefab", "path"},
                                    instance.prefab_path,
                                    result,
                                    true) ||
        !readStringField(instance_json, "asset_package", instance.asset_package_id, result) ||
        !readStringField(instance_json, "asset_package_id", instance.asset_package_id, result) ||
        !readStringAliasField(instance_json,
                              {"parent", "parent_entity", "parent_entity_id"},
                              instance.parent_entity_id,
                              result) ||
        !readTransform(instance_json, instance.transform, result)) {
      return false;
    }
    const auto variables_it = instance_json.find("variables");
    if (variables_it != instance_json.end()) {
      if (!variables_it->is_object()) {
        return fail(result, "scene prefab instance variables must be an object");
      }
      instance.variables = *variables_it;
    }
    const auto static_it = instance_json.find("static");
    if (static_it != instance_json.end() && !static_it->is_null()) {
      if (!static_it->is_object()) {
        return fail(result,
                    "scene prefab instance static membership must be an object");
      }
      components::StaticComponent membership{};
      if (!readBoolField(*static_it, "enabled", membership.enabled, result) ||
          !readBoolField(*static_it,
                         "include_descendants",
                         membership.include_descendants,
                         result) ||
          !readUint32Field(*static_it, "flags", membership.flags, result)) {
        return false;
      }
      if (!components::validStaticComponentFlags(membership.flags)) {
        return fail(result,
                    "scene prefab instance static flags contain unsupported bits");
      }
      instance.static_component = membership;
    }
    if (!registerId(all_ids, "prefab instance", instance.id, result)) {
      return false;
    }
    out.push_back(std::move(instance));
  }
  return true;
}

bool readEnvironment(const Json& root,
                     std::unordered_set<std::string>& all_ids,
                     SceneDocument& document,
                     SceneLoadResult& result) {
  const auto it = root.find("environment");
  if (it == root.end() || it->is_null()) {
    return true;
  }
  if (!it->is_object()) {
    return fail(result, "scene field 'environment' must be an object");
  }
  SceneEnvironment environment{};
  if (!readStringField(*it, "id", environment.id, result) ||
      !readStringField(*it, "entity", environment.entity_id, result) ||
      !readStringField(*it, "entity_id", environment.entity_id, result) ||
      !readStringField(*it, "environment_map", environment.environment_map_asset_id, result) ||
      !readStringField(*it, "environment_map_asset_id", environment.environment_map_asset_id, result) ||
      !readRelativePathAliasField(*it,
                                  {"path", "environment_map_path"},
                                  environment.environment_map_path,
                                  result) ||
      !readFloatField(*it, "intensity", environment.component.intensity, result) ||
      !readBoolField(*it, "draw_skybox", environment.component.draw_skybox, result) ||
      !readBoolField(*it, "enabled", environment.component.enabled, result)) {
    return false;
  }
  environment.component.environment_map_asset_key = environment.environment_map_asset_id;
  if (!environment.id.empty() && !registerId(all_ids, "environment", environment.id, result)) {
    return false;
  }
  document.environment = std::move(environment);
  return true;
}

bool readCameras(const Json& root,
                 std::unordered_set<std::string>& all_ids,
                 std::vector<SceneCamera>& out,
                 SceneLoadResult& result) {
  const auto it = root.find("cameras");
  if (it == root.end()) {
    return true;
  }
  if (!it->is_array()) {
    return fail(result, "scene field 'cameras' must be an array");
  }
  for (const Json& camera_json : *it) {
    if (!camera_json.is_object()) {
      return fail(result, "scene camera entries must be objects");
    }
    SceneCamera camera{};
    std::filesystem::path vertex_path;
    std::filesystem::path fragment_path;
    if (!readStringField(camera_json, "id", camera.id, result, true) ||
        !readStringAliasField(camera_json, {"entity", "entity_id"}, camera.entity_id, result, true) ||
        !readBoolField(camera_json, "perspective", camera.component.perspective, result) ||
        !readBoolField(camera_json, "render_shadows", camera.component.render_shadows, result) ||
        !readFloatField(camera_json, "fov_y_degrees", camera.component.fov_y_degrees, result) ||
        !readFloatField(camera_json, "near_clip", camera.component.near_clip, result) ||
        !readFloatField(camera_json, "far_clip", camera.component.far_clip, result) ||
        !readFloatField(camera_json, "ortho_left", camera.component.ortho_left, result) ||
        !readFloatField(camera_json, "ortho_right", camera.component.ortho_right, result) ||
        !readFloatField(camera_json, "ortho_top", camera.component.ortho_top, result) ||
        !readFloatField(camera_json, "ortho_bottom", camera.component.ortho_bottom, result) ||
        !readBoolField(camera_json, "primary", camera.component.is_primary, result) ||
        !readBoolField(camera_json, "is_primary", camera.component.is_primary, result) ||
        !readBoolField(camera_json,
                       "render_to_texture",
                       camera.component.render_to_texture,
                       result) ||
        !readStringField(camera_json,
                         "render_target_key",
                         camera.component.render_target_key,
                         result) ||
        !readStringField(camera_json,
                         "frame_graph_key",
                         camera.component.frame_graph_key,
                         result) ||
        !readRelativePathField(camera_json, "shader_override_vertex_path", vertex_path, result) ||
        !readRelativePathField(camera_json, "shader_override_fragment_path", fragment_path, result) ||
        !readCameraAntiAliasing(camera_json,
                                camera.component.anti_aliasing,
                                result) ||
        !readCameraShaderParams(camera_json,
                                camera.component.shader_user_params,
                                result)) {
      return false;
    }
    camera.component.shader_override_vertex_path = std::move(vertex_path);
    camera.component.shader_override_fragment_path = std::move(fragment_path);
    if (!registerId(all_ids, "camera", camera.id, result)) {
      return false;
    }
    out.push_back(std::move(camera));
  }
  return true;
}

bool readLightType(std::string_view value,
                   components::LightComponent::Type& out,
                   SceneLoadResult& result) {
  if (value == "directional") {
    out = components::LightComponent::Type::Directional;
    return true;
  }
  if (value == "point") {
    out = components::LightComponent::Type::Point;
    return true;
  }
  if (value == "spot") {
    out = components::LightComponent::Type::Spot;
    return true;
  }
  return fail(result, "scene light type must be directional, point, or spot");
}

bool readLightBakeMode(std::string_view value,
                       components::LightComponent::BakeMode& out,
                       SceneLoadResult& result) {
  if (value == "realtime") {
    out = components::LightComponent::BakeMode::Realtime;
    return true;
  }
  if (value == "mixed") {
    out = components::LightComponent::BakeMode::Mixed;
    return true;
  }
  if (value == "baked") {
    out = components::LightComponent::BakeMode::Baked;
    return true;
  }
  return fail(result,
              "scene light bake_mode must be realtime, mixed, or baked");
}

bool readLights(const Json& root,
                std::unordered_set<std::string>& all_ids,
                std::vector<SceneLight>& out,
                SceneLoadResult& result) {
  const auto it = root.find("lights");
  if (it == root.end()) {
    return true;
  }
  if (!it->is_array()) {
    return fail(result, "scene field 'lights' must be an array");
  }
  for (const Json& light_json : *it) {
    if (!light_json.is_object()) {
      return fail(result, "scene light entries must be objects");
    }
    SceneLight light{};
    std::string type;
    std::string bake_mode;
    if (!readStringField(light_json, "id", light.id, result, true) ||
        !readStringAliasField(light_json, {"entity", "entity_id"}, light.entity_id, result, true) ||
        !readStringField(light_json, "type", type, result) ||
        !readStringField(light_json, "bake_mode", bake_mode, result) ||
        !readColorField(light_json, "color", light.component.color, result) ||
        !readFloatField(light_json, "intensity", light.component.intensity, result) ||
        !readFloatField(light_json, "range", light.component.range, result) ||
        !readFloatField(light_json, "inner_cone_degrees", light.component.inner_cone_degrees, result) ||
        !readFloatField(light_json, "outer_cone_degrees", light.component.outer_cone_degrees, result) ||
        !readBoolField(light_json, "casts_shadows", light.component.casts_shadows, result) ||
        !readFloatField(light_json, "shadow_extent", light.component.shadow_extent, result)) {
      return false;
    }
    if (!type.empty() && !readLightType(type, light.component.type, result)) {
      return false;
    }
    if (!bake_mode.empty() &&
        !readLightBakeMode(bake_mode, light.component.bake_mode, result)) {
      return false;
    }
    if (!registerId(all_ids, "light", light.id, result)) {
      return false;
    }
    out.push_back(std::move(light));
  }
  return true;
}

bool readStaticComponents(const Json& root,
                          std::unordered_set<std::string>& all_ids,
                          std::unordered_set<std::string>& static_ids,
                          std::vector<SceneStaticComponent>& out,
                          SceneLoadResult& result) {
  const auto it = root.find("static");
  if (it == root.end()) {
    return true;
  }
  if (!it->is_array()) {
    return fail(result, "scene field 'static' must be an array");
  }
  for (const Json& static_json : *it) {
    if (!static_json.is_object()) {
      return fail(result, "scene static entries must be objects");
    }
    SceneStaticComponent component{};
    if (!readStringField(static_json, "id", component.id, result, true) ||
        !readStringAliasField(static_json, {"entity", "entity_id"}, component.entity_id, result, true) ||
        !readStringField(static_json, "gltf_scene", component.gltf_scene_id, result) ||
        !readStringField(static_json, "gltf_scene_id", component.gltf_scene_id, result) ||
        !readStringField(static_json, "mesh", component.mesh_asset_key, result) ||
        !readStringField(static_json, "mesh_asset_key", component.mesh_asset_key, result) ||
        !readStringField(static_json, "material", component.material_asset_key, result) ||
        !readStringField(static_json, "material_asset_key", component.material_asset_key, result) ||
        !readBoolField(static_json, "transform", component.transform, result) ||
        !readBoolField(static_json, "static_transform", component.transform, result) ||
        !readBoolField(static_json, "render", component.render, result) ||
        !readBoolField(static_json, "static_render", component.render, result) ||
        !readBoolField(static_json, "lighting", component.lighting, result) ||
        !readBoolField(static_json, "static_lighting", component.lighting, result) ||
        !readBoolField(static_json, "collision", component.collision, result) ||
        !readBoolField(static_json, "static_collision", component.collision, result) ||
        !readBoolField(static_json, "navigation", component.navigation, result) ||
        !readBoolField(static_json, "static_navigation", component.navigation, result) ||
        !readBoolField(static_json, "casts_shadows", component.casts_shadows, result) ||
        !readBoolField(static_json,
                       "receives_baked_lighting",
                       component.receives_baked_lighting,
                       result)) {
      return false;
    }
    if (!registerId(all_ids, "static component", component.id, result) ||
        !static_ids.insert(component.id).second) {
      if (result.diagnostics.empty()) {
        return fail(result, "duplicate static component id: " + component.id);
      }
      return false;
    }
    out.push_back(std::move(component));
  }
  return true;
}

bool readStringArray(const Json& object,
                     std::string_view field,
                     std::vector<std::string>& out,
                     SceneLoadResult& result) {
  const auto it = object.find(std::string(field));
  if (it == object.end()) {
    return true;
  }
  if (!it->is_array()) {
    return fail(result, "scene field '" + std::string(field) + "' must be an array");
  }
  for (const Json& item : *it) {
    if (!item.is_string() || item.get<std::string>().empty()) {
      return fail(result, "scene field '" + std::string(field) +
                              "' entries must be non-empty strings");
    }
    out.push_back(item.get<std::string>());
  }
  return true;
}

bool readRelativePathArray(const Json& object,
                           std::string_view field,
                           std::vector<std::filesystem::path>& out,
                           SceneLoadResult& result) {
  const auto it = object.find(std::string(field));
  if (it == object.end()) {
    return true;
  }
  if (!it->is_array()) {
    return fail(result, "scene field '" + std::string(field) + "' must be an array");
  }
  for (const Json& item : *it) {
    if (!item.is_string() || item.get<std::string>().empty()) {
      return fail(result, "scene field '" + std::string(field) +
                              "' entries must be non-empty path strings");
    }
    const std::string value = item.get<std::string>();
    if (!isPortableRelativePath(value)) {
      return fail(result, "scene path field '" + std::string(field) +
                              "' must contain relative portable paths without '..': " +
                              value);
    }
    out.emplace_back(value);
  }
  return true;
}

bool readBakedLighting(const Json& object,
                       BakedLightingComponent& out,
                       SceneLoadResult& result) {
  if (!readStringField(object, "entity", out.entity_id, result) ||
      !readStringField(object, "entity_id", out.entity_id, result) ||
      !readStringField(object, "lightmap_asset_key", out.lightmap_asset_key, result) ||
      !readRelativePathField(object, "lightmap_path", out.lightmap_path, result) ||
      !readFloatField(object, "intensity", out.intensity, result) ||
      !readBoolField(object, "enabled", out.enabled, result)) {
    return false;
  }
  return true;
}

bool readLightmapBakeSettings(const Json& object,
                              SceneLightmapBakeSettings& out,
                              SceneLoadResult& result) {
  return readBoolField(object, "enabled", out.enabled, result) &&
         readBoolField(object, "generate_uv1", out.generate_uv1, result) &&
         readFloatField(object, "texels_per_unit", out.texels_per_unit, result) &&
         readUint32Field(object, "max_atlas_size", out.max_atlas_size, result) &&
         readUint32Field(object, "padding", out.padding, result) &&
         readUint32Field(object, "dilation", out.dilation, result) &&
         readUint32Field(object, "sky_samples", out.sky_samples, result) &&
         readFloatField(object, "ao_max_distance", out.ao_max_distance, result) &&
         readBoolField(object, "directional", out.directional, result);
}

bool readNavigationBakeSettings(const Json& object,
                                SceneNavigationBakeSettings& out,
                                SceneLoadResult& result) {
  return readBoolField(object, "enabled", out.enabled, result);
}

bool readBakes(const Json& root,
               std::unordered_set<std::string>& all_ids,
               std::vector<SceneBakeDesc>& out,
               SceneLoadResult& result) {
  const auto it = root.find("bakes");
  if (it == root.end()) {
    return true;
  }
  if (!it->is_array()) {
    return fail(result, "scene field 'bakes' must be an array");
  }
  for (const Json& bake_json : *it) {
    if (!bake_json.is_object()) {
      return fail(result, "scene bake entries must be objects");
    }
    SceneBakeDesc bake{};
    std::filesystem::path nav_cache_path;
    if (!readStringField(bake_json, "id", bake.id, result, true) ||
        !readRelativePathField(bake_json, "path", bake.path, result) ||
        !readBoolField(bake_json, "enabled", bake.enabled, result) ||
        !readBoolField(bake_json,
                       "load_at_runtime",
                       bake.load_at_runtime,
                       result) ||
        !readStringArray(bake_json, "static", bake.static_component_ids, result) ||
        !readStringArray(bake_json, "static_components", bake.static_component_ids, result) ||
        !readRelativePathField(bake_json,
                               "nav_cache_path",
                               nav_cache_path,
                               result) ||
        !readRelativePathArray(bake_json, "nav_cache", bake.nav_cache_paths, result) ||
        !readRelativePathArray(bake_json, "nav_caches", bake.nav_cache_paths, result)) {
      return false;
    }
    const auto settings_lighting_it = bake_json.find("lighting");
    if (settings_lighting_it != bake_json.end()) {
      if (!settings_lighting_it->is_object()) {
        return fail(result, "scene bake lighting settings must be an object");
      }
      if (!readLightmapBakeSettings(*settings_lighting_it,
                                    bake.lighting,
                                    result)) {
        return false;
      }
    }
    const auto settings_navigation_it = bake_json.find("navigation");
    if (settings_navigation_it != bake_json.end()) {
      if (!settings_navigation_it->is_object()) {
        return fail(result,
                    "scene bake navigation settings must be an object");
      }
      if (!readNavigationBakeSettings(*settings_navigation_it,
                                      bake.navigation,
                                      result)) {
        return false;
      }
    }
    if (!nav_cache_path.empty()) {
      bake.nav_cache_paths.push_back(std::move(nav_cache_path));
    }
    bake.baked_lighting.bake_id = bake.id;
    const auto lighting_it = bake_json.find("baked_lighting");
    if (lighting_it != bake_json.end()) {
      if (!lighting_it->is_object()) {
        return fail(result, "scene bake baked_lighting must be an object");
      }
      if (!readBakedLighting(*lighting_it, bake.baked_lighting, result)) {
        return false;
      }
      bake.baked_lighting.bake_id = bake.id;
    }
    if (!registerId(all_ids, "bake", bake.id, result)) {
      return false;
    }
    out.push_back(std::move(bake));
  }
  return true;
}

bool validateReferences(const SceneDocument& document,
                        const std::unordered_set<std::string>& asset_package_ids,
                        const std::unordered_set<std::string>& gltf_scene_ids,
                        const std::unordered_set<std::string>& entity_ids,
                        const std::unordered_set<std::string>& static_ids,
                        SceneLoadResult& result) {
  for (const SceneAssetRef& asset : document.gltf_scenes) {
    if (!requireRef(asset_package_ids, "asset_package", asset.asset_package_id, result)) {
      return false;
    }
  }
  for (const SceneEntity& entity : document.entities) {
    if (!requireRef(entity_ids, "entity", entity.parent_id, result)) {
      return false;
    }
  }
  for (const ScenePrefabInstance& instance : document.prefab_instances) {
    if (!requireRef(asset_package_ids, "asset_package", instance.asset_package_id, result) ||
        !requireRef(entity_ids, "entity", instance.parent_entity_id, result)) {
      return false;
    }
  }
  if (document.environment &&
      !requireRef(entity_ids, "entity", document.environment->entity_id, result)) {
    return false;
  }
  for (const SceneCamera& camera : document.cameras) {
    if (!requireRef(entity_ids, "entity", camera.entity_id, result)) {
      return false;
    }
  }
  for (const SceneLight& light : document.lights) {
    if (!requireRef(entity_ids, "entity", light.entity_id, result)) {
      return false;
    }
  }
  for (const SceneStaticComponent& component : document.static_components) {
    if (!requireRef(entity_ids, "entity", component.entity_id, result) ||
        !requireRef(gltf_scene_ids, "gltf_scene", component.gltf_scene_id, result)) {
      return false;
    }
  }
  for (const SceneBakeDesc& bake : document.bakes) {
    for (const std::string& static_id : bake.static_component_ids) {
      if (!requireRef(static_ids, "static", static_id, result)) {
        return false;
      }
    }
    if (!requireRef(entity_ids, "entity", bake.baked_lighting.entity_id, result)) {
      return false;
    }
  }
  return true;
}

}  // namespace

namespace detail {

bool parseSceneDocument(const Json& root,
                        const std::filesystem::path& source_path,
                        SceneLoadResult& result) {
  const auto version_it = root.find("version");
  if (version_it == root.end() ||
      (!version_it->is_number_integer() && !version_it->is_number_unsigned())) {
    return fail(result, "scene document version must be integer 1");
  }
  bool version_matches = false;
  std::string version_text;
  if (version_it->is_number_unsigned()) {
    const uint64_t version = version_it->get<uint64_t>();
    version_matches = version == static_cast<uint64_t>(kSceneDocumentVersion);
    version_text = std::to_string(version);
  } else {
    const int64_t version = version_it->get<int64_t>();
    version_matches = version == static_cast<int64_t>(kSceneDocumentVersion);
    version_text = std::to_string(version);
  }
  if (!version_matches) {
    return fail(result, "unsupported scene document version: " + version_text);
  }

  SceneDocument document{};
  document.version = kSceneDocumentVersion;
  document.source_path = source_path;

  std::unordered_set<std::string> all_ids;
  std::unordered_set<std::string> asset_package_ids;
  std::unordered_set<std::string> gltf_scene_ids;
  std::unordered_set<std::string> entity_ids;
  std::unordered_set<std::string> static_ids;

  if (!readStringField(root, "name", document.name, result) ||
      !readAssetArray(root,
                      "asset_packages",
                      "asset_package",
                      all_ids,
                      asset_package_ids,
                      document.asset_packages,
                      result) ||
      !readAssetArray(root,
                      "gltf_scenes",
                      "gltf_scene",
                      all_ids,
                      gltf_scene_ids,
                      document.gltf_scenes,
                      result) ||
      !readPrefabInstances(root, all_ids, document.prefab_instances, result) ||
      !readEntities(root, all_ids, entity_ids, document.entities, result) ||
      !readEnvironment(root, all_ids, document, result) ||
      !readCameras(root, all_ids, document.cameras, result) ||
      !readLights(root, all_ids, document.lights, result) ||
      !readStaticComponents(root, all_ids, static_ids, document.static_components, result) ||
      !readBakes(root, all_ids, document.bakes, result) ||
      !validateReferences(document,
                          asset_package_ids,
                          gltf_scene_ids,
                          entity_ids,
                          static_ids,
                          result)) {
    return false;
  }

  SceneValidationResult validation = validateSceneDocument(document);
  if (!validation.success()) {
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(validation.diagnostics.begin()),
                              std::make_move_iterator(validation.diagnostics.end()));
    result.document.reset();
    return false;
  }

  result.document = std::move(document);
  return true;
}

}  // namespace detail

}  // namespace karma::scenes
