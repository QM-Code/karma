#pragma once

#include <cstdint>
#include <vector>

#include "karma/world/components/collider.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"
#include "karma/core/math/types.h"

namespace karma::components {

/// \ingroup karma_components
/// Solid contact point produced by the physics backend.
struct ContactEvent {
  ecs::Entity other{};
  ColliderShapeType other_shape = ColliderShapeType::Box;
  math::Vec3 point{};
  math::Vec3 normal{0.0f, 1.0f, 0.0f};
};

/// \ingroup karma_components
/// Opt-in listener for physics contact enter/stay/exit events.
struct ContactListenerComponent : ecs::ComponentTag {
  bool enabled = true;
  bool emit_stay = false;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
};

/// \ingroup karma_components
/// Per-frame solid-contact buffers written by `PhysicsSystem`.
struct ContactEventsComponent : ecs::ComponentTag {
  std::vector<ContactEvent> entered;
  std::vector<ContactEvent> stayed;
  std::vector<ContactEvent> exited;
  std::vector<ContactEvent> active;

  /// Clears one-frame event buffers.
  void clearTransient() {
    entered.clear();
    stayed.clear();
    exited.clear();
    active.clear();
  }
};

}  // namespace karma::components
