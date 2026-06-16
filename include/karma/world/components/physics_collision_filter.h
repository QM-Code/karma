#pragma once

#include <cstdint>

#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Physics collision filtering consumed by `PhysicsSystem`.
///
/// `layers` are the collision categories this body belongs to. `collides_with`
/// is the set of categories this body accepts contacts from. A pair collides
/// only when both masks accept the other body's layers.
struct PhysicsCollisionFilterComponent : ecs::ComponentTag {
  uint32_t layers = 1u;
  uint32_t collides_with = 0xFFFFFFFFu;
};

}  // namespace karma::components
