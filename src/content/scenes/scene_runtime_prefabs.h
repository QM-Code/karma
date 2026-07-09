#pragma once

#include "karma/scenes.h"

#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace karma::scenes::detail {

bool deserializeAuthoredComponents(world::World& world,
                                   world::Entity entity,
                                   const nlohmann::json& components,
                                   SceneInstantiateResult& result);

std::unordered_map<std::string, nlohmann::json> prefabVariables(
    const nlohmann::json& variables);

}  // namespace karma::scenes::detail
