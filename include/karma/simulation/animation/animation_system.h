#pragma once

#include <string_view>

#include "karma/world/ecs/world.h"
#include "karma/world/scene/scene.h"

namespace karma::animation {

/// \ingroup karma_animation
/// Samples animation components and writes local transforms.
///
/// The system consumes `AnimationPlayerComponent` and `AnimatorComponent`.
/// Scene hierarchy composition later writes final world transforms.
class AnimationSystem {
 public:
  std::string_view name() const { return "AnimationSystem"; }
  void update(ecs::World& world, scene::Scene& scene, float dt);
};

}  // namespace karma::animation
