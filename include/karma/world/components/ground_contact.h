#pragma once

#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/component.h"
#include "karma/core/math/types.h"

namespace karma::components {

/// \ingroup karma_components
/// Ground/support state written by physics/character-controller integration.
///
/// `entered` and `exited` are one-frame flags. `support_entity`, point, and
/// normal describe the current support surface when available.
struct GroundContactComponent : ecs::ComponentTag {
  bool grounded = false;
  bool entered = false;
  bool exited = false;
  bool has_support = false;
  ecs::Entity support_entity{};
  math::Vec3 point{};
  math::Vec3 normal{0.0f, 1.0f, 0.0f};

  /// Clears one-frame enter/exit flags.
  void clearTransient() {
    entered = false;
    exited = false;
  }
};

}  // namespace karma::components
