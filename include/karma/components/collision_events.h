#pragma once

#include <cstdint>
#include <vector>

#include "karma/ecs/collider_queries.h"
#include "karma/ecs/component.h"
#include "karma/ecs/entity.h"

namespace karma::components {

enum class CollisionListenMode : uint8_t {
  All = 0,
  TriggersOnly = 1,
  SolidsOnly = 2,
};

struct CollisionContact {
  ecs::Entity other{};
  ecs::queries::ColliderShape other_shape = ecs::queries::ColliderShape::Box;
  bool other_is_trigger = false;
};

struct CollisionListenerComponent : ecs::ComponentTag {
  bool enabled = true;
  CollisionListenMode mode = CollisionListenMode::All;
  bool emit_stay = false;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
};

struct CollisionEventsComponent : ecs::ComponentTag {
  std::vector<CollisionContact> entered;
  std::vector<CollisionContact> stayed;
  std::vector<CollisionContact> exited;
  std::vector<CollisionContact> active;

  void clearTransient() {
    entered.clear();
    stayed.clear();
    exited.clear();
    active.clear();
  }

  bool empty() const {
    return entered.empty() && stayed.empty() && exited.empty() && active.empty();
  }
};

}  // namespace karma::components
