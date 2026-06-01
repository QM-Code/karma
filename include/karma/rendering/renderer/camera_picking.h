#pragma once

#include "karma/core/math/types.h"

namespace karma::renderer {

struct ScreenRay {
  math::Vec3 origin{};
  math::Vec3 direction{};
};

bool screenPointToWorldRay(double screen_x,
                           double screen_y,
                           int viewport_width,
                           int viewport_height,
                           const math::Vec3& camera_position,
                           const math::Quat& camera_rotation,
                           float fov_y_degrees,
                           ScreenRay& out_ray);

}  // namespace karma::renderer
