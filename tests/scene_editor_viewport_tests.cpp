#include "scene_editor_viewport.h"
#include "scene_editor_pointer_input.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

#define KARMA_REQUIRE(expression)                                      \
  do {                                                                \
    if (!(expression)) {                                              \
      std::cerr << "Requirement failed: " << #expression << " at " \
                << __FILE__ << ':' << __LINE__ << '\n';              \
      std::abort();                                                   \
    }                                                                 \
  } while (false)

namespace editor = karma::tools::scene_editor;

bool nearly(float a, float b, float epsilon = 0.0001f) {
  return std::abs(a - b) <= epsilon;
}

bool nearlyVec3(const karma::math::Vec3& a,
                const karma::math::Vec3& b,
                float epsilon = 0.0001f) {
  return nearly(a.x, b.x, epsilon) && nearly(a.y, b.y, epsilon) &&
         nearly(a.z, b.z, epsilon);
}

editor::ViewportProjection baseProjection() {
  return editor::ViewportProjection{
      .rect = {.x = 100.0f, .y = 50.0f, .width = 800.0f, .height = 600.0f},
      .camera = {
          .position = {},
          .rotation = {},
          .fov_y_degrees = 60.0f,
          .near_clip = 0.1f,
          .far_clip = 100.0f,
      },
  };
}

editor::ViewportProjection navigationProjection(
    const editor::ViewportNavigationState& navigation,
    float width = 800.0f,
    float height = 600.0f) {
  const editor::ViewportCameraPose pose =
      editor::viewportCameraPose(navigation);
  return editor::ViewportProjection{
      .rect = {.width = width, .height = height},
      .camera = {
          .position = pose.position,
          .rotation = pose.rotation,
          .fov_y_degrees = 60.0f,
          .near_clip = 0.1f,
          .far_clip = 1000.0f,
      },
  };
}

void testViewportInputSnapshotResolution() {
  const editor::ViewportInputSnapshot fallback{
      .primary_down = true,
      .primary_pressed = true,
      .middle_down = true,
      .right_down = true,
      .orbit_modifier_down = true,
      .fast_down = true,
      .move_forward = true,
      .move_backward = true,
      .move_left = true,
      .move_right = true,
      .move_down = true,
      .move_up = true,
      .delta_x = 17.0f,
      .delta_y = -9.0f,
      .wheel = 4.0f,
  };
  const editor::ViewportInputSnapshot without_imgui =
      editor::resolveViewportInputSnapshot(std::nullopt, fallback);
  KARMA_REQUIRE(without_imgui.primary_down);
  KARMA_REQUIRE(without_imgui.primary_pressed);
  KARMA_REQUIRE(without_imgui.middle_down);
  KARMA_REQUIRE(without_imgui.right_down);
  KARMA_REQUIRE(without_imgui.orbit_modifier_down);
  KARMA_REQUIRE(without_imgui.fast_down);
  KARMA_REQUIRE(without_imgui.move_forward);
  KARMA_REQUIRE(without_imgui.move_backward);
  KARMA_REQUIRE(without_imgui.move_left);
  KARMA_REQUIRE(without_imgui.move_right);
  KARMA_REQUIRE(without_imgui.move_down);
  KARMA_REQUIRE(without_imgui.move_up);
  KARMA_REQUIRE(nearly(without_imgui.delta_x, 17.0f));
  KARMA_REQUIRE(nearly(without_imgui.delta_y, -9.0f));
  KARMA_REQUIRE(nearly(without_imgui.wheel, 4.0f));

  const editor::ViewportInputSnapshot imgui{
      .primary_down = false,
      .primary_pressed = false,
      .middle_down = false,
      .right_down = false,
      .delta_x = -2.5f,
      .delta_y = 3.5f,
      .wheel = -1.0f,
  };
  const editor::ViewportInputSnapshot with_imgui =
      editor::resolveViewportInputSnapshot(imgui, fallback);
  KARMA_REQUIRE(!with_imgui.primary_down);
  KARMA_REQUIRE(!with_imgui.primary_pressed);
  KARMA_REQUIRE(!with_imgui.middle_down);
  KARMA_REQUIRE(!with_imgui.right_down);
  KARMA_REQUIRE(!with_imgui.orbit_modifier_down);
  KARMA_REQUIRE(!with_imgui.fast_down);
  KARMA_REQUIRE(!with_imgui.move_forward);
  KARMA_REQUIRE(!with_imgui.move_backward);
  KARMA_REQUIRE(!with_imgui.move_left);
  KARMA_REQUIRE(!with_imgui.move_right);
  KARMA_REQUIRE(!with_imgui.move_down);
  KARMA_REQUIRE(!with_imgui.move_up);
  KARMA_REQUIRE(nearly(with_imgui.delta_x, -2.5f));
  KARMA_REQUIRE(nearly(with_imgui.delta_y, 3.5f));
  KARMA_REQUIRE(nearly(with_imgui.wheel, -1.0f));

  const editor::ViewportInputSnapshot stale_imgui{
      .primary_down = true,
      .primary_pressed = true,
      .middle_down = true,
      .right_down = true,
      .orbit_modifier_down = true,
      .fast_down = true,
      .move_forward = true,
      .move_backward = true,
      .move_left = true,
      .move_right = true,
      .move_down = true,
      .move_up = true,
      .delta_x = 8.0f,
      .delta_y = -6.0f,
      .wheel = 2.0f,
  };
  const editor::ViewportInputSnapshot focus_lost =
      editor::resolveViewportInputSnapshot(stale_imgui, fallback, true);
  KARMA_REQUIRE(!focus_lost.primary_down);
  KARMA_REQUIRE(!focus_lost.primary_pressed);
  KARMA_REQUIRE(!focus_lost.middle_down);
  KARMA_REQUIRE(!focus_lost.right_down);
  KARMA_REQUIRE(!focus_lost.orbit_modifier_down);
  KARMA_REQUIRE(!focus_lost.fast_down);
  KARMA_REQUIRE(!focus_lost.move_forward);
  KARMA_REQUIRE(!focus_lost.move_backward);
  KARMA_REQUIRE(!focus_lost.move_left);
  KARMA_REQUIRE(!focus_lost.move_right);
  KARMA_REQUIRE(!focus_lost.move_down);
  KARMA_REQUIRE(!focus_lost.move_up);
  KARMA_REQUIRE(nearly(focus_lost.delta_x, 0.0f));
  KARMA_REQUIRE(nearly(focus_lost.delta_y, 0.0f));
  KARMA_REQUIRE(nearly(focus_lost.wheel, 0.0f));
}

void testViewportPointerRectangleHit() {
  const editor::ViewportRect viewport{
      .x = 100.0f, .y = 50.0f, .width = 800.0f, .height = 600.0f};
  KARMA_REQUIRE(editor::viewportContainsPointer(viewport, 100.0f, 50.0f));
  KARMA_REQUIRE(editor::viewportContainsPointer(viewport, 899.999f, 649.999f));
  KARMA_REQUIRE(!editor::viewportContainsPointer(viewport, 99.999f, 50.0f));
  KARMA_REQUIRE(!editor::viewportContainsPointer(viewport, 900.0f, 50.0f));
  KARMA_REQUIRE(!editor::viewportContainsPointer(viewport, 100.0f, 650.0f));
  KARMA_REQUIRE(!editor::viewportContainsPointer(
      editor::ViewportRect{.width = 0.0f, .height = 600.0f}, 0.0f, 0.0f));
  KARMA_REQUIRE(!editor::viewportContainsPointer(
      viewport, std::numeric_limits<float>::quiet_NaN(), 100.0f));
}

void testViewportButtonOwnership() {
  KARMA_REQUIRE(editor::updateViewportButtonOwnership(
      false, true, true, true));
  KARMA_REQUIRE(editor::updateViewportButtonOwnership(
      true, true, false, false));
  KARMA_REQUIRE(!editor::updateViewportButtonOwnership(
      true, false, false, true));

  KARMA_REQUIRE(!editor::updateViewportButtonOwnership(
      false, true, true, false));
  KARMA_REQUIRE(!editor::updateViewportButtonOwnership(
      false, true, false, true));
}

void testProjectionOrientationAndClipping() {
  const editor::ViewportProjection projection = baseProjection();
  KARMA_REQUIRE(editor::validViewportProjection(projection));

  const auto center =
      editor::projectWorldToViewport(projection, {0.0f, 0.0f, -10.0f});
  KARMA_REQUIRE(center.has_value());
  KARMA_REQUIRE(nearly(center->screen.x, 500.0f));
  KARMA_REQUIRE(nearly(center->screen.y, 350.0f));
  KARMA_REQUIRE(nearly(center->view_depth, 10.0f));
  KARMA_REQUIRE(center->inside_viewport);
  KARMA_REQUIRE(center->inside_clip);

  const auto world_up =
      editor::projectWorldToViewport(projection, {0.0f, 1.0f, -10.0f});
  const auto world_right =
      editor::projectWorldToViewport(projection, {1.0f, 0.0f, -10.0f});
  KARMA_REQUIRE(world_up.has_value());
  KARMA_REQUIRE(world_right.has_value());
  KARMA_REQUIRE(world_up->screen.y < center->screen.y);
  KARMA_REQUIRE(world_right->screen.x > center->screen.x);

  const auto near_point =
      editor::projectWorldToViewport(projection, {0.0f, 0.0f, -0.1f});
  const auto far_point =
      editor::projectWorldToViewport(projection, {0.0f, 0.0f, -100.0f});
  KARMA_REQUIRE(near_point.has_value());
  KARMA_REQUIRE(far_point.has_value());
  KARMA_REQUIRE(nearly(near_point->ndc_depth, -1.0f, 0.0002f));
  KARMA_REQUIRE(nearly(far_point->ndc_depth, 1.0f, 0.0002f));

  const auto before_near =
      editor::projectWorldToViewport(projection, {0.0f, 0.0f, -0.05f});
  KARMA_REQUIRE(before_near.has_value());
  KARMA_REQUIRE(before_near->inside_viewport);
  KARMA_REQUIRE(!before_near->inside_clip);
  const auto outside =
      editor::projectWorldToViewport(projection, {100.0f, 0.0f, -10.0f});
  KARMA_REQUIRE(outside.has_value());
  KARMA_REQUIRE(!outside->inside_viewport);
  KARMA_REQUIRE(!outside->inside_clip);

  KARMA_REQUIRE(!editor::projectWorldToViewport(
                     projection, {0.0f, 0.0f, 1.0f})
                     .has_value());
  KARMA_REQUIRE(editor::worldUnitsPerViewportPixel(
                    projection, {0.0f, 0.0f, 1.0f}) == 0.0f);
}

void testProjectionAndRayRoundTrips() {
  const editor::ViewportProjection projection = baseProjection();
  const karma::math::Vec3 point{2.0f, 1.25f, -12.0f};
  const auto projected =
      editor::projectWorldToViewport(projection, point);
  KARMA_REQUIRE(projected.has_value());
  const auto ray = editor::viewportPointToWorldRay(
      projection, projected->screen);
  KARMA_REQUIRE(ray.has_value());
  const karma::math::Vec3 expected_direction = karma::math::normalize(point);
  KARMA_REQUIRE(nearly(karma::math::dot(ray->direction, expected_direction),
                       1.0f,
                       0.0001f));
  KARMA_REQUIRE(nearlyVec3(ray->origin, projection.camera.position));

  const auto center_ray = editor::viewportPointToWorldRay(
      projection, {500.0f, 350.0f});
  const auto upper_ray = editor::viewportPointToWorldRay(
      projection, {500.0f, 50.0f});
  KARMA_REQUIRE(center_ray.has_value());
  KARMA_REQUIRE(upper_ray.has_value());
  KARMA_REQUIRE(nearlyVec3(center_ray->direction, {0.0f, 0.0f, -1.0f}));
  KARMA_REQUIRE(upper_ray->direction.y > 0.0f);

  editor::ViewportProjection turned = projection;
  turned.camera.position = {3.0f, -2.0f, 8.0f};
  turned.camera.rotation = karma::math::fromYawPitch(0.52f, -0.24f);
  const auto basis = editor::viewportCameraBasis(turned.camera);
  KARMA_REQUIRE(basis.has_value());
  const karma::math::Vec3 turned_point = karma::math::add(
      turned.camera.position,
      karma::math::add(
          karma::math::scale(basis->forward, 20.0f),
          karma::math::add(karma::math::scale(basis->right, 3.0f),
                           karma::math::scale(basis->up, 2.0f))));
  const auto turned_screen =
      editor::projectWorldToViewport(turned, turned_point);
  KARMA_REQUIRE(turned_screen.has_value());
  const auto turned_ray = editor::viewportPointToWorldRay(
      turned, turned_screen->screen);
  KARMA_REQUIRE(turned_ray.has_value());
  KARMA_REQUIRE(nearly(
      karma::math::dot(
          turned_ray->direction,
          karma::math::normalize(
              karma::math::subtract(turned_point, turned.camera.position))),
      1.0f,
      0.0001f));
}

void testProjectionAcrossViewportSizes() {
  editor::ViewportProjection small = baseProjection();
  small.rect = {.width = 400.0f, .height = 300.0f};
  editor::ViewportProjection large = small;
  large.rect = {.width = 800.0f, .height = 600.0f};
  const karma::math::Vec3 point{2.0f, 1.0f, -10.0f};
  const auto small_point = editor::projectWorldToViewport(small, point);
  const auto large_point = editor::projectWorldToViewport(large, point);
  KARMA_REQUIRE(small_point.has_value());
  KARMA_REQUIRE(large_point.has_value());
  KARMA_REQUIRE(nearly(large_point->screen.x, small_point->screen.x * 2.0f));
  KARMA_REQUIRE(nearly(large_point->screen.y, small_point->screen.y * 2.0f));
  KARMA_REQUIRE(nearly(
      editor::worldUnitsPerViewportPixel(small, point),
      editor::worldUnitsPerViewportPixel(large, point) * 2.0f));

  editor::ViewportProjection wide = large;
  wide.rect.width = 1200.0f;
  const auto wide_point = editor::projectWorldToViewport(wide, point);
  KARMA_REQUIRE(wide_point.has_value());
  KARMA_REQUIRE(nearly(wide_point->screen.y, large_point->screen.y));
  KARMA_REQUIRE(nearly(wide_point->screen.x - 600.0f,
                       large_point->screen.x - 400.0f));

  const float units_per_pixel =
      editor::worldUnitsPerViewportPixel(large, point);
  const auto center =
      editor::projectWorldToViewport(large, {0.0f, 0.0f, -10.0f});
  const auto twenty_pixels_right = editor::projectWorldToViewport(
      large, {units_per_pixel * 20.0f, 0.0f, -10.0f});
  KARMA_REQUIRE(center.has_value());
  KARMA_REQUIRE(twenty_pixels_right.has_value());
  KARMA_REQUIRE(nearly(twenty_pixels_right->screen.x - center->screen.x,
                       20.0f,
                       0.001f));
}

void testProjectionValidation() {
  editor::ViewportProjection projection = baseProjection();
  projection.rect.height = 0.0f;
  KARMA_REQUIRE(!editor::validViewportProjection(projection));
  KARMA_REQUIRE(!editor::viewportPointToWorldRay(projection, {}).has_value());
  projection = baseProjection();
  projection.camera.rotation = {0.0f, 0.0f, 0.0f, 0.0f};
  KARMA_REQUIRE(!editor::validViewportProjection(projection));
  KARMA_REQUIRE(!editor::viewportCameraBasis(projection.camera).has_value());
  projection = baseProjection();
  projection.camera.fov_y_degrees = 180.0f;
  KARMA_REQUIRE(!editor::validViewportProjection(projection));
  projection = baseProjection();
  projection.camera.far_clip = projection.camera.near_clip;
  KARMA_REQUIRE(!editor::validViewportProjection(projection));
  projection = baseProjection();
  KARMA_REQUIRE(!editor::viewportPointToWorldRay(
                     projection,
                     {std::numeric_limits<float>::quiet_NaN(), 0.0f})
                     .has_value());
}

void testUnityNavigationModeChords() {
  using Mode = editor::ViewportNavigationMode;
  KARMA_REQUIRE(editor::unityViewportNavigationMode({}) == Mode::None);
  KARMA_REQUIRE(editor::unityViewportNavigationMode(
                    {.alt = true, .left = true}) == Mode::Orbit);
  KARMA_REQUIRE(editor::unityViewportNavigationMode({.middle = true}) ==
                Mode::Pan);
  KARMA_REQUIRE(editor::unityViewportNavigationMode(
                    {.alt = true, .right = true}) == Mode::Dolly);
  KARMA_REQUIRE(editor::unityViewportNavigationMode({.right = true}) ==
                Mode::Fly);
  KARMA_REQUIRE(editor::unityViewportNavigationMode(
                    {.alt = true, .left = true, .right = true}) ==
                Mode::Dolly);
}

void testOrbitAndLookDirections() {
  editor::ViewportNavigationState navigation{};
  navigation.pivot = {};
  navigation.distance = 10.0f;
  navigation.yaw = 0.0f;
  navigation.pitch = 0.0f;
  editor::setViewportNavigationMode(
      navigation, editor::ViewportNavigationMode::Orbit);

  KARMA_REQUIRE(editor::applyViewportLookDrag(
      navigation, 20.0f, -10.0f, 0.01f));
  KARMA_REQUIRE(navigation.yaw < 0.0f);
  KARMA_REQUIRE(navigation.pitch > 0.0f);
  const editor::ViewportCameraPose orbit_pose =
      editor::viewportCameraPose(navigation);
  const karma::math::Vec3 orbit_forward = karma::math::rotateVec(
      orbit_pose.rotation, {0.0f, 0.0f, -1.0f});
  KARMA_REQUIRE(orbit_forward.x > 0.0f);
  KARMA_REQUIRE(orbit_forward.y > 0.0f);
  KARMA_REQUIRE(orbit_pose.position.x < 0.0f);
  KARMA_REQUIRE(orbit_pose.position.y < 0.0f);

  editor::setViewportNavigationMode(
      navigation, editor::ViewportNavigationMode::None);
  const float old_yaw = navigation.yaw;
  KARMA_REQUIRE(!editor::applyViewportLookDrag(
      navigation, 10.0f, 0.0f, 0.01f));
  KARMA_REQUIRE(nearly(navigation.yaw, old_yaw));

  editor::setViewportNavigationMode(
      navigation, editor::ViewportNavigationMode::Fly);
  const karma::math::Vec3 position_before =
      editor::viewportCameraPose(navigation).position;
  KARMA_REQUIRE(editor::applyViewportLookDrag(
      navigation, 10.0f, -10.0f, 0.01f));
  const editor::ViewportCameraPose fly_pose =
      editor::viewportCameraPose(navigation);
  KARMA_REQUIRE(nearlyVec3(fly_pose.position, position_before));
  const karma::math::Vec3 fly_forward = karma::math::rotateVec(
      fly_pose.rotation, {0.0f, 0.0f, -1.0f});
  KARMA_REQUIRE(fly_forward.x > orbit_forward.x);
  KARMA_REQUIRE(fly_forward.y > orbit_forward.y);
}

void testPanVisibleDragDirections() {
  editor::ViewportNavigationState navigation{};
  navigation.pivot = {};
  navigation.distance = 10.0f;
  navigation.yaw = 0.0f;
  navigation.pitch = 0.0f;
  editor::setViewportNavigationMode(
      navigation, editor::ViewportNavigationMode::Pan);
  KARMA_REQUIRE(editor::applyViewportPanDrag(
      navigation, 30.0f, 20.0f, 600.0f, 60.0f));
  KARMA_REQUIRE(navigation.pivot.x < 0.0f);
  KARMA_REQUIRE(navigation.pivot.y > 0.0f);

  const auto moved_origin = editor::projectWorldToViewport(
      navigationProjection(navigation), {});
  KARMA_REQUIRE(moved_origin.has_value());
  KARMA_REQUIRE(moved_origin->screen.x > 400.0f);
  KARMA_REQUIRE(moved_origin->screen.y > 300.0f);
  KARMA_REQUIRE(nearly(moved_origin->screen.x, 430.0f, 0.001f));
  KARMA_REQUIRE(nearly(moved_origin->screen.y, 320.0f, 0.001f));
}

void testDollyAndWheelSemantics() {
  editor::ViewportNavigationState navigation{};
  navigation.pivot = {};
  navigation.distance = 10.0f;
  navigation.yaw = 0.0f;
  navigation.pitch = 0.0f;
  editor::setViewportNavigationMode(
      navigation, editor::ViewportNavigationMode::Dolly);
  KARMA_REQUIRE(editor::applyViewportDollyDrag(navigation, -20.0f));
  KARMA_REQUIRE(navigation.distance < 10.0f);
  const float after_in = navigation.distance;
  KARMA_REQUIRE(editor::applyViewportDollyDrag(navigation, 20.0f));
  KARMA_REQUIRE(nearly(navigation.distance, 10.0f, 0.0002f));
  KARMA_REQUIRE(after_in < navigation.distance);

  const karma::math::Vec3 pivot_before = navigation.pivot;
  KARMA_REQUIRE(editor::applyViewportWheel(navigation, 1.0f));
  KARMA_REQUIRE(nearly(navigation.distance, 8.5f));
  KARMA_REQUIRE(nearlyVec3(navigation.pivot, pivot_before));
  KARMA_REQUIRE(editor::applyViewportWheel(navigation, -1.0f));
  KARMA_REQUIRE(nearly(navigation.distance, 10.0f, 0.0002f));

  editor::setViewportNavigationMode(
      navigation, editor::ViewportNavigationMode::Fly);
  const editor::ViewportCameraPose pose_before =
      editor::viewportCameraPose(navigation);
  const float distance_before = navigation.distance;
  const float speed_before = navigation.fly_speed;
  KARMA_REQUIRE(editor::applyViewportWheel(navigation, 1.0f));
  KARMA_REQUIRE(navigation.fly_speed > speed_before);
  KARMA_REQUIRE(nearly(navigation.distance, distance_before));
  KARMA_REQUIRE(nearlyVec3(
      editor::viewportCameraPose(navigation).position, pose_before.position));
}

void testFlyMotionAndOrbitTransition() {
  editor::ViewportNavigationState navigation{};
  navigation.pivot = {};
  navigation.distance = 10.0f;
  navigation.yaw = 0.0f;
  navigation.pitch = 0.0f;
  navigation.fly_speed = 2.0f;
  const editor::ViewportCameraPose orbit_pose =
      editor::viewportCameraPose(navigation);
  KARMA_REQUIRE(nearlyVec3(orbit_pose.position, {0.0f, 0.0f, 10.0f}));

  editor::setViewportNavigationMode(
      navigation, editor::ViewportNavigationMode::Fly);
  KARMA_REQUIRE(nearlyVec3(navigation.fly_position, orbit_pose.position));
  KARMA_REQUIRE(editor::applyViewportFlyMotion(
      navigation, {.forward = 1.0f}, 1.0f, false));
  KARMA_REQUIRE(nearlyVec3(navigation.fly_position, {0.0f, 0.0f, 8.0f}));
  KARMA_REQUIRE(editor::applyViewportFlyMotion(
      navigation, {.right = 1.0f}, 0.5f, true));
  KARMA_REQUIRE(nearlyVec3(navigation.fly_position, {4.0f, 0.0f, 8.0f}));

  KARMA_REQUIRE(editor::applyViewportLookDrag(
      navigation, 25.0f, -10.0f, 0.01f));
  const editor::ViewportCameraPose flown_pose =
      editor::viewportCameraPose(navigation);
  editor::setViewportNavigationMode(
      navigation, editor::ViewportNavigationMode::Orbit);
  const editor::ViewportCameraPose transitioned_pose =
      editor::viewportCameraPose(navigation);
  KARMA_REQUIRE(nearlyVec3(transitioned_pose.position,
                           flown_pose.position,
                           0.0002f));
  KARMA_REQUIRE(nearlyVec3(
      navigation.pivot,
      karma::math::add(
          flown_pose.position,
          karma::math::scale(
              karma::math::rotateVec(
                  flown_pose.rotation, {0.0f, 0.0f, -1.0f}),
              navigation.distance)),
      0.0002f));
}

void testFraming() {
  editor::ViewportNavigationState navigation{};
  navigation.yaw = 0.0f;
  navigation.pitch = 0.0f;
  const karma::math::Vec3 target{4.0f, 3.0f, -2.0f};
  KARMA_REQUIRE(editor::frameViewportCamera(
      navigation, target, 2.0f, 60.0f));
  KARMA_REQUIRE(nearlyVec3(navigation.pivot, target));
  KARMA_REQUIRE(nearly(navigation.distance, 4.8f, 0.0002f));
  const editor::ViewportCameraPose pose =
      editor::viewportCameraPose(navigation);
  KARMA_REQUIRE(nearlyVec3(pose.position, {4.0f, 3.0f, 2.8f}, 0.0002f));

  editor::setViewportNavigationMode(
      navigation, editor::ViewportNavigationMode::Fly);
  const karma::math::Vec3 second_target{-2.0f, 1.0f, 6.0f};
  KARMA_REQUIRE(editor::frameViewportCamera(
      navigation, second_target, 0.0f, 60.0f));
  const editor::ViewportCameraPose fly_pose =
      editor::viewportCameraPose(navigation);
  editor::setViewportNavigationMode(
      navigation, editor::ViewportNavigationMode::None);
  KARMA_REQUIRE(nearlyVec3(navigation.pivot, second_target));
  KARMA_REQUIRE(nearlyVec3(
      editor::viewportCameraPose(navigation).position, fly_pose.position));
}

}  // namespace

int main() {
  testViewportInputSnapshotResolution();
  testViewportPointerRectangleHit();
  testViewportButtonOwnership();
  testProjectionOrientationAndClipping();
  testProjectionAndRayRoundTrips();
  testProjectionAcrossViewportSizes();
  testProjectionValidation();
  testUnityNavigationModeChords();
  testOrbitAndLookDirections();
  testPanVisibleDragDirections();
  testDollyAndWheelSemantics();
  testFlyMotionAndOrbitTransition();
  testFraming();
  std::cout << "scene editor viewport tests passed\n";
  return 0;
}
