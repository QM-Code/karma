#pragma once

#include "karma/world/components/transform.h"
#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Light data extracted by `RenderSystem`.
///
/// Directional, point, and spot lights share one component. Point lights can
/// request shadows when the renderer has point-shadow budget available.
struct LightComponent : ecs::ComponentTag {
  /// Light shape/type consumed by the renderer.
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
