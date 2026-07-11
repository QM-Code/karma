#include "scene_editor_gizmo.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <set>

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
namespace math = karma::math;
namespace scenes = karma::scenes;

constexpr float kPi = 3.14159265358979323846f;

bool nearly(float a, float b, float epsilon = 0.001f) {
  return std::abs(a - b) <= epsilon;
}

bool nearlyVec(const math::Vec3& a,
               const math::Vec3& b,
               float epsilon = 0.001f) {
  return nearly(a.x, b.x, epsilon) && nearly(a.y, b.y, epsilon) &&
         nearly(a.z, b.z, epsilon);
}

bool nearlyRotation(const math::Quat& a,
                    const math::Quat& b,
                    float epsilon = 0.001f) {
  return std::abs(math::dot(math::normalize(a), math::normalize(b))) >=
         1.0f - epsilon;
}

bool finiteTransform(const scenes::SceneTransform& transform) {
  return math::isFinite(transform.position) &&
         math::isFinite(transform.rotation) &&
         math::isFinite(transform.scale);
}

math::Quat axisAngle(math::Vec3 axis, float angle) {
  axis = math::normalize(axis);
  const float half = angle * 0.5f;
  const float sine = std::sin(half);
  return math::normalize(
      {axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half)});
}

math::Vec3 rotateAround(math::Vec3 value, math::Vec3 axis, float angle) {
  return math::rotateVec(axisAngle(axis, angle), value);
}

scenes::SceneTransform compose(const scenes::SceneTransform& parent,
                               const scenes::SceneTransform& local) {
  return scenes::SceneTransform{
      .position = math::add(
          parent.position,
          math::rotateVec(parent.rotation,
                          math::multiply(local.position, parent.scale))),
      .rotation = math::normalize(math::mul(parent.rotation, local.rotation)),
      .scale = math::multiply(parent.scale, local.scale),
  };
}

editor::ViewportProjection projectionFor(
    math::Vec3 pivot = {},
    float distance = 5.0f,
    float yaw = 0.0f,
    float pitch = 0.0f,
    float width = 800.0f,
    float height = 600.0f) {
  const math::Quat rotation = math::fromYawPitch(yaw, pitch);
  const math::Vec3 forward =
      math::rotateVec(rotation, {0.0f, 0.0f, -1.0f});
  return editor::ViewportProjection{
      .rect = {20.0f, 30.0f, width, height},
      .camera = {
          .position = math::subtract(pivot, math::scale(forward, distance)),
          .rotation = rotation,
          .fov_y_degrees = 60.0f,
          .near_clip = 0.1f,
          .far_clip = 1000.0f,
      },
  };
}

editor::ViewportPoint screenPoint(const editor::ViewportProjection& projection,
                                  const math::Vec3& world) {
  const auto projected = editor::projectWorldToViewport(projection, world);
  KARMA_REQUIRE(projected.has_value());
  KARMA_REQUIRE(projected->inside_clip);
  return projected->screen;
}

std::set<editor::GizmoHandle> handleSet(
    const editor::GizmoGeometry& geometry) {
  std::set<editor::GizmoHandle> result;
  for (const editor::GizmoHandleGeometry& handle : geometry.handles) {
    result.insert(handle.handle);
  }
  return result;
}

editor::GizmoGeometry build(editor::GizmoTool tool,
                            editor::GizmoSpace space,
                            const scenes::SceneTransform& transform,
                            const editor::ViewportProjection& projection) {
  return editor::buildGizmoGeometry({
      .tool = tool,
      .space = space,
      .world_transform = transform,
      .projection = projection,
      .apparent_size_pixels = 96.0f,
  });
}

void requireHit(const editor::GizmoGeometry& geometry,
                const editor::ViewportProjection& projection,
                const math::Vec3& point,
                editor::GizmoHandle expected) {
  const auto hit = editor::hitTestGizmo(
      geometry, projection, screenPoint(projection, point));
  KARMA_REQUIRE(hit.has_value());
  KARMA_REQUIRE(hit->handle == expected);
}

void testGeometryAndOrientation() {
  const editor::ViewportProjection projection =
      projectionFor({}, 8.0f, 0.65f, -0.32f);
  const scenes::SceneTransform identity{};

  const editor::GizmoGeometry move =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            identity, projection);
  KARMA_REQUIRE(move.world_size > 0.0f);
  const std::set<editor::GizmoHandle> expected_move{
      editor::GizmoHandle::MoveX,
      editor::GizmoHandle::MoveY,
      editor::GizmoHandle::MoveZ,
      editor::GizmoHandle::MoveXY,
      editor::GizmoHandle::MoveXZ,
      editor::GizmoHandle::MoveYZ,
      editor::GizmoHandle::MoveView,
  };
  KARMA_REQUIRE(handleSet(move) == expected_move);

  const editor::GizmoGeometry rotate =
      build(editor::GizmoTool::Rotate, editor::GizmoSpace::World,
            identity, projection);
  const std::set<editor::GizmoHandle> expected_rotate{
      editor::GizmoHandle::RotateX,
      editor::GizmoHandle::RotateY,
      editor::GizmoHandle::RotateZ,
      editor::GizmoHandle::RotateView,
  };
  KARMA_REQUIRE(handleSet(rotate) == expected_rotate);
  KARMA_REQUIRE(rotate.lines.size() == 4u * 64u);

  const editor::GizmoGeometry scale =
      build(editor::GizmoTool::Scale, editor::GizmoSpace::World,
            identity, projection);
  const std::set<editor::GizmoHandle> expected_scale{
      editor::GizmoHandle::ScaleX,
      editor::GizmoHandle::ScaleY,
      editor::GizmoHandle::ScaleZ,
      editor::GizmoHandle::ScaleUniform,
  };
  KARMA_REQUIRE(handleSet(scale) == expected_scale);

  for (const auto* geometry : {&move, &rotate, &scale}) {
    KARMA_REQUIRE(!geometry->lines.empty());
    for (const editor::GizmoLineSegment& line : geometry->lines) {
      KARMA_REQUIRE(math::isFinite(line.start));
      KARMA_REQUIRE(math::isFinite(line.end));
      KARMA_REQUIRE(line.handle != editor::GizmoHandle::None);
      KARMA_REQUIRE(line.thickness_pixels > 0.0f);
    }
  }

  scenes::SceneTransform rotated{};
  rotated.rotation = axisAngle({0.0f, 0.0f, 1.0f}, kPi * 0.5f);
  const editor::GizmoGeometry world_move =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            rotated, projection);
  const editor::GizmoGeometry local_move =
      build(editor::GizmoTool::Move, editor::GizmoSpace::Local,
            rotated, projection);
  const editor::GizmoGeometry world_scale =
      build(editor::GizmoTool::Scale, editor::GizmoSpace::World,
            rotated, projection);
  KARMA_REQUIRE(nearlyVec(world_move.axes[0], {1.0f, 0.0f, 0.0f}));
  KARMA_REQUIRE(nearlyVec(local_move.axes[0], {0.0f, 1.0f, 0.0f}));
  // Scale is deliberately local even while the move/rotate mode says World.
  KARMA_REQUIRE(nearlyVec(world_scale.axes[0], local_move.axes[0]));
}

void testApparentSizingAndVisibility() {
  scenes::SceneTransform near_transform{};
  scenes::SceneTransform far_transform{};
  far_transform.position.z = -45.0f;
  const editor::ViewportProjection projection = projectionFor();
  const editor::GizmoGeometry near_geometry =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            near_transform, projection);
  const editor::GizmoGeometry far_geometry =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            far_transform, projection);
  KARMA_REQUIRE(nearly(far_geometry.world_size / near_geometry.world_size,
                       10.0f, 0.01f));

  const auto apparentLength = [](const editor::GizmoGeometry& geometry,
                                 const editor::ViewportProjection& view) {
    const editor::ViewportPoint pivot = screenPoint(view, geometry.pivot);
    const editor::ViewportPoint tip = screenPoint(
        view, math::add(geometry.pivot,
                        math::scale(geometry.axes[0], geometry.world_size)));
    return std::sqrt((tip.x - pivot.x) * (tip.x - pivot.x) +
                     (tip.y - pivot.y) * (tip.y - pivot.y));
  };
  KARMA_REQUIRE(nearly(apparentLength(near_geometry, projection), 96.0f,
                       0.05f));
  KARMA_REQUIRE(nearly(apparentLength(far_geometry, projection), 96.0f,
                       0.05f));

  const editor::ViewportProjection wide =
      projectionFor({}, 5.0f, 0.0f, 0.0f, 1600.0f, 600.0f);
  const editor::ViewportProjection tall =
      projectionFor({}, 5.0f, 0.0f, 0.0f, 800.0f, 1200.0f);
  const editor::GizmoGeometry wide_geometry =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            near_transform, wide);
  const editor::GizmoGeometry tall_geometry =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            near_transform, tall);
  KARMA_REQUIRE(nearly(apparentLength(wide_geometry, wide), 96.0f, 0.05f));
  KARMA_REQUIRE(nearly(apparentLength(tall_geometry, tall), 96.0f, 0.05f));

  scenes::SceneTransform behind{};
  behind.position.z = 6.0f;
  const editor::GizmoGeometry hidden =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            behind, projection);
  KARMA_REQUIRE(hidden.world_size == 0.0f);
  KARMA_REQUIRE(hidden.lines.empty());
}

void testProjectedHandlePicking() {
  const editor::ViewportProjection projection =
      projectionFor({}, 8.0f, 0.65f, -0.32f);
  const scenes::SceneTransform transform{};
  const editor::GizmoGeometry move =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            transform, projection);

  requireHit(move, projection, math::scale(move.axes[0], move.world_size * 0.68f),
             editor::GizmoHandle::MoveX);
  requireHit(move, projection, math::scale(move.axes[1], move.world_size * 0.68f),
             editor::GizmoHandle::MoveY);
  requireHit(move, projection, math::scale(move.axes[2], move.world_size * 0.68f),
             editor::GizmoHandle::MoveZ);
  requireHit(move, projection,
             math::add(math::scale(move.axes[0], move.world_size * 0.3f),
                       math::scale(move.axes[1], move.world_size * 0.3f)),
             editor::GizmoHandle::MoveXY);
  requireHit(move, projection,
             math::add(math::scale(move.axes[0], move.world_size * 0.3f),
                       math::scale(move.axes[2], move.world_size * 0.3f)),
             editor::GizmoHandle::MoveXZ);
  requireHit(move, projection,
             math::add(math::scale(move.axes[1], move.world_size * 0.3f),
                       math::scale(move.axes[2], move.world_size * 0.3f)),
             editor::GizmoHandle::MoveYZ);
  requireHit(move, projection, move.pivot, editor::GizmoHandle::MoveView);

  const editor::GizmoGeometry rotate =
      build(editor::GizmoTool::Rotate, editor::GizmoSpace::World,
            transform, projection);
  const float radius = rotate.world_size * 0.82f;
  const float sample_angle = 0.7f;
  requireHit(rotate, projection,
             math::add(math::scale(rotate.axes[1], radius * std::cos(sample_angle)),
                       math::scale(rotate.axes[2], radius * std::sin(sample_angle))),
             editor::GizmoHandle::RotateX);
  requireHit(rotate, projection,
             math::add(math::scale(rotate.axes[2], radius * std::cos(sample_angle)),
                       math::scale(rotate.axes[0], radius * std::sin(sample_angle))),
             editor::GizmoHandle::RotateY);
  requireHit(rotate, projection,
             math::add(math::scale(rotate.axes[0], radius * std::cos(sample_angle)),
                       math::scale(rotate.axes[1], radius * std::sin(sample_angle))),
             editor::GizmoHandle::RotateZ);
  const auto basis = editor::viewportCameraBasis(projection.camera);
  KARMA_REQUIRE(basis.has_value());
  requireHit(rotate, projection,
             math::add(math::scale(basis->right,
                                   rotate.world_size * 1.08f *
                                       std::cos(sample_angle)),
                       math::scale(basis->up,
                                   rotate.world_size * 1.08f *
                                       std::sin(sample_angle))),
             editor::GizmoHandle::RotateView);

  const editor::GizmoGeometry scale =
      build(editor::GizmoTool::Scale, editor::GizmoSpace::World,
            transform, projection);
  requireHit(scale, projection,
             math::scale(scale.axes[0], scale.world_size * 0.62f),
             editor::GizmoHandle::ScaleX);
  requireHit(scale, projection,
             math::scale(scale.axes[1], scale.world_size * 0.62f),
             editor::GizmoHandle::ScaleY);
  requireHit(scale, projection,
             math::scale(scale.axes[2], scale.world_size * 0.62f),
             editor::GizmoHandle::ScaleZ);
  requireHit(scale, projection, scale.pivot,
             editor::GizmoHandle::ScaleUniform);

  const auto outside = editor::hitTestGizmo(
      move, projection,
      {projection.rect.x - 1.0f, projection.rect.y - 1.0f});
  KARMA_REQUIRE(!outside.has_value());

  // A camera exactly parallel to Z collapses that projected arrow. The center
  // grip wins instead of returning an unstable/ambiguous Z-axis hit.
  const editor::ViewportProjection parallel = projectionFor();
  const editor::GizmoGeometry parallel_move =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            transform, parallel);
  const auto collapsed = editor::hitTestGizmo(
      parallel_move, parallel, screenPoint(parallel, {}));
  KARMA_REQUIRE(collapsed.has_value());
  KARMA_REQUIRE(collapsed->handle == editor::GizmoHandle::MoveView);
}

editor::GizmoDragBegin dragDesc(editor::GizmoHandle handle,
                                const editor::ViewportProjection& projection,
                                const scenes::SceneTransform& local,
                                const scenes::SceneTransform& world,
                                float world_size) {
  return editor::GizmoDragBegin{
      .handle = handle,
      .space = editor::GizmoSpace::World,
      .projection = projection,
      .local_transform = local,
      .world_transform = world,
      .world_size = world_size,
  };
}

void testEveryMoveHandleAndSnapping() {
  const editor::ViewportProjection projection =
      projectionFor({}, 8.0f, 0.62f, -0.28f);
  const scenes::SceneTransform transform{};
  const editor::GizmoGeometry geometry =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            transform, projection);

  constexpr std::array<editor::GizmoHandle, 3> axis_handles{
      editor::GizmoHandle::MoveX,
      editor::GizmoHandle::MoveY,
      editor::GizmoHandle::MoveZ,
  };
  for (size_t axis = 0; axis < axis_handles.size(); ++axis) {
    const math::Vec3 start =
        math::scale(geometry.axes[axis], geometry.world_size * 0.65f);
    const math::Vec3 end =
        math::add(start, math::scale(geometry.axes[axis], 0.73f));
    editor::GizmoDragState drag;
    KARMA_REQUIRE(drag.begin(
        dragDesc(axis_handles[axis], projection, transform, transform,
                 geometry.world_size),
        screenPoint(projection, start)));
    const auto update = drag.update(screenPoint(projection, end));
    KARMA_REQUIRE(update.has_value());
    KARMA_REQUIRE(nearlyVec(update->world_transform.position,
                            math::scale(geometry.axes[axis], 0.73f),
                            0.003f));
    const auto completion = drag.finish();
    KARMA_REQUIRE(completion.has_value());
    KARMA_REQUIRE(completion->commit);
    KARMA_REQUIRE(!drag.finish().has_value());
  }

  constexpr std::array<editor::GizmoHandle, 3> plane_handles{
      editor::GizmoHandle::MoveXY,
      editor::GizmoHandle::MoveXZ,
      editor::GizmoHandle::MoveYZ,
  };
  constexpr std::array<std::array<int, 2>, 3> plane_axes{{
      {{0, 1}}, {{0, 2}}, {{1, 2}},
  }};
  for (size_t index = 0; index < plane_handles.size(); ++index) {
    const math::Vec3 axis_a = geometry.axes[plane_axes[index][0]];
    const math::Vec3 axis_b = geometry.axes[plane_axes[index][1]];
    const math::Vec3 start = math::add(math::scale(axis_a, 0.3f),
                                       math::scale(axis_b, 0.3f));
    const math::Vec3 expected = math::add(math::scale(axis_a, 0.45f),
                                          math::scale(axis_b, 0.65f));
    const math::Vec3 end = math::add(start, expected);
    editor::GizmoDragState drag;
    KARMA_REQUIRE(drag.begin(
        dragDesc(plane_handles[index], projection, transform, transform,
                 geometry.world_size),
        screenPoint(projection, start)));
    const auto update = drag.update(screenPoint(projection, end));
    KARMA_REQUIRE(update.has_value());
    KARMA_REQUIRE(nearlyVec(update->world_transform.position, expected,
                            0.003f));
  }

  const auto camera_basis = editor::viewportCameraBasis(projection.camera);
  KARMA_REQUIRE(camera_basis.has_value());
  editor::GizmoDragState view_drag;
  KARMA_REQUIRE(view_drag.begin(
      dragDesc(editor::GizmoHandle::MoveView, projection, transform, transform,
               geometry.world_size),
      screenPoint(projection, {})));
  const math::Vec3 view_delta =
      math::add(math::scale(camera_basis->right, 0.4f),
                math::scale(camera_basis->up, 0.3f));
  const auto view_update =
      view_drag.update(screenPoint(projection, view_delta));
  KARMA_REQUIRE(view_update.has_value());
  KARMA_REQUIRE(nearlyVec(view_update->world_transform.position, view_delta,
                          0.003f));

  editor::GizmoDragBegin snapped =
      dragDesc(editor::GizmoHandle::MoveX, projection, transform, transform,
               geometry.world_size);
  snapped.snap = {.enabled = true, .translation_step = 0.5f};
  const math::Vec3 start = math::scale(geometry.axes[0], 0.5f);
  const math::Vec3 end = math::add(start, math::scale(geometry.axes[0], 0.76f));
  editor::GizmoDragState snap_drag;
  KARMA_REQUIRE(snap_drag.begin(snapped, screenPoint(projection, start)));
  const auto snap_update = snap_drag.update(screenPoint(projection, end));
  KARMA_REQUIRE(snap_update.has_value());
  KARMA_REQUIRE(nearlyVec(snap_update->world_transform.position,
                          geometry.axes[0], 0.003f));
}

void testEveryRotationHandleAndSnapping() {
  const editor::ViewportProjection projection =
      projectionFor({}, 8.0f, 0.62f, -0.28f);
  const scenes::SceneTransform transform{};
  const editor::GizmoGeometry geometry =
      build(editor::GizmoTool::Rotate, editor::GizmoSpace::World,
            transform, projection);
  const auto camera_basis = editor::viewportCameraBasis(projection.camera);
  KARMA_REQUIRE(camera_basis.has_value());

  constexpr std::array<editor::GizmoHandle, 4> handles{
      editor::GizmoHandle::RotateX,
      editor::GizmoHandle::RotateY,
      editor::GizmoHandle::RotateZ,
      editor::GizmoHandle::RotateView,
  };
  for (size_t index = 0; index < handles.size(); ++index) {
    math::Vec3 axis{};
    math::Vec3 start_vector{};
    if (index < 3u) {
      axis = geometry.axes[index];
      start_vector = geometry.axes[(index + 1u) % 3u];
    } else {
      axis = camera_basis->forward;
      start_vector = camera_basis->right;
    }
    const float input_angle = 22.0f * kPi / 180.0f;
    const math::Vec3 end_vector =
        rotateAround(start_vector, axis, input_angle);
    editor::GizmoDragBegin desc =
        dragDesc(handles[index], projection, transform, transform,
                 geometry.world_size);
    desc.snap = {.enabled = true, .rotation_step_degrees = 15.0f};
    editor::GizmoDragState drag;
    KARMA_REQUIRE(drag.begin(
        desc, screenPoint(projection,
                          math::scale(start_vector,
                                      geometry.world_size * 0.82f))));
    const auto update = drag.update(screenPoint(
        projection,
        math::scale(end_vector, geometry.world_size * 0.82f)));
    KARMA_REQUIRE(update.has_value());
    const math::Quat expected = axisAngle(axis, 15.0f * kPi / 180.0f);
    KARMA_REQUIRE(nearlyRotation(update->world_transform.rotation, expected,
                                 0.0002f));
  }
}

void testEveryScaleHandleAndSnapping() {
  const editor::ViewportProjection projection =
      projectionFor({}, 8.0f, 0.62f, -0.28f);
  const scenes::SceneTransform transform{};
  const editor::GizmoGeometry geometry =
      build(editor::GizmoTool::Scale, editor::GizmoSpace::World,
            transform, projection);
  constexpr std::array<editor::GizmoHandle, 3> handles{
      editor::GizmoHandle::ScaleX,
      editor::GizmoHandle::ScaleY,
      editor::GizmoHandle::ScaleZ,
  };
  for (size_t axis = 0; axis < handles.size(); ++axis) {
    const math::Vec3 start =
        math::scale(geometry.axes[axis], geometry.world_size * 0.65f);
    const math::Vec3 end = math::add(
        start, math::scale(geometry.axes[axis], geometry.world_size * 0.46f));
    editor::GizmoDragBegin desc =
        dragDesc(handles[axis], projection, transform, transform,
                 geometry.world_size);
    desc.snap = {.enabled = true, .scale_step = 0.1f};
    editor::GizmoDragState drag;
    KARMA_REQUIRE(drag.begin(desc, screenPoint(projection, start)));
    const auto update = drag.update(screenPoint(projection, end));
    KARMA_REQUIRE(update.has_value());
    math::Vec3 expected{1.0f, 1.0f, 1.0f};
    if (axis == 0u) expected.x = 1.5f;
    if (axis == 1u) expected.y = 1.5f;
    if (axis == 2u) expected.z = 1.5f;
    KARMA_REQUIRE(nearlyVec(update->local_transform.scale, expected, 0.003f));
  }

  const auto camera_basis = editor::viewportCameraBasis(projection.camera);
  KARMA_REQUIRE(camera_basis.has_value());
  const math::Vec3 diagonal = math::normalize(
      math::add(camera_basis->right, camera_basis->up));
  editor::GizmoDragState uniform;
  KARMA_REQUIRE(uniform.begin(
      dragDesc(editor::GizmoHandle::ScaleUniform, projection, transform,
               transform, geometry.world_size),
      screenPoint(projection, {})));
  const auto update = uniform.update(screenPoint(
      projection, math::scale(diagonal, geometry.world_size * 0.5f)));
  KARMA_REQUIRE(update.has_value());
  KARMA_REQUIRE(nearlyVec(update->local_transform.scale,
                          {1.5f, 1.5f, 1.5f}, 0.003f));
}

void testParentsCancellationAndSingleCommit() {
  scenes::SceneTransform parent{};
  parent.position = {10.0f, -2.0f, 3.0f};
  parent.rotation = axisAngle({0.0f, 0.0f, 1.0f}, 0.5f * kPi);
  parent.scale = {2.0f, 3.0f, 4.0f};
  scenes::SceneTransform local{};
  local.position = {1.0f, 2.0f, 0.5f};
  local.rotation = axisAngle({0.0f, 1.0f, 0.0f}, 0.35f);
  local.scale = {1.0f, 2.0f, 3.0f};
  const scenes::SceneTransform world = compose(parent, local);
  const editor::ViewportProjection projection =
      projectionFor(world.position, 12.0f, 0.58f, -0.24f);
  const editor::GizmoGeometry move_geometry =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            world, projection);

  editor::GizmoDragBegin move =
      dragDesc(editor::GizmoHandle::MoveX, projection, local, world,
               move_geometry.world_size);
  move.parent_world_transform = parent;
  const math::Vec3 start = math::add(
      world.position, math::scale(move_geometry.axes[0],
                                  move_geometry.world_size * 0.65f));
  const math::Vec3 end = math::add(start, {1.25f, 0.0f, 0.0f});
  editor::GizmoDragState drag;
  KARMA_REQUIRE(drag.begin(move, screenPoint(projection, start)));
  const auto update = drag.update(screenPoint(projection, end));
  KARMA_REQUIRE(update.has_value());
  KARMA_REQUIRE(nearlyVec(update->world_transform.position,
                          math::add(world.position, {1.25f, 0.0f, 0.0f}),
                          0.004f));
  KARMA_REQUIRE(nearlyVec(compose(parent, update->local_transform).position,
                          update->world_transform.position, 0.004f));
  KARMA_REQUIRE(nearlyRotation(update->local_transform.rotation,
                               local.rotation));
  KARMA_REQUIRE(nearlyVec(update->local_transform.scale, local.scale));

  const auto cancelled = drag.cancel();
  KARMA_REQUIRE(cancelled.has_value());
  KARMA_REQUIRE(cancelled->cancelled);
  KARMA_REQUIRE(!cancelled->commit);
  KARMA_REQUIRE(nearlyVec(cancelled->after_local.position, local.position));
  KARMA_REQUIRE(nearlyVec(cancelled->after_world.position, world.position));
  KARMA_REQUIRE(!drag.cancel().has_value());
  KARMA_REQUIRE(!drag.finish().has_value());

  const editor::GizmoGeometry scale_geometry =
      build(editor::GizmoTool::Scale, editor::GizmoSpace::World,
            world, projection);
  // Scale axes remain the child's local axes, regardless of requested World.
  KARMA_REQUIRE(nearlyVec(scale_geometry.axes[0],
                          math::rotateVec(world.rotation, {1.0f, 0.0f, 0.0f})));
  editor::GizmoDragBegin scale =
      dragDesc(editor::GizmoHandle::ScaleX, projection, local, world,
               scale_geometry.world_size);
  scale.parent_world_transform = parent;
  const math::Vec3 scale_start = math::add(
      world.position, math::scale(scale_geometry.axes[0],
                                  scale_geometry.world_size * 0.65f));
  const math::Vec3 scale_end = math::add(
      scale_start, math::scale(scale_geometry.axes[0],
                               scale_geometry.world_size * 0.5f));
  editor::GizmoDragState scale_drag;
  KARMA_REQUIRE(scale_drag.begin(scale, screenPoint(projection, scale_start)));
  const auto scale_update =
      scale_drag.update(screenPoint(projection, scale_end));
  KARMA_REQUIRE(scale_update.has_value());
  KARMA_REQUIRE(nearlyVec(scale_update->local_transform.scale,
                          {1.5f, 2.0f, 3.0f}, 0.004f));
  KARMA_REQUIRE(nearlyVec(scale_update->world_transform.scale,
                          {3.0f, 6.0f, 12.0f}, 0.004f));

  const auto completed = scale_drag.finish();
  KARMA_REQUIRE(completed.has_value());
  KARMA_REQUIRE(completed->commit);
  KARMA_REQUIRE(!completed->cancelled);
  KARMA_REQUIRE(!scale_drag.finish().has_value());
}

void testParallelAndInvalidDragsStayFinite() {
  const editor::ViewportProjection projection = projectionFor();
  const scenes::SceneTransform transform{};
  const editor::GizmoGeometry geometry =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            transform, projection);
  editor::GizmoDragState parallel;
  const bool began = parallel.begin(
      dragDesc(editor::GizmoHandle::MoveZ, projection, transform, transform,
               geometry.world_size),
      screenPoint(projection, {}));
  KARMA_REQUIRE(!began);
  KARMA_REQUIRE(!parallel.active());
  KARMA_REQUIRE(!parallel.update(screenPoint(projection, {})).has_value());

  editor::GizmoDragBegin invalid =
      dragDesc(editor::GizmoHandle::MoveX, projection, transform, transform,
               geometry.world_size);
  invalid.world_size = 0.0f;
  editor::GizmoDragState rejected;
  KARMA_REQUIRE(!rejected.begin(invalid, screenPoint(projection, {})));

  // A very shallow but non-parallel axis is either safely constrained or
  // rejected; neither path may expose a non-finite transform.
  const editor::ViewportProjection shallow =
      projectionFor({}, 5.0f, 0.0005f, 0.0f);
  const editor::GizmoGeometry shallow_geometry =
      build(editor::GizmoTool::Move, editor::GizmoSpace::World,
            transform, shallow);
  editor::GizmoDragState shallow_drag;
  const math::Vec3 start = math::scale(shallow_geometry.axes[2], 0.5f);
  if (shallow_drag.begin(
          dragDesc(editor::GizmoHandle::MoveZ, shallow, transform, transform,
                   shallow_geometry.world_size),
          screenPoint(shallow, start))) {
    const auto update = shallow_drag.update(screenPoint(
        shallow, math::add(start, math::scale(shallow_geometry.axes[2], 0.2f))));
    if (update.has_value()) {
      KARMA_REQUIRE(finiteTransform(update->local_transform));
      KARMA_REQUIRE(finiteTransform(update->world_transform));
    }
  }
}

}  // namespace

int main() {
  testGeometryAndOrientation();
  testApparentSizingAndVisibility();
  testProjectedHandlePicking();
  testEveryMoveHandleAndSnapping();
  testEveryRotationHandleAndSnapping();
  testEveryScaleHandleAndSnapping();
  testParentsCancellationAndSingleCommit();
  testParallelAndInvalidDragsStayFinite();
  std::cout << "scene editor gizmo tests passed\n";
  return 0;
}
