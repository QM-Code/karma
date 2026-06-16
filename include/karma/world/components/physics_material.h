#pragma once

#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Physics material authoring data consumed by `PhysicsSystem`.
struct PhysicsMaterialComponent : ecs::ComponentTag {
  float friction = 0.2f;
  float restitution = 0.0f;
};

}  // namespace karma::components
