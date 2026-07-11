#include "karma/prefabs.h"

#include <array>
#include <string_view>
#include <utility>

namespace karma::prefabs {

LegacyRenderComponentMigrationResult migrateLegacyRenderComponentsJson(
    nlohmann::json& components) {
  using Json = nlohmann::json;
  LegacyRenderComponentMigrationResult result{};
  if (!components.is_object()) {
    result.diagnostics.push_back("components value must be a JSON object");
    return result;
  }

  Json migrated = components;
  auto instanced_it = migrated.find("InstancedMeshComponent");
  if (instanced_it != migrated.end() && !instanced_it->is_object()) {
    result.diagnostics.push_back(
        "InstancedMeshComponent payload must be a JSON object");
    return result;
  }
  auto foliage_it = migrated.find("FoliageComponent");
  if (foliage_it != migrated.end() && !foliage_it->is_object()) {
    result.diagnostics.push_back(
        "FoliageComponent payload must be a JSON object");
    return result;
  }

  static constexpr std::array<std::string_view, 5> kInstanceSetFields{
      "gpu_layout",
      "instances",
      "planar_instances",
      "instance_revision",
      "dynamic",
  };
  bool has_legacy_instance_set = false;
  if (instanced_it != migrated.end()) {
    for (const std::string_view field : kInstanceSetFields) {
      has_legacy_instance_set =
          has_legacy_instance_set || instanced_it->contains(field);
    }
  }
  if (has_legacy_instance_set && migrated.contains("InstanceSetComponent")) {
    result.diagnostics.push_back(
        "legacy InstancedMeshComponent instance fields conflict with sibling "
        "InstanceSetComponent");
    return result;
  }

  const bool has_instanced_lods =
      instanced_it != migrated.end() && instanced_it->contains("lods");
  const bool has_foliage_lods =
      foliage_it != migrated.end() && foliage_it->contains("lods");
  if (has_instanced_lods && has_foliage_lods) {
    result.diagnostics.push_back(
        "legacy InstancedMeshComponent and FoliageComponent LOD fields "
        "conflict on the same entity");
    return result;
  }
  if ((has_instanced_lods || has_foliage_lods) &&
      migrated.contains("LODComponent")) {
    result.diagnostics.push_back(
        "legacy embedded LOD fields conflict with sibling LODComponent");
    return result;
  }
  if (has_instanced_lods && !(*instanced_it)["lods"].is_array()) {
    result.diagnostics.push_back("legacy lods field must be a JSON array");
    return result;
  }
  if (has_foliage_lods && !(*foliage_it)["lods"].is_array()) {
    result.diagnostics.push_back("legacy lods field must be a JSON array");
    return result;
  }

  if (has_legacy_instance_set) {
    Json instance_set{
        {"gpu_layout", "matrix4x4_params"},
        {"instances", Json::array()},
        {"planar_instances", Json::array()},
        {"instance_revision", 0u},
        {"dynamic", false},
    };
    for (const std::string_view field : kInstanceSetFields) {
      const auto field_it = instanced_it->find(field);
      if (field_it == instanced_it->end()) continue;
      instance_set[std::string(field)] = *field_it;
      instanced_it->erase(field_it);
    }
    migrated["InstanceSetComponent"] = std::move(instance_set);
    result.changed = true;
  }

  if (has_instanced_lods || has_foliage_lods) {
    Json& source = has_instanced_lods ? *instanced_it : *foliage_it;
    const auto lods_it = source.find("lods");
    migrated["LODComponent"] = Json{{"levels", *lods_it}};
    source.erase(lods_it);
    result.changed = true;
  }

  components = std::move(migrated);
  return result;
}

LegacyRenderComponentMigrationResult migrateLegacyPrefabJson(
    nlohmann::json& prefab_json) {
  using Json = nlohmann::json;
  LegacyRenderComponentMigrationResult result{};
  if (!prefab_json.is_object()) {
    result.diagnostics.push_back("prefab document must be a JSON object");
    return result;
  }
  const auto nodes_it = prefab_json.find("nodes");
  if (nodes_it == prefab_json.end() || !nodes_it->is_array()) {
    result.diagnostics.push_back("prefab document is missing array 'nodes'");
    return result;
  }

  Json migrated = prefab_json;
  Json& nodes = migrated["nodes"];
  for (size_t index = 0u; index < nodes.size(); ++index) {
    Json& node = nodes[index];
    if (!node.is_object()) {
      result.diagnostics.push_back("prefab node " + std::to_string(index) +
                                   " must be a JSON object");
      return result;
    }
    const auto components_it = node.find("components");
    if (components_it == node.end() || !components_it->is_object()) {
      result.diagnostics.push_back("prefab node " + std::to_string(index) +
                                   " is missing object 'components'");
      return result;
    }
    LegacyRenderComponentMigrationResult node_result =
        migrateLegacyRenderComponentsJson(*components_it);
    if (!node_result.success()) {
      for (std::string& diagnostic : node_result.diagnostics) {
        result.diagnostics.push_back("prefab node " +
                                     std::to_string(index) + ": " +
                                     std::move(diagnostic));
      }
      return result;
    }
    result.changed = result.changed || node_result.changed;
  }
  prefab_json = std::move(migrated);
  return result;
}

}  // namespace karma::prefabs
