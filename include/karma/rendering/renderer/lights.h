#pragma once

#include <cstdint>

#include "karma/core/math/types.h"

#include <glm/glm.hpp>

namespace karma::renderer {

/// \ingroup karma_rendering
/// Renderer-facing directional light state.
struct DirectionalLightData {
  glm::vec3 direction{0.0f, -1.0f, 0.0f};
  math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  float shadow_extent = 0.0f;
  bool casts_shadows = false;
};

/// \ingroup karma_rendering
/// Renderer-facing local/directional light kind.
enum class LightType : uint32_t {
  Directional = 0,
  Point = 1,
  Spot = 2
};

/// \ingroup karma_rendering
/// Renderer-facing point/spot/directional light record.
struct LightData {
  LightType type = LightType::Point;
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::vec3 direction{0.0f, -1.0f, 0.0f};
  math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float range = 10.0f;
  float inner_cone_cos = 0.9659258f;
  float outer_cone_cos = 0.8660254f;
  bool casts_shadows = false;
};

}  // namespace karma::renderer
