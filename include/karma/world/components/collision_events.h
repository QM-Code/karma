#pragma once

#include <cstdint>
#include <vector>

#include "karma/world/components/collider.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"

namespace karma::components {

/// \ingroup karma_components
/// Which collider classes a collision listener records.
enum class CollisionListenMode : uint8_t {
  All = 0,
  TriggersOnly = 1,
  SolidsOnly = 2,
};

/// \ingroup karma_components
/// Overlap/trigger contact against another entity.
struct CollisionContact {
  ecs::Entity other{};
  ColliderShapeType other_shape = ColliderShapeType::Box;
  bool other_is_trigger = false;
};

/// \ingroup karma_components
/// Opt-in listener for ECS overlap and trigger events.
struct CollisionListenerComponent : ecs::ComponentTag {
  bool enabled = true;
  CollisionListenMode mode = CollisionListenMode::All;
  bool emit_stay = false;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
};

/// \ingroup karma_components
/// Per-frame overlap event buffers written by `CollisionEventSystem`.
struct CollisionEventsComponent : ecs::ComponentTag {
  std::vector<CollisionContact> entered;
  std::vector<CollisionContact> stayed;
  std::vector<CollisionContact> exited;
  std::vector<CollisionContact> active;

  /// Clears one-frame event buffers.
  void clearTransient() {
    entered.clear();
    stayed.clear();
    exited.clear();
    active.clear();
  }

  /// Returns true when no collision contacts are recorded.
  bool empty() const {
    return entered.empty() && stayed.empty() && exited.empty() && active.empty();
  }
};

}  // namespace karma::components
