#pragma once

#include <string_view>

#include "karma/prefabs/effect_prefab_registry.h"

namespace karma::demo {

inline constexpr std::string_view kEnergyOrbPrefabKey = "energy_orb";

bool registerEnergyOrbPrefabPackage(prefabs::EffectPrefabRegistry& registry);

}  // namespace karma::demo
