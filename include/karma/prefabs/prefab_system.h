#pragma once

#include "karma/ecs/world.h"

namespace karma::prefabs {

class PrefabSystem {
 public:
  void update(ecs::World& world, float dt, float interpolation_alpha);
};

}  // namespace karma::prefabs
