#include "scene_runtime_prefabs.h"

#include <exception>
#include <string>
#include <unordered_set>
#include <utility>

#include "karma/prefabs.h"

namespace karma::scenes::detail {

namespace {

bool appendDiagnostic(SceneInstantiateResult& result, std::string message) {
  result.diagnostics.push_back(std::move(message));
  return false;
}

}  // namespace

bool deserializeAuthoredComponents(world::World& world,
                                   world::Entity entity,
                                   const nlohmann::json& components,
                                   SceneInstantiateResult& result) {
  if (!components.is_object() || components.empty()) {
    return true;
  }

  prefabs::ensureBuiltinComponentSerializers();
  prefabs::ComponentSerializerRegistry& registry = prefabs::componentSerializerRegistry();
  std::unordered_set<std::string> consumed;
  consumed.reserve(components.size());

  for (const prefabs::ComponentSerializer& serializer : registry.serializers()) {
    const auto component_it = components.find(serializer.type_name);
    if (component_it == components.end()) {
      continue;
    }
    try {
      if (!serializer.deserialize(world, entity, *component_it)) {
        return appendDiagnostic(result,
                                "scene entity has invalid component payload: " +
                                    serializer.type_name);
      }
    } catch (const std::exception& e) {
      return appendDiagnostic(result,
                              "scene entity failed to add component '" +
                                  serializer.type_name + "': " + e.what());
    }
    consumed.insert(serializer.type_name);
  }

  for (auto it = components.begin(); it != components.end(); ++it) {
    const std::string type_name = it.key();
    if (consumed.find(type_name) == consumed.end()) {
      return appendDiagnostic(result,
                              "scene entity has unknown component: " + type_name);
    }
  }
  return true;
}

std::unordered_map<std::string, nlohmann::json> prefabVariables(
    const nlohmann::json& variables) {
  std::unordered_map<std::string, nlohmann::json> out;
  if (!variables.is_object()) {
    return out;
  }
  out.reserve(variables.size());
  for (auto it = variables.begin(); it != variables.end(); ++it) {
    out[it.key()] = *it;
  }
  return out;
}

}  // namespace karma::scenes::detail
