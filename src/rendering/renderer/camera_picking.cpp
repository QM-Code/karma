#include "karma/rendering/renderer/camera_picking.h"

#include <cmath>

#include "karma/core/math/quat.h"
#include "karma/core/math/vec3.h"

namespace karma::renderer {
namespace {

constexpr float kPi = 3.14159265358979323846f;

}  // namespace

bool screenPointToWorldRay(double screen_x,
                           double screen_y,
                           int viewport_width,
                           int viewport_height,
                           const math::Vec3& camera_position,
                           const math::Quat& camera_rotation,
                           float fov_y_degrees,
                           ScreenRay& out_ray) {
  if (viewport_width <= 0 || viewport_height <= 0) {
    return false;
  }

  const float ndc_x =
      static_cast<float>((screen_x / static_cast<double>(viewport_width)) * 2.0 - 1.0);
  const float ndc_y =
      static_cast<float>(1.0 - (screen_y / static_cast<double>(viewport_height)) * 2.0);
  const float aspect = static_cast<float>(viewport_width) / static_cast<float>(viewport_height);
  const float tan_half_fov = std::tan(fov_y_degrees * 0.5f * kPi / 180.0f);
  const math::Vec3 camera_ray =
      math::normalize(math::Vec3{ndc_x * aspect * tan_half_fov, ndc_y * tan_half_fov, -1.0f});
  const math::Vec3 ray_dir =
      math::normalize(math::rotateVec(camera_rotation, camera_ray));

  out_ray.origin = camera_position;
  out_ray.direction = ray_dir;
  return math::lengthSquared(ray_dir) > 0.0f;
}

}  // namespace karma::renderer
