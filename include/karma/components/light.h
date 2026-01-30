#pragma once

#include "karma/components/transform.h"
#include "karma/ecs/component.h"

namespace karma::components {

struct LightComponent : ecs::ComponentTag {
  enum class Type {
    Directional,
    Point,
    Spot
  };

  Type type = Type::Point;
  math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float range = 10.0f;
  float inner_cone_degrees = 15.0f;
  float outer_cone_degrees = 30.0f;
  bool casts_shadows = false;
  float shadow_extent = 0.0f;
};

}  // namespace karma::components
