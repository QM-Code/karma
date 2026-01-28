#pragma once

#include "karma/components/transform.h"
#include "karma/ecs/component.h"

namespace karma::components {

struct ColliderComponent : ecs::ComponentTag {
  enum class Shape {
    Box,
    Sphere,
    Capsule,
    Mesh
  };

  Shape shape = Shape::Box;
  Vec3 center{};
  Vec3 half_extents{0.5f, 0.5f, 0.5f};
  float radius = 0.5f;
  float height = 1.0f;
  bool is_trigger = false;
};

}  // namespace karma::components
