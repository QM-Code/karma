#pragma once

#include <string_view>

#include "karma/content/prefabs/prefab_registry.h"

namespace karma::demo {

inline constexpr std::string_view kEnergyOrbPrefabKey = "energy_orb";

bool registerEnergyOrbPrefabPackage(prefabs::PrefabRegistry& registry);

}  // namespace karma::demo
