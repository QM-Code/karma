#pragma once

#include <cstdint>
#include <vector>

#include "karma/ecs/collider_queries.h"
#include "karma/ecs/component.h"
#include "karma/ecs/entity.h"
#include "karma/math/types.h"

namespace karma::components {

struct ContactEvent {
  ecs::Entity other{};
  ecs::queries::ColliderShape other_shape = ecs::queries::ColliderShape::Box;
  math::Vec3 point{};
  math::Vec3 normal{0.0f, 1.0f, 0.0f};
};

struct ContactListenerComponent : ecs::ComponentTag {
  bool enabled = true;
  bool emit_stay = false;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
};

struct ContactEventsComponent : ecs::ComponentTag {
  std::vector<ContactEvent> entered;
  std::vector<ContactEvent> stayed;
  std::vector<ContactEvent> exited;
  std::vector<ContactEvent> active;

  void clearTransient() {
    entered.clear();
    stayed.clear();
    exited.clear();
    active.clear();
  }
};

}  // namespace karma::components
