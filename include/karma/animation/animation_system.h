#pragma once

#include <string_view>

#include "karma/ecs/world.h"
#include "karma/scene/scene.h"

namespace karma::animation {

class AnimationSystem {
 public:
  std::string_view name() const { return "AnimationSystem"; }
  void update(ecs::World& world, scene::Scene& scene, float dt);
};

}  // namespace karma::animation
