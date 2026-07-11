#pragma once

#include "karma/rendering.h"

namespace karma::rendering::detail {

inline bool isPointShadowAllocationCandidate(const LightData& light) noexcept {
  const bool local_cubemap_light =
      light.type == LightType::Point || light.type == LightType::Spot;
  return local_cubemap_light && light.casts_shadows &&
         light.intensity > 0.0f && light.range > 0.0f;
}

}  // namespace karma::rendering::detail
