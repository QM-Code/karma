#include "karma/rendering.h"

#include <cmath>

#include "karma/math.h"

namespace karma::rendering {
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
  const bool finite_position = std::isfinite(camera_position.x) &&
                               std::isfinite(camera_position.y) &&
                               std::isfinite(camera_position.z);
  const bool finite_rotation = std::isfinite(camera_rotation.x) &&
                               std::isfinite(camera_rotation.y) &&
                               std::isfinite(camera_rotation.z) &&
                               std::isfinite(camera_rotation.w);
  const float rotation_length_squared = math::lengthSquared(camera_rotation);
  if (viewport_width <= 0 || viewport_height <= 0 ||
      !std::isfinite(screen_x) || !std::isfinite(screen_y) ||
      !finite_position || !finite_rotation ||
      !std::isfinite(rotation_length_squared) || rotation_length_squared <= 1.0e-12f ||
      !std::isfinite(fov_y_degrees) || fov_y_degrees <= 0.0f ||
      fov_y_degrees >= 179.0f) {
    return false;
  }

  const float ndc_x =
      static_cast<float>((screen_x / static_cast<double>(viewport_width)) * 2.0 - 1.0);
  const float ndc_y =
      static_cast<float>(1.0 - (screen_y / static_cast<double>(viewport_height)) * 2.0);
  const float aspect = static_cast<float>(viewport_width) / static_cast<float>(viewport_height);
  const float tan_half_fov = std::tan(fov_y_degrees * 0.5f * kPi / 180.0f);
  if (!std::isfinite(tan_half_fov) || tan_half_fov <= 0.0f) {
    return false;
  }
  const math::Vec3 camera_ray =
      math::normalize(math::Vec3{ndc_x * aspect * tan_half_fov, ndc_y * tan_half_fov, -1.0f});
  const math::Quat normalized_rotation = math::normalize(camera_rotation);
  const math::Vec3 ray_dir =
      math::normalize(math::rotateVec(normalized_rotation, camera_ray));
  const float ray_length_squared = math::lengthSquared(ray_dir);
  if (!std::isfinite(ray_dir.x) || !std::isfinite(ray_dir.y) ||
      !std::isfinite(ray_dir.z) || !std::isfinite(ray_length_squared) ||
      ray_length_squared <= 0.0f) {
    return false;
  }

  out_ray.origin = camera_position;
  out_ray.direction = ray_dir;
  return true;
}

}  // namespace karma::rendering
