#pragma once

#include "karma/ecs/entity.h"
#include "karma/ecs/component.h"
#include "karma/math/types.h"

namespace karma::components {

struct GroundContactComponent : ecs::ComponentTag {
  bool grounded = false;
  bool entered = false;
  bool exited = false;
  bool has_support = false;
  ecs::Entity support_entity{};
  math::Vec3 point{};
  math::Vec3 normal{0.0f, 1.0f, 0.0f};

  void clearTransient() {
    entered = false;
    exited = false;
  }
};

}  // namespace karma::components
