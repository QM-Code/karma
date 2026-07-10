#pragma once

#include "karma/rendering.h"

namespace karma::rendering::detail {

inline bool isPointShadowAllocationCandidate(const LightData& light) noexcept {
  return light.type == LightType::Point && light.casts_shadows &&
         light.intensity > 0.0f && light.range > 0.0f;
}

}  // namespace karma::rendering::detail
