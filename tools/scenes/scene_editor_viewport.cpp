#include "scene_editor_viewport.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace karma::tools::scene_editor {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMaximumPitch = 1.55334306f;  // 89 degrees.
constexpr float kMinimumCameraPlaneDepth = 1.0e-6f;

bool finite(float value) {
  return std::isfinite(value);
}

bool finite(ViewportPoint point) {
  return finite(point.x) && finite(point.y);
}

bool validRotation(const math::Quat& rotation) {
  const float length_squared = math::lengthSquared(rotation);
  return math::isFinite(rotation) && finite(length_squared) &&
         length_squared > 1.0e-12f;
}

bool validFov(float fov_y_degrees) {
  return finite(fov_y_degrees) && fov_y_degrees > 0.0f &&
         fov_y_degrees < 179.0f;
}

float tanHalfFov(float fov_y_degrees) {
  return std::tan(fov_y_degrees * (kPi / 360.0f));
}

math::Quat navigationRotation(const ViewportNavigationState& navigation) {
  return math::fromYawPitch(navigation.yaw, navigation.pitch);
}

math::Vec3 navigationForward(const ViewportNavigationState& navigation) {
  return math::normalize(
      math::rotateVec(navigationRotation(navigation), {0.0f, 0.0f, -1.0f}));
}

bool validRange(float minimum, float maximum) {
  return finite(minimum) && finite(maximum) && minimum > 0.0f &&
         maximum >= minimum;
}

float clampedScaledValue(float value,
                         double factor,
                         float minimum,
                         float maximum) {
  const double scaled = static_cast<double>(value) * factor;
  if (std::isnan(scaled)) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  if (scaled <= static_cast<double>(minimum)) {
    return minimum;
  }
  if (scaled >= static_cast<double>(maximum)) {
    return maximum;
  }
  return static_cast<float>(scaled);
}

}  // namespace

bool validViewportProjection(const ViewportProjection& projection) {
  const ViewportRect& rect = projection.rect;
  const ViewportCamera& camera = projection.camera;
  return finite(rect.x) && finite(rect.y) && finite(rect.width) &&
         finite(rect.height) && rect.width > 0.0f && rect.height > 0.0f &&
         math::isFinite(camera.position) && validRotation(camera.rotation) &&
         validFov(camera.fov_y_degrees) && finite(camera.near_clip) &&
         finite(camera.far_clip) && camera.near_clip > 0.0f &&
         camera.far_clip > camera.near_clip;
}

std::optional<ViewportCameraBasis> viewportCameraBasis(
    const ViewportCamera& camera) {
  if (!validRotation(camera.rotation)) {
    return std::nullopt;
  }
  const math::Quat rotation = math::normalize(camera.rotation);
  const ViewportCameraBasis basis{
      .right = math::normalize(
          math::rotateVec(rotation, {1.0f, 0.0f, 0.0f})),
      .up = math::normalize(
          math::rotateVec(rotation, {0.0f, 1.0f, 0.0f})),
      .forward = math::normalize(
          math::rotateVec(rotation, {0.0f, 0.0f, -1.0f})),
  };
  if (math::lengthSquared(basis.right) <= 0.0f ||
      math::lengthSquared(basis.up) <= 0.0f ||
      math::lengthSquared(basis.forward) <= 0.0f) {
    return std::nullopt;
  }
  return basis;
}

std::optional<ProjectedPoint> projectWorldToViewport(
    const ViewportProjection& projection,
    const math::Vec3& world_point) {
  if (!validViewportProjection(projection) || !math::isFinite(world_point)) {
    return std::nullopt;
  }

  const ViewportCamera& camera = projection.camera;
  const math::Vec3 camera_point = math::rotateVec(
      math::inverse(math::normalize(camera.rotation)),
      math::subtract(world_point, camera.position));
  const float view_depth = -camera_point.z;
  if (!math::isFinite(camera_point) || !finite(view_depth) ||
      view_depth <= kMinimumCameraPlaneDepth) {
    return std::nullopt;
  }

  const float tan_half_fov = tanHalfFov(camera.fov_y_degrees);
  const float aspect = projection.rect.width / projection.rect.height;
  if (!finite(tan_half_fov) || tan_half_fov <= 0.0f || !finite(aspect) ||
      aspect <= 0.0f) {
    return std::nullopt;
  }

  const float ndc_x = camera_point.x / (view_depth * tan_half_fov * aspect);
  const float ndc_y = camera_point.y / (view_depth * tan_half_fov);
  const float near_clip = camera.near_clip;
  const float far_clip = camera.far_clip;
  const float ndc_depth =
      (far_clip + near_clip) / (far_clip - near_clip) -
      (2.0f * far_clip * near_clip) /
          ((far_clip - near_clip) * view_depth);
  const ViewportPoint screen{
      .x = projection.rect.x + (ndc_x + 1.0f) * 0.5f * projection.rect.width,
      .y = projection.rect.y + (1.0f - ndc_y) * 0.5f * projection.rect.height,
  };
  if (!finite(ndc_x) || !finite(ndc_y) || !finite(ndc_depth) ||
      !finite(screen)) {
    return std::nullopt;
  }

  const bool inside_viewport = ndc_x >= -1.0f && ndc_x <= 1.0f &&
                               ndc_y >= -1.0f && ndc_y <= 1.0f;
  const bool inside_depth = view_depth >= near_clip && view_depth <= far_clip;
  return ProjectedPoint{
      .screen = screen,
      .view_depth = view_depth,
      .ndc_depth = ndc_depth,
      .inside_viewport = inside_viewport,
      .inside_clip = inside_viewport && inside_depth,
  };
}

std::optional<ViewportRay> viewportPointToWorldRay(
    const ViewportProjection& projection,
    ViewportPoint screen_point) {
  if (!validViewportProjection(projection) || !finite(screen_point)) {
    return std::nullopt;
  }

  const float local_x =
      (screen_point.x - projection.rect.x) / projection.rect.width;
  const float local_y =
      (screen_point.y - projection.rect.y) / projection.rect.height;
  const float ndc_x = local_x * 2.0f - 1.0f;
  const float ndc_y = 1.0f - local_y * 2.0f;
  const float aspect = projection.rect.width / projection.rect.height;
  const float tan_half_fov = tanHalfFov(projection.camera.fov_y_degrees);
  const math::Vec3 camera_direction = math::normalize(math::Vec3{
      ndc_x * aspect * tan_half_fov, ndc_y * tan_half_fov, -1.0f});
  const math::Vec3 world_direction = math::normalize(math::rotateVec(
      math::normalize(projection.camera.rotation), camera_direction));
  if (!math::isFinite(world_direction) ||
      math::lengthSquared(world_direction) <= 0.0f) {
    return std::nullopt;
  }
  return ViewportRay{
      .origin = projection.camera.position,
      .direction = world_direction,
  };
}

float worldUnitsPerViewportPixel(const ViewportProjection& projection,
                                 const math::Vec3& world_point) {
  const auto projected = projectWorldToViewport(projection, world_point);
  if (!projected) {
    return 0.0f;
  }
  const float units = 2.0f * projected->view_depth *
                      tanHalfFov(projection.camera.fov_y_degrees) /
                      projection.rect.height;
  return finite(units) && units > 0.0f ? units : 0.0f;
}

ViewportNavigationMode unityViewportNavigationMode(
    const ViewportNavigationButtons& buttons) {
  if (buttons.alt && buttons.right) {
    return ViewportNavigationMode::Dolly;
  }
  if (buttons.alt && buttons.left) {
    return ViewportNavigationMode::Orbit;
  }
  if (buttons.middle) {
    return ViewportNavigationMode::Pan;
  }
  if (buttons.right) {
    return ViewportNavigationMode::Fly;
  }
  return ViewportNavigationMode::None;
}

ViewportCameraPose viewportCameraPose(
    const ViewportNavigationState& navigation) {
  const math::Quat rotation = navigationRotation(navigation);
  const math::Vec3 forward = math::normalize(
      math::rotateVec(rotation, {0.0f, 0.0f, -1.0f}));
  const float distance = std::max(navigation.distance, 0.0f);
  return ViewportCameraPose{
      .position = navigation.mode == ViewportNavigationMode::Fly
                      ? navigation.fly_position
                      : math::subtract(navigation.pivot,
                                       math::scale(forward, distance)),
      .rotation = rotation,
  };
}

void setViewportNavigationMode(ViewportNavigationState& navigation,
                               ViewportNavigationMode mode) {
  if (mode == navigation.mode) {
    return;
  }
  if (navigation.mode == ViewportNavigationMode::Fly) {
    navigation.pivot = math::add(
        navigation.fly_position,
        math::scale(navigationForward(navigation),
                    std::max(navigation.distance, 0.0f)));
  }
  if (mode == ViewportNavigationMode::Fly) {
    navigation.fly_position = viewportCameraPose(navigation).position;
  }
  navigation.mode = mode;
}

bool applyViewportLookDrag(ViewportNavigationState& navigation,
                           float mouse_delta_x,
                           float mouse_delta_y,
                           float sensitivity) {
  if ((navigation.mode != ViewportNavigationMode::Orbit &&
       navigation.mode != ViewportNavigationMode::Fly) ||
      !finite(mouse_delta_x) || !finite(mouse_delta_y) ||
      !finite(sensitivity) || sensitivity < 0.0f ||
      !finite(navigation.yaw) || !finite(navigation.pitch)) {
    return false;
  }
  const double next_yaw =
      static_cast<double>(navigation.yaw) -
      static_cast<double>(mouse_delta_x) * sensitivity;
  const double next_pitch =
      static_cast<double>(navigation.pitch) -
      static_cast<double>(mouse_delta_y) * sensitivity;
  if (!std::isfinite(next_yaw) || !std::isfinite(next_pitch)) {
    return false;
  }
  navigation.yaw = static_cast<float>(
      std::remainder(next_yaw, static_cast<double>(2.0f * kPi)));
  navigation.pitch = static_cast<float>(std::clamp(
      next_pitch,
      -static_cast<double>(kMaximumPitch),
      static_cast<double>(kMaximumPitch)));
  return true;
}

bool applyViewportPanDrag(ViewportNavigationState& navigation,
                          float mouse_delta_x,
                          float mouse_delta_y,
                          float viewport_height,
                          float fov_y_degrees,
                          float pan_speed) {
  if (navigation.mode != ViewportNavigationMode::Pan ||
      !finite(mouse_delta_x) || !finite(mouse_delta_y) ||
      !finite(viewport_height) || viewport_height <= 0.0f ||
      !validFov(fov_y_degrees) || !finite(pan_speed) || pan_speed < 0.0f ||
      !finite(navigation.distance) || navigation.distance < 0.0f ||
      !finite(navigation.yaw) || !finite(navigation.pitch) ||
      !math::isFinite(navigation.pivot)) {
    return false;
  }

  const math::Quat rotation = navigationRotation(navigation);
  const math::Vec3 right = math::normalize(
      math::rotateVec(rotation, {1.0f, 0.0f, 0.0f}));
  const math::Vec3 up = math::normalize(
      math::rotateVec(rotation, {0.0f, 1.0f, 0.0f}));
  const float units_per_pixel =
      2.0f * navigation.distance * tanHalfFov(fov_y_degrees) /
      viewport_height * pan_speed;
  if (!finite(units_per_pixel)) {
    return false;
  }
  const math::Vec3 movement = math::add(
      math::scale(right, -mouse_delta_x * units_per_pixel),
      math::scale(up, mouse_delta_y * units_per_pixel));
  if (!math::isFinite(movement)) {
    return false;
  }
  const math::Vec3 next_pivot = math::add(navigation.pivot, movement);
  if (!math::isFinite(next_pivot)) {
    return false;
  }
  navigation.pivot = next_pivot;
  return true;
}

bool applyViewportDollyDrag(ViewportNavigationState& navigation,
                            float mouse_delta_y,
                            float sensitivity,
                            float min_distance,
                            float max_distance) {
  if (navigation.mode != ViewportNavigationMode::Dolly ||
      !finite(mouse_delta_y) || !finite(sensitivity) || sensitivity < 0.0f ||
      !validRange(min_distance, max_distance) ||
      !finite(navigation.distance) || navigation.distance < 0.0f) {
    return false;
  }
  const double factor =
      std::exp(static_cast<double>(mouse_delta_y) * sensitivity);
  navigation.distance = clampedScaledValue(
      navigation.distance, factor, min_distance, max_distance);
  return finite(navigation.distance);
}

bool applyViewportWheel(ViewportNavigationState& navigation,
                        float wheel_delta,
                        float dolly_base,
                        float fly_speed_base,
                        float min_distance,
                        float max_distance,
                        float min_fly_speed,
                        float max_fly_speed) {
  if (!finite(wheel_delta) || !finite(dolly_base) || dolly_base <= 0.0f ||
      !finite(fly_speed_base) || fly_speed_base <= 0.0f ||
      !validRange(min_distance, max_distance) ||
      !validRange(min_fly_speed, max_fly_speed)) {
    return false;
  }
  if (navigation.mode == ViewportNavigationMode::Fly) {
    if (!finite(navigation.fly_speed) || navigation.fly_speed < 0.0f) {
      return false;
    }
    navigation.fly_speed = clampedScaledValue(
        navigation.fly_speed,
        std::pow(static_cast<double>(fly_speed_base), wheel_delta),
        min_fly_speed,
        max_fly_speed);
    return finite(navigation.fly_speed);
  }
  if (!finite(navigation.distance) || navigation.distance < 0.0f) {
    return false;
  }
  navigation.distance = clampedScaledValue(
      navigation.distance,
      std::pow(static_cast<double>(dolly_base), wheel_delta),
      min_distance,
      max_distance);
  return finite(navigation.distance);
}

bool applyViewportFlyMotion(ViewportNavigationState& navigation,
                            const ViewportFlyMotion& motion,
                            float delta_time,
                            bool fast,
                            float fast_multiplier) {
  if (navigation.mode != ViewportNavigationMode::Fly ||
      !finite(motion.right) || !finite(motion.up) ||
      !finite(motion.forward) || !finite(delta_time) || delta_time < 0.0f ||
      !finite(fast_multiplier) || fast_multiplier <= 0.0f ||
      !finite(navigation.fly_speed) || navigation.fly_speed < 0.0f ||
      !finite(navigation.yaw) || !finite(navigation.pitch) ||
      !math::isFinite(navigation.fly_position)) {
    return false;
  }
  const math::Quat rotation = navigationRotation(navigation);
  const math::Vec3 right = math::normalize(
      math::rotateVec(rotation, {1.0f, 0.0f, 0.0f}));
  const math::Vec3 forward = math::normalize(
      math::rotateVec(rotation, {0.0f, 0.0f, -1.0f}));
  math::Vec3 direction = math::add(
      math::add(math::scale(right, motion.right),
                math::scale(math::Vec3{0.0f, 1.0f, 0.0f}, motion.up)),
      math::scale(forward, motion.forward));
  if (math::lengthSquared(direction) <= 1.0e-12f) {
    return true;
  }
  direction = math::normalize(direction);
  const float speed = navigation.fly_speed *
                      (fast ? fast_multiplier : 1.0f);
  const math::Vec3 movement = math::scale(direction, speed * delta_time);
  if (!math::isFinite(movement)) {
    return false;
  }
  const math::Vec3 next_position =
      math::add(navigation.fly_position, movement);
  if (!math::isFinite(next_position)) {
    return false;
  }
  navigation.fly_position = next_position;
  return true;
}

bool frameViewportCamera(ViewportNavigationState& navigation,
                         const math::Vec3& target,
                         float radius,
                         float fov_y_degrees,
                         float padding,
                         float min_distance,
                         float max_distance) {
  if (!math::isFinite(target) || !finite(radius) || radius < 0.0f ||
      !validFov(fov_y_degrees) || !finite(padding) || padding <= 0.0f ||
      !validRange(min_distance, max_distance) || !finite(navigation.yaw) ||
      !finite(navigation.pitch)) {
    return false;
  }
  const float sin_half_fov =
      std::sin(fov_y_degrees * (kPi / 360.0f));
  if (!finite(sin_half_fov) || sin_half_fov <= 0.0f) {
    return false;
  }
  const double required_distance =
      static_cast<double>(radius) * padding / sin_half_fov;
  navigation.pivot = target;
  navigation.distance = static_cast<float>(std::clamp(
      required_distance,
      static_cast<double>(min_distance),
      static_cast<double>(max_distance)));
  if (navigation.mode == ViewportNavigationMode::Fly) {
    navigation.fly_position = math::subtract(
        target,
        math::scale(navigationForward(navigation), navigation.distance));
  }
  return true;
}

}  // namespace karma::tools::scene_editor
