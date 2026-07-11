#include "component_serializer_rendering.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "component_serializer_utilities.h"
#include "karma/components.h"
#include "karma/foliage.h"

namespace karma::prefabs {
namespace {

using Json = nlohmann::json;
using component_serializer_detail::isPortableRelativePath;
using component_serializer_detail::readBool;
using component_serializer_detail::readEntityReference;
using component_serializer_detail::readFloat;
using component_serializer_detail::readFloatValue;
using component_serializer_detail::readQuat;
using component_serializer_detail::readString;
using component_serializer_detail::readUint32;
using component_serializer_detail::readUint64;
using component_serializer_detail::readVec3;
using component_serializer_detail::registerComponent;
using component_serializer_detail::registerContextualComponent;
using component_serializer_detail::serializeEntityReference;
using component_serializer_detail::toJson;

const char* instanceLayoutName(rendering::InstanceGpuLayout layout) {
  switch (layout) {
    case rendering::InstanceGpuLayout::Matrix4x4Params:
      return "matrix4x4_params";
    case rendering::InstanceGpuLayout::PositionYawScaleParams:
      return "position_yaw_scale_params";
  }
  return "matrix4x4_params";
}

bool readInstanceLayout(const Json& object,
                        std::string_view key,
                        rendering::InstanceGpuLayout& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "matrix4x4_params") {
    out = rendering::InstanceGpuLayout::Matrix4x4Params;
    return true;
  }
  if (value == "position_yaw_scale_params") {
    out = rendering::InstanceGpuLayout::PositionYawScaleParams;
    return true;
  }
  return false;
}

const char* lodRenderModeName(rendering::LodRenderMode mode) {
  switch (mode) {
    case rendering::LodRenderMode::Mesh:
      return "mesh";
    case rendering::LodRenderMode::UprightBillboard:
      return "upright_billboard";
  }
  return "mesh";
}

bool readLodRenderMode(const Json& object,
                       std::string_view key,
                       rendering::LodRenderMode& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "mesh") {
    out = rendering::LodRenderMode::Mesh;
    return true;
  }
  if (value == "upright_billboard") {
    out = rendering::LodRenderMode::UprightBillboard;
    return true;
  }
  return false;
}

Json serializeMesh(const components::MeshComponent& component) {
  Json materials = Json::array();
  for (const auto& binding : component.materials) {
    materials.push_back(Json{
        {"slot", binding.slot},
        {"material_key", binding.material_key},
    });
  }
  return Json{
      {"mesh_asset_key", component.mesh_asset_key},
      {"materials", std::move(materials)},
      {"visible", component.visible},
      {"shadow_visible", component.shadow_visible},
  };
}

std::optional<components::MeshComponent> deserializeMesh(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  if (json.contains("mesh_key") || json.contains("material_key") ||
      json.contains("texture_key") || json.contains("mesh_id") ||
      json.contains("material_id") || json.contains("owns_mesh_id") ||
      json.contains("owns_material_id")) {
    return std::nullopt;
  }
  components::MeshComponent component{};
  if (!readString(json, "mesh_asset_key", component.mesh_asset_key) ||
      !readBool(json, "visible", component.visible) ||
      !readBool(json, "shadow_visible", component.shadow_visible)) {
    return std::nullopt;
  }

  if (const auto materials_it = json.find("materials");
      materials_it != json.end()) {
    if (!materials_it->is_array()) {
      return std::nullopt;
    }
    component.materials.reserve(materials_it->size());
    for (const Json& material_json : *materials_it) {
      if (!material_json.is_object()) {
        return std::nullopt;
      }
      components::MeshMaterialAssignment binding{};
      if (!readUint32(material_json, "slot", binding.slot) ||
          !readString(material_json, "material_key", binding.material_key)) {
        return std::nullopt;
      }
      if (!binding.material_key.empty()) {
        component.materials.push_back(std::move(binding));
      }
    }
  }
  return component;
}

Json serializeMaterialAssignments(
    const std::vector<components::MeshMaterialAssignment>& materials) {
  Json out = Json::array();
  for (const auto& binding : materials) {
    out.push_back(Json{
        {"slot", binding.slot},
        {"material_key", binding.material_key},
    });
  }
  return out;
}

bool readMaterialAssignments(
    const Json& object,
    std::string_view key,
    std::vector<components::MeshMaterialAssignment>& out) {
  const auto materials_it = object.find(key);
  if (materials_it == object.end()) {
    return true;
  }
  if (!materials_it->is_array()) {
    return false;
  }
  std::vector<components::MeshMaterialAssignment> materials;
  materials.reserve(materials_it->size());
  for (const Json& material_json : *materials_it) {
    if (!material_json.is_object()) {
      return false;
    }
    components::MeshMaterialAssignment binding{};
    if (!readUint32(material_json, "slot", binding.slot) ||
        !readString(material_json, "material_key", binding.material_key) ||
        binding.material_key.empty()) {
      return false;
    }
    materials.push_back(std::move(binding));
  }
  out = std::move(materials);
  return true;
}

Json serializeInstanceSet(const components::InstanceSetComponent& component) {
  if (component.gpu_layout != rendering::InstanceGpuLayout::Matrix4x4Params &&
      component.gpu_layout !=
          rendering::InstanceGpuLayout::PositionYawScaleParams) {
    throw std::runtime_error("InstanceSetComponent has an invalid GPU layout");
  }
  for (const components::MeshInstance& instance : component.instances) {
    if (!math::isFinite(instance.position) ||
        !math::isFinite(instance.rotation) ||
        !math::isFinite(instance.scale) ||
        math::lengthSquared(instance.rotation) <= 1.0e-12f ||
        !std::all_of(instance.params.begin(),
                     instance.params.end(),
                     [](float value) { return std::isfinite(value); })) {
      throw std::runtime_error("InstanceSetComponent has an invalid instance");
    }
  }
  for (const components::PlanarMeshInstance& instance :
       component.planar_instances) {
    if (!math::isFinite(instance.position) ||
        !std::isfinite(instance.yaw_radians) ||
        !math::isFinite(instance.scale) ||
        !std::all_of(instance.params.begin(),
                     instance.params.end(),
                     [](float value) { return std::isfinite(value); })) {
      throw std::runtime_error(
          "InstanceSetComponent has an invalid planar instance");
    }
  }
  Json instances = Json::array();
  for (const auto& instance : component.instances) {
    instances.push_back(Json{
        {"position", toJson(instance.position)},
        {"rotation", toJson(instance.rotation)},
        {"scale", toJson(instance.scale)},
        {"params", Json::array({instance.params[0],
                                instance.params[1],
                                instance.params[2],
                                instance.params[3]})},
    });
  }

  Json planar_instances = Json::array();
  for (const auto& instance : component.planar_instances) {
    planar_instances.push_back(Json{
        {"position", toJson(instance.position)},
        {"yaw_radians", instance.yaw_radians},
        {"scale", toJson(instance.scale)},
        {"params", Json::array({instance.params[0],
                                instance.params[1],
                                instance.params[2],
                                instance.params[3]})},
    });
  }

  return Json{
      {"gpu_layout", instanceLayoutName(component.gpu_layout)},
      {"instances", std::move(instances)},
      {"planar_instances", std::move(planar_instances)},
      {"instance_revision", component.instance_revision},
      {"dynamic", component.dynamic},
  };
}

std::optional<components::InstanceSetComponent> deserializeInstanceSet(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::InstanceSetComponent component{};
  if (!readInstanceLayout(json, "gpu_layout", component.gpu_layout) ||
      !readUint64(json, "instance_revision", component.instance_revision) ||
      !readBool(json, "dynamic", component.dynamic)) {
    return std::nullopt;
  }

  if (const auto instances_it = json.find("instances");
      instances_it != json.end()) {
    if (!instances_it->is_array()) {
      return std::nullopt;
    }
    component.instances.reserve(instances_it->size());
    for (const Json& instance_json : *instances_it) {
      if (!instance_json.is_object()) {
        return std::nullopt;
      }
      components::MeshInstance instance{};
      if (!readVec3(instance_json, "position", instance.position) ||
          !readQuat(instance_json, "rotation", instance.rotation) ||
          !readVec3(instance_json, "scale", instance.scale) ||
          math::lengthSquared(instance.rotation) <= 1.0e-12f) {
        return std::nullopt;
      }
      if (const auto params_it = instance_json.find("params");
          params_it != instance_json.end()) {
        if (!params_it->is_array() ||
            params_it->size() != instance.params.size()) {
          return std::nullopt;
        }
        for (size_t index = 0; index < instance.params.size(); ++index) {
          if (!readFloatValue((*params_it)[index], instance.params[index])) {
            return std::nullopt;
          }
        }
      }
      component.instances.push_back(instance);
    }
  }

  if (const auto instances_it = json.find("planar_instances");
      instances_it != json.end()) {
    if (!instances_it->is_array()) {
      return std::nullopt;
    }
    component.planar_instances.reserve(instances_it->size());
    for (const Json& instance_json : *instances_it) {
      if (!instance_json.is_object()) {
        return std::nullopt;
      }
      components::PlanarMeshInstance instance{};
      if (!readVec3(instance_json, "position", instance.position) ||
          !readFloat(instance_json, "yaw_radians", instance.yaw_radians) ||
          !readVec3(instance_json, "scale", instance.scale)) {
        return std::nullopt;
      }
      if (const auto params_it = instance_json.find("params");
          params_it != instance_json.end()) {
        if (!params_it->is_array() ||
            params_it->size() != instance.params.size()) {
          return std::nullopt;
        }
        for (size_t index = 0; index < instance.params.size(); ++index) {
          if (!readFloatValue((*params_it)[index], instance.params[index])) {
            return std::nullopt;
          }
        }
      }
      component.planar_instances.push_back(instance);
    }
  }
  return component;
}

Json serializeLod(const components::LodComponent& component) {
  std::string validation_error;
  if (!components::validateLodComponent(component, &validation_error)) {
    throw std::runtime_error(validation_error);
  }
  Json levels = Json::array();
  for (const components::LodLevel& level : component.levels) {
    levels.push_back(Json{
        {"start_distance", level.start_distance},
        {"mesh_asset_key", level.mesh_asset_key},
        {"materials", serializeMaterialAssignments(level.materials)},
        {"render_mode", lodRenderModeName(level.render_mode)},
        {"shadow_visible", level.shadow_visible},
    });
  }
  return Json{{"levels", std::move(levels)}};
}

std::optional<components::LodComponent> deserializeLod(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::LodComponent component{};
  if (const auto levels_it = json.find("levels"); levels_it != json.end()) {
    if (!levels_it->is_array() ||
        levels_it->size() > components::kMaxLodLevels) {
      return std::nullopt;
    }
    component.levels.reserve(levels_it->size());
    for (const Json& level_json : *levels_it) {
      if (!level_json.is_object()) {
        return std::nullopt;
      }
      components::LodLevel level{};
      if (!readFloat(level_json, "start_distance", level.start_distance) ||
          !readString(level_json, "mesh_asset_key", level.mesh_asset_key) ||
          !readMaterialAssignments(level_json, "materials", level.materials) ||
          !readLodRenderMode(level_json, "render_mode", level.render_mode) ||
          !readBool(level_json, "shadow_visible", level.shadow_visible)) {
        return std::nullopt;
      }
      component.levels.push_back(std::move(level));
    }
  }
  std::string validation_error;
  if (!components::validateLodComponent(component, &validation_error)) {
    return std::nullopt;
  }
  return component;
}

Json serializeInstancedMesh(
    const components::InstancedMeshComponent& component,
    const ComponentSerializationContext& context) {
  if (!math::isFinite(component.local_position) ||
      !math::isFinite(component.local_rotation) ||
      !math::isFinite(component.local_scale) ||
      math::lengthSquared(component.local_rotation) <= 1.0e-12f) {
    throw std::runtime_error(
        "InstancedMeshComponent has an invalid local transform");
  }
  Json materials = Json::array();
  for (const auto& binding : component.materials) {
    materials.push_back(Json{
        {"slot", binding.slot},
        {"material_key", binding.material_key},
    });
  }

  Json out = Json{
      {"mesh_asset_key", component.mesh_asset_key},
      {"materials", std::move(materials)},
      {"local_position", toJson(component.local_position)},
      {"local_rotation", toJson(component.local_rotation)},
      {"local_scale", toJson(component.local_scale)},
      {"visible", component.visible},
      {"shadow_visible", component.shadow_visible},
  };
  if (component.instance_source.isValid()) {
    const Json reference =
        serializeEntityReference(component.instance_source, context);
    if (reference.is_null()) {
      throw std::runtime_error(
          "InstancedMeshComponent instance_source requires a contextual "
          "entity serializer");
    }
    out["instance_source"] = reference;
  }
  return out;
}

std::optional<components::InstancedMeshComponent> deserializeInstancedMesh(
    const Json& json,
    const ComponentSerializationContext& context) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  if (json.contains("lods") || json.contains("gpu_layout") ||
      json.contains("instances") || json.contains("planar_instances") ||
      json.contains("instance_revision") || json.contains("dynamic")) {
    return std::nullopt;
  }
  components::InstancedMeshComponent component{};
  if (!readString(json, "mesh_asset_key", component.mesh_asset_key) ||
      !readEntityReference(
          json, "instance_source", component.instance_source, context) ||
      !readVec3(json, "local_position", component.local_position) ||
      !readQuat(json, "local_rotation", component.local_rotation) ||
      !readVec3(json, "local_scale", component.local_scale) ||
      !readBool(json, "visible", component.visible) ||
      !readBool(json, "shadow_visible", component.shadow_visible) ||
      math::lengthSquared(component.local_rotation) <= 1.0e-12f) {
    return std::nullopt;
  }
  if (!readMaterialAssignments(json, "materials", component.materials)) {
    return std::nullopt;
  }
  return component;
}

Json serializeFoliage(const components::FoliageComponent& component) {
  std::string validation_error;
  if (!foliage::validateFoliageComponent(component, &validation_error)) {
    throw std::runtime_error(validation_error);
  }
  Json out{
      {"sidecar_path", component.sidecar_path.generic_string()},
      {"chunk_size", component.chunk_size},
      {"view_distance", component.view_distance},
      {"max_resident_instances", component.max_resident_instances},
      {"source_revision", component.source_revision},
      {"visible", component.visible},
      {"shadow_visible", component.shadow_visible},
  };
  if (!component.prefab_path.empty()) {
    out["prefab_path"] = component.prefab_path.generic_string();
    out["prefab_variables"] = component.prefab_variables;
  } else {
    out["mesh_asset_key"] = component.mesh_asset_key;
    out["materials"] = serializeMaterialAssignments(component.materials);
  }
  return out;
}

std::optional<components::FoliageComponent> deserializeFoliage(
    const Json& json) {
  if (!json.is_object() || json.contains("lods")) {
    return std::nullopt;
  }

  components::FoliageComponent component{};
  std::string sidecar_path;
  std::string prefab_path;
  if (!readString(json, "sidecar_path", sidecar_path) ||
      !readString(json, "prefab_path", prefab_path) ||
      !readString(json, "mesh_asset_key", component.mesh_asset_key) ||
      !readFloat(json, "chunk_size", component.chunk_size) ||
      !readFloat(json, "view_distance", component.view_distance) ||
      !readUint32(
          json, "max_resident_instances", component.max_resident_instances) ||
      !readUint64(json, "source_revision", component.source_revision) ||
      !readBool(json, "visible", component.visible) ||
      !readBool(json, "shadow_visible", component.shadow_visible) ||
      !isPortableRelativePath(sidecar_path) ||
      (!prefab_path.empty() && !isPortableRelativePath(prefab_path))) {
    return std::nullopt;
  }
  component.sidecar_path = std::filesystem::path(std::move(sidecar_path));
  component.prefab_path = std::filesystem::path(std::move(prefab_path));

  if (const auto variables_it = json.find("prefab_variables");
      variables_it != json.end()) {
    if (!variables_it->is_object()) {
      return std::nullopt;
    }
    component.prefab_variables = *variables_it;
  }
  if (!readMaterialAssignments(json, "materials", component.materials)) {
    return std::nullopt;
  }

  std::string validation_error;
  if (!foliage::validateFoliageComponent(component, &validation_error)) {
    return std::nullopt;
  }
  return component;
}

}  // namespace

void registerRenderingAuthoringComponentSerializers(
    ComponentSerializerRegistry& registry) {
  registerComponent<components::MeshComponent>(
      registry, "MeshComponent", serializeMesh, deserializeMesh);
  registerComponent<components::InstanceSetComponent>(
      registry,
      "InstanceSetComponent",
      serializeInstanceSet,
      deserializeInstanceSet);
  registerContextualComponent<components::InstancedMeshComponent>(
      registry,
      "InstancedMeshComponent",
      serializeInstancedMesh,
      deserializeInstancedMesh);
  registerComponent<components::LodComponent>(
      registry, "LODComponent", serializeLod, deserializeLod);
  registerComponent<components::FoliageComponent>(
      registry, "FoliageComponent", serializeFoliage, deserializeFoliage);
}

}  // namespace karma::prefabs
