#pragma once

#include "karma/core/math/types.h"

namespace karma::renderer {

/// \ingroup karma_rendering
/// World-space ray generated from a screen point and camera state.
struct ScreenRay {
  math::Vec3 origin{};
  math::Vec3 direction{};
};

/// Converts a screen-space point into a world-space camera ray.
bool screenPointToWorldRay(double screen_x,
                           double screen_y,
                           int viewport_width,
                           int viewport_height,
                           const math::Vec3& camera_position,
                           const math::Quat& camera_rotation,
                           float fov_y_degrees,
                           ScreenRay& out_ray);

}  // namespace karma::renderer
