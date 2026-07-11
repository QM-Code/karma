#include "scene_editor_gizmo.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace karma::tools::scene_editor {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kEpsilon = 1.0e-6f;
constexpr int kRingSegments = 64;
constexpr int kCenterSegments = 20;

constexpr math::Vec3 kBasisX{1.0f, 0.0f, 0.0f};
constexpr math::Vec3 kBasisY{0.0f, 1.0f, 0.0f};
constexpr math::Vec3 kBasisZ{0.0f, 0.0f, 1.0f};

constexpr math::Color kAxisXColor{0.95f, 0.22f, 0.22f, 1.0f};
constexpr math::Color kAxisYColor{0.35f, 0.9f, 0.28f, 1.0f};
constexpr math::Color kAxisZColor{0.25f, 0.5f, 1.0f, 1.0f};
constexpr math::Color kViewColor{0.95f, 0.9f, 0.35f, 1.0f};

bool finiteTransform(const scenes::SceneTransform& transform) {
  return math::isFinite(transform.position) &&
         math::isFinite(transform.rotation) &&
         math::isFinite(transform.scale) &&
         math::lengthSquared(transform.rotation) > kEpsilon * kEpsilon;
}

math::Vec3 addScaled(const math::Vec3& value,
                     const math::Vec3& direction,
                     float scale) {
  return math::add(value, math::scale(direction, scale));
}

float component(const math::Vec3& value, int axis) {
  switch (axis) {
    case 0:
      return value.x;
    case 1:
      return value.y;
    default:
      return value.z;
  }
}

void setComponent(math::Vec3& value, int axis, float component_value) {
  switch (axis) {
    case 0:
      value.x = component_value;
      break;
    case 1:
      value.y = component_value;
      break;
    default:
      value.z = component_value;
      break;
  }
}

math::Color axisColor(int axis) {
  switch (axis) {
    case 0:
      return kAxisXColor;
    case 1:
      return kAxisYColor;
    default:
      return kAxisZColor;
  }
}

GizmoHandleGeometry& ensureHandle(GizmoGeometry& geometry,
                                  GizmoHandle handle,
                                  float pick_radius_pixels = 7.0f) {
  const auto it = std::find_if(
      geometry.handles.begin(), geometry.handles.end(),
      [handle](const GizmoHandleGeometry& candidate) {
        return candidate.handle == handle;
      });
  if (it != geometry.handles.end()) {
    return *it;
  }
  geometry.handles.push_back(GizmoHandleGeometry{
      .handle = handle,
      .segments = {},
      .polygon = {},
      .pick_radius_pixels = pick_radius_pixels,
  });
  return geometry.handles.back();
}

void addLine(GizmoGeometry& geometry,
             GizmoHandle handle,
             const math::Vec3& start,
             const math::Vec3& end,
             const math::Color& color,
             float thickness_pixels = 2.0f,
             bool pickable = true) {
  const GizmoLineSegment line{
      .start = start,
      .end = end,
      .color = color,
      .handle = handle,
      .thickness_pixels = thickness_pixels,
  };
  geometry.lines.push_back(line);
  if (pickable) {
    ensureHandle(geometry, handle).segments.push_back(line);
  }
}

void setPolygon(GizmoGeometry& geometry,
                GizmoHandle handle,
                std::vector<math::Vec3> polygon,
                float pick_radius_pixels = 7.0f) {
  GizmoHandleGeometry& hit =
      ensureHandle(geometry, handle, pick_radius_pixels);
  hit.polygon = std::move(polygon);
  hit.pick_radius_pixels = pick_radius_pixels;
}

std::array<math::Vec3, 3> gizmoAxes(
    const scenes::SceneTransform& world_transform,
    GizmoTool tool,
    GizmoSpace space) {
  const bool local = space == GizmoSpace::Local || tool == GizmoTool::Scale;
  if (!local) {
    return {kBasisX, kBasisY, kBasisZ};
  }
  const math::Quat rotation = math::normalize(world_transform.rotation);
  return {
      math::normalize(math::rotateVec(rotation, kBasisX)),
      math::normalize(math::rotateVec(rotation, kBasisY)),
      math::normalize(math::rotateVec(rotation, kBasisZ)),
  };
}

void perpendicularBasis(const math::Vec3& axis,
                        math::Vec3& out_a,
                        math::Vec3& out_b) {
  const math::Vec3 helper = std::abs(axis.y) < 0.8f ? kBasisY : kBasisX;
  out_a = math::normalize(math::cross(axis, helper));
  out_b = math::normalize(math::cross(axis, out_a));
}

void addArrow(GizmoGeometry& geometry,
              GizmoHandle handle,
              const math::Vec3& pivot,
              const math::Vec3& axis,
              float size,
              const math::Color& color) {
  const math::Vec3 tip = addScaled(pivot, axis, size);
  const math::Vec3 base = addScaled(pivot, axis, size * 0.77f);
  math::Vec3 side_a{};
  math::Vec3 side_b{};
  perpendicularBasis(axis, side_a, side_b);
  const float radius = size * 0.075f;
  addLine(geometry, handle, pivot, tip, color, 2.4f);
  addLine(geometry, handle, tip, addScaled(base, side_a, radius), color, 2.4f);
  addLine(geometry, handle, tip, addScaled(base, side_a, -radius), color, 2.4f);
  addLine(geometry, handle, tip, addScaled(base, side_b, radius), color, 2.4f);
  addLine(geometry, handle, tip, addScaled(base, side_b, -radius), color, 2.4f);
}

void addPlaneHandle(GizmoGeometry& geometry,
                    GizmoHandle handle,
                    const math::Vec3& pivot,
                    const math::Vec3& axis_a,
                    const math::Vec3& axis_b,
                    float size,
                    const math::Color& color) {
  const float inner = size * 0.2f;
  const float outer = size * 0.42f;
  const math::Vec3 p0 = addScaled(addScaled(pivot, axis_a, inner), axis_b, inner);
  const math::Vec3 p1 = addScaled(addScaled(pivot, axis_a, outer), axis_b, inner);
  const math::Vec3 p2 = addScaled(addScaled(pivot, axis_a, outer), axis_b, outer);
  const math::Vec3 p3 = addScaled(addScaled(pivot, axis_a, inner), axis_b, outer);
  addLine(geometry, handle, p0, p1, color, 1.8f);
  addLine(geometry, handle, p1, p2, color, 1.8f);
  addLine(geometry, handle, p2, p3, color, 1.8f);
  addLine(geometry, handle, p3, p0, color, 1.8f);
  setPolygon(geometry, handle, {p0, p1, p2, p3}, 5.0f);
}

void addCircle(GizmoGeometry& geometry,
               GizmoHandle handle,
               const math::Vec3& center,
               const math::Vec3& axis_a,
               const math::Vec3& axis_b,
               float radius,
               const math::Color& color,
               int segments,
               float thickness_pixels,
               bool center_polygon) {
  std::vector<math::Vec3> points;
  points.reserve(static_cast<size_t>(segments));
  for (int index = 0; index < segments; ++index) {
    const float angle = (2.0f * kPi * static_cast<float>(index)) /
                        static_cast<float>(segments);
    points.push_back(addScaled(
        addScaled(center, axis_a, radius * std::cos(angle)),
        axis_b, radius * std::sin(angle)));
  }
  for (int index = 0; index < segments; ++index) {
    addLine(geometry, handle, points[static_cast<size_t>(index)],
            points[static_cast<size_t>((index + 1) % segments)], color,
            thickness_pixels);
  }
  if (center_polygon) {
    setPolygon(geometry, handle, std::move(points), 5.0f);
  }
}

void addBox(GizmoGeometry& geometry,
            GizmoHandle handle,
            const math::Vec3& center,
            const std::array<math::Vec3, 3>& axes,
            float half_extent,
            const math::Color& color) {
  std::array<math::Vec3, 8> corners{};
  for (int index = 0; index < 8; ++index) {
    math::Vec3 point = center;
    point = addScaled(point, axes[0], (index & 1) ? half_extent : -half_extent);
    point = addScaled(point, axes[1], (index & 2) ? half_extent : -half_extent);
    point = addScaled(point, axes[2], (index & 4) ? half_extent : -half_extent);
    corners[static_cast<size_t>(index)] = point;
  }
  constexpr std::array<std::array<int, 2>, 12> edges{{
      {{0, 1}}, {{2, 3}}, {{4, 5}}, {{6, 7}},
      {{0, 2}}, {{1, 3}}, {{4, 6}}, {{5, 7}},
      {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
  }};
  for (const auto& edge : edges) {
    addLine(geometry, handle, corners[static_cast<size_t>(edge[0])],
            corners[static_cast<size_t>(edge[1])], color, 2.0f);
  }
}

void addCenterSquareHit(GizmoGeometry& geometry,
                        GizmoHandle handle,
                        const math::Vec3& pivot,
                        const ViewportCameraBasis& basis,
                        float half_extent) {
  setPolygon(
      geometry, handle,
      {
          addScaled(addScaled(pivot, basis.right, -half_extent), basis.up,
                    -half_extent),
          addScaled(addScaled(pivot, basis.right, half_extent), basis.up,
                    -half_extent),
          addScaled(addScaled(pivot, basis.right, half_extent), basis.up,
                    half_extent),
          addScaled(addScaled(pivot, basis.right, -half_extent), basis.up,
                    half_extent),
      },
      5.0f);
}

float distanceSquared(const ViewportPoint& a, const ViewportPoint& b) {
  const float x = a.x - b.x;
  const float y = a.y - b.y;
  return x * x + y * y;
}

float pointSegmentDistance(ViewportPoint point,
                           ViewportPoint start,
                           ViewportPoint end) {
  const float dx = end.x - start.x;
  const float dy = end.y - start.y;
  const float length_squared = dx * dx + dy * dy;
  if (length_squared <= kEpsilon) {
    return std::sqrt(distanceSquared(point, start));
  }
  const float amount = std::clamp(
      ((point.x - start.x) * dx + (point.y - start.y) * dy) /
          length_squared,
      0.0f, 1.0f);
  const ViewportPoint closest{start.x + dx * amount, start.y + dy * amount};
  return std::sqrt(distanceSquared(point, closest));
}

bool pointInPolygon(ViewportPoint point,
                    const std::vector<ViewportPoint>& polygon) {
  bool inside = false;
  if (polygon.size() < 3u) {
    return false;
  }
  size_t previous = polygon.size() - 1u;
  for (size_t index = 0; index < polygon.size(); ++index) {
    const ViewportPoint a = polygon[index];
    const ViewportPoint b = polygon[previous];
    const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
                         (point.x < (b.x - a.x) * (point.y - a.y) /
                                            (b.y - a.y) +
                                        a.x);
    if (crosses) {
      inside = !inside;
    }
    previous = index;
  }
  return inside;
}

std::optional<ViewportPoint> projectHitPoint(
    const ViewportProjection& projection,
    const math::Vec3& world_point) {
  const auto projected = projectWorldToViewport(projection, world_point);
  if (!projected.has_value() || !projected->inside_clip) {
    return std::nullopt;
  }
  return projected->screen;
}

int hitPriority(GizmoHandle handle) {
  switch (handle) {
    case GizmoHandle::MoveView:
    case GizmoHandle::ScaleUniform:
      return 0;
    case GizmoHandle::MoveXY:
    case GizmoHandle::MoveXZ:
    case GizmoHandle::MoveYZ:
      return 1;
    case GizmoHandle::MoveX:
    case GizmoHandle::MoveY:
    case GizmoHandle::MoveZ:
    case GizmoHandle::ScaleX:
    case GizmoHandle::ScaleY:
    case GizmoHandle::ScaleZ:
      return 2;
    case GizmoHandle::RotateView:
      return 3;
    case GizmoHandle::RotateX:
    case GizmoHandle::RotateY:
    case GizmoHandle::RotateZ:
      return 4;
    default:
      return 5;
  }
}

GizmoTool toolForHandle(GizmoHandle handle) {
  switch (handle) {
    case GizmoHandle::MoveX:
    case GizmoHandle::MoveY:
    case GizmoHandle::MoveZ:
    case GizmoHandle::MoveXY:
    case GizmoHandle::MoveXZ:
    case GizmoHandle::MoveYZ:
    case GizmoHandle::MoveView:
      return GizmoTool::Move;
    case GizmoHandle::RotateX:
    case GizmoHandle::RotateY:
    case GizmoHandle::RotateZ:
    case GizmoHandle::RotateView:
      return GizmoTool::Rotate;
    default:
      return GizmoTool::Scale;
  }
}

int axisIndex(GizmoHandle handle) {
  switch (handle) {
    case GizmoHandle::MoveX:
    case GizmoHandle::RotateX:
    case GizmoHandle::ScaleX:
      return 0;
    case GizmoHandle::MoveY:
    case GizmoHandle::RotateY:
    case GizmoHandle::ScaleY:
      return 1;
    case GizmoHandle::MoveZ:
    case GizmoHandle::RotateZ:
    case GizmoHandle::ScaleZ:
      return 2;
    default:
      return -1;
  }
}

bool isMoveAxis(GizmoHandle handle) {
  return handle == GizmoHandle::MoveX || handle == GizmoHandle::MoveY ||
         handle == GizmoHandle::MoveZ;
}

bool isScaleAxis(GizmoHandle handle) {
  return handle == GizmoHandle::ScaleX || handle == GizmoHandle::ScaleY ||
         handle == GizmoHandle::ScaleZ;
}

bool isRotation(GizmoHandle handle) {
  return handle == GizmoHandle::RotateX ||
         handle == GizmoHandle::RotateY ||
         handle == GizmoHandle::RotateZ ||
         handle == GizmoHandle::RotateView;
}

bool intersectPlane(const ViewportRay& ray,
                    const math::Vec3& plane_point,
                    const math::Vec3& plane_normal,
                    math::Vec3& out_point) {
  const float denominator = math::dot(ray.direction, plane_normal);
  if (!std::isfinite(denominator) || std::abs(denominator) <= 1.0e-6f) {
    return false;
  }
  const float distance =
      math::dot(math::subtract(plane_point, ray.origin), plane_normal) /
      denominator;
  if (!std::isfinite(distance) || distance < 0.0f) {
    return false;
  }
  out_point = addScaled(ray.origin, ray.direction, distance);
  return math::isFinite(out_point);
}

bool closestAxisParameter(const ViewportRay& ray,
                          const math::Vec3& axis_origin,
                          const math::Vec3& axis,
                          float& out_parameter) {
  const math::Vec3 offset = math::subtract(ray.origin, axis_origin);
  const float ray_length_squared = math::dot(ray.direction, ray.direction);
  const float axis_length_squared = math::dot(axis, axis);
  const float mixed = math::dot(ray.direction, axis);
  const float denominator =
      ray_length_squared * axis_length_squared - mixed * mixed;
  if (!std::isfinite(denominator) || std::abs(denominator) <= 1.0e-5f) {
    return false;
  }
  const float ray_offset = math::dot(ray.direction, offset);
  const float axis_offset = math::dot(axis, offset);
  out_parameter =
      (ray_length_squared * axis_offset - mixed * ray_offset) / denominator;
  return std::isfinite(out_parameter);
}

math::Vec3 axisDragPlaneNormal(const math::Vec3& axis,
                               const math::Vec3& camera_position,
                               const math::Vec3& pivot,
                               const ViewportCameraBasis& camera_basis) {
  const math::Vec3 to_camera =
      math::normalize(math::subtract(camera_position, pivot));
  math::Vec3 normal = math::subtract(
      to_camera, math::scale(axis, math::dot(to_camera, axis)));
  normal = math::normalize(normal);
  if (math::lengthSquared(normal) > kEpsilon * kEpsilon) {
    return normal;
  }

  const math::Vec3 right = math::subtract(
      camera_basis.right,
      math::scale(axis, math::dot(camera_basis.right, axis)));
  const math::Vec3 up = math::subtract(
      camera_basis.up, math::scale(axis, math::dot(camera_basis.up, axis)));
  normal = math::lengthSquared(right) > math::lengthSquared(up)
               ? math::normalize(right)
               : math::normalize(up);
  return normal;
}

float snapDelta(float value, float step, bool enabled) {
  if (!enabled || !std::isfinite(step) || step <= kEpsilon) {
    return value;
  }
  return std::round(value / step) * step;
}

math::Quat axisAngle(const math::Vec3& axis, float angle) {
  const math::Vec3 normalized_axis = math::normalize(axis);
  const float half_angle = angle * 0.5f;
  const float sine = std::sin(half_angle);
  return math::normalize({normalized_axis.x * sine,
                          normalized_axis.y * sine,
                          normalized_axis.z * sine,
                          std::cos(half_angle)});
}

scenes::SceneTransform composeTransforms(
    const scenes::SceneTransform& parent,
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

math::Vec3 localPositionForWorld(
    const scenes::SceneTransform& parent,
    const math::Vec3& world_position,
    const math::Vec3& fallback_local_position) {
  const math::Vec3 unrotated = math::rotateVec(
      math::inverse(parent.rotation),
      math::subtract(world_position, parent.position));
  math::Vec3 local = fallback_local_position;
  if (std::abs(parent.scale.x) > kEpsilon) {
    local.x = unrotated.x / parent.scale.x;
  }
  if (std::abs(parent.scale.y) > kEpsilon) {
    local.y = unrotated.y / parent.scale.y;
  }
  if (std::abs(parent.scale.z) > kEpsilon) {
    local.z = unrotated.z / parent.scale.z;
  }
  return local;
}

void applyWorldPosition(const GizmoDragBegin& desc,
                        const math::Vec3& position,
                        GizmoDragUpdate& update) {
  update.local_transform = desc.local_transform;
  update.world_transform = desc.world_transform;
  if (desc.parent_world_transform.has_value()) {
    update.local_transform.position = localPositionForWorld(
        *desc.parent_world_transform, position, desc.local_transform.position);
    update.world_transform =
        composeTransforms(*desc.parent_world_transform, update.local_transform);
  } else {
    update.local_transform.position = position;
    update.world_transform = update.local_transform;
  }
}

void applyWorldRotation(const GizmoDragBegin& desc,
                        const math::Quat& rotation,
                        GizmoDragUpdate& update) {
  update.local_transform = desc.local_transform;
  update.world_transform = desc.world_transform;
  const math::Quat normalized_rotation = math::normalize(rotation);
  if (desc.parent_world_transform.has_value()) {
    update.local_transform.rotation = math::normalize(math::mul(
        math::inverse(desc.parent_world_transform->rotation),
        normalized_rotation));
    update.world_transform =
        composeTransforms(*desc.parent_world_transform, update.local_transform);
  } else {
    update.local_transform.rotation = normalized_rotation;
    update.world_transform = update.local_transform;
  }
}

void applyLocalScale(const GizmoDragBegin& desc,
                     const math::Vec3& scale,
                     GizmoDragUpdate& update) {
  update.local_transform = desc.local_transform;
  update.local_transform.scale = scale;
  update.world_transform = desc.parent_world_transform.has_value()
                               ? composeTransforms(*desc.parent_world_transform,
                                                   update.local_transform)
                               : update.local_transform;
}

bool sameTransform(const scenes::SceneTransform& a,
                   const scenes::SceneTransform& b) {
  constexpr float kTransformEpsilon = 1.0e-5f;
  const auto near = [](float first, float second) {
    return std::abs(first - second) <= kTransformEpsilon;
  };
  const math::Quat rotation_a = math::normalize(a.rotation);
  const math::Quat rotation_b = math::normalize(b.rotation);
  return near(a.position.x, b.position.x) &&
         near(a.position.y, b.position.y) &&
         near(a.position.z, b.position.z) &&
         near(a.scale.x, b.scale.x) && near(a.scale.y, b.scale.y) &&
         near(a.scale.z, b.scale.z) &&
         std::abs(math::dot(rotation_a, rotation_b)) >=
             1.0f - kTransformEpsilon;
}

}  // namespace

GizmoGeometry buildGizmoGeometry(const GizmoBuildDesc& desc) {
  GizmoGeometry geometry{};
  geometry.tool = desc.tool;
  geometry.space = desc.space;
  geometry.pivot = desc.world_transform.position;
  geometry.axes = gizmoAxes(desc.world_transform, desc.tool, desc.space);

  if (!finiteTransform(desc.world_transform) ||
      !validViewportProjection(desc.projection)) {
    return geometry;
  }
  const auto pivot_projection =
      projectWorldToViewport(desc.projection, geometry.pivot);
  if (!pivot_projection.has_value() || !pivot_projection->inside_clip) {
    return geometry;
  }
  const float pixel_world_size =
      worldUnitsPerViewportPixel(desc.projection, geometry.pivot);
  const float apparent_size =
      std::isfinite(desc.apparent_size_pixels) && desc.apparent_size_pixels > 0.0f
          ? desc.apparent_size_pixels
          : 96.0f;
  geometry.world_size = pixel_world_size * apparent_size;
  if (!std::isfinite(geometry.world_size) || geometry.world_size <= kEpsilon) {
    geometry.world_size = 0.0f;
    return geometry;
  }

  const auto camera_basis = viewportCameraBasis(desc.projection.camera);
  if (!camera_basis.has_value()) {
    geometry.world_size = 0.0f;
    return geometry;
  }
  const float size = geometry.world_size;

  if (desc.tool == GizmoTool::Move) {
    addArrow(geometry, GizmoHandle::MoveX, geometry.pivot, geometry.axes[0],
             size, kAxisXColor);
    addArrow(geometry, GizmoHandle::MoveY, geometry.pivot, geometry.axes[1],
             size, kAxisYColor);
    addArrow(geometry, GizmoHandle::MoveZ, geometry.pivot, geometry.axes[2],
             size, kAxisZColor);
    addPlaneHandle(geometry, GizmoHandle::MoveXY, geometry.pivot,
                   geometry.axes[0], geometry.axes[1], size,
                   math::Color{0.85f, 0.75f, 0.2f, 1.0f});
    addPlaneHandle(geometry, GizmoHandle::MoveXZ, geometry.pivot,
                   geometry.axes[0], geometry.axes[2], size,
                   math::Color{0.85f, 0.25f, 0.75f, 1.0f});
    addPlaneHandle(geometry, GizmoHandle::MoveYZ, geometry.pivot,
                   geometry.axes[1], geometry.axes[2], size,
                   math::Color{0.2f, 0.8f, 0.8f, 1.0f});
    addCircle(geometry, GizmoHandle::MoveView, geometry.pivot,
              camera_basis->right, camera_basis->up, size * 0.105f,
              kViewColor, kCenterSegments, 2.2f, true);
  } else if (desc.tool == GizmoTool::Rotate) {
    addCircle(geometry, GizmoHandle::RotateX, geometry.pivot,
              geometry.axes[1], geometry.axes[2], size * 0.82f,
              kAxisXColor, kRingSegments, 2.2f, false);
    addCircle(geometry, GizmoHandle::RotateY, geometry.pivot,
              geometry.axes[2], geometry.axes[0], size * 0.82f,
              kAxisYColor, kRingSegments, 2.2f, false);
    addCircle(geometry, GizmoHandle::RotateZ, geometry.pivot,
              geometry.axes[0], geometry.axes[1], size * 0.82f,
              kAxisZColor, kRingSegments, 2.2f, false);
    addCircle(geometry, GizmoHandle::RotateView, geometry.pivot,
              camera_basis->right, camera_basis->up, size * 1.08f,
              kViewColor, kRingSegments, 2.4f, false);
  } else {
    for (int axis = 0; axis < 3; ++axis) {
      const GizmoHandle handle = axis == 0   ? GizmoHandle::ScaleX
                                 : axis == 1 ? GizmoHandle::ScaleY
                                             : GizmoHandle::ScaleZ;
      const math::Vec3 endpoint =
          addScaled(geometry.pivot, geometry.axes[static_cast<size_t>(axis)],
                    size * 0.88f);
      addLine(geometry, handle, geometry.pivot, endpoint, axisColor(axis),
              2.3f);
      addBox(geometry, handle, endpoint, geometry.axes, size * 0.065f,
             axisColor(axis));
    }
    addBox(geometry, GizmoHandle::ScaleUniform, geometry.pivot, geometry.axes,
           size * 0.1f, kViewColor);
    addCenterSquareHit(geometry, GizmoHandle::ScaleUniform, geometry.pivot,
                       *camera_basis, size * 0.115f);
  }
  return geometry;
}

std::optional<GizmoHit> hitTestGizmo(const GizmoGeometry& geometry,
                                     const ViewportProjection& projection,
                                     ViewportPoint cursor) {
  if (!validViewportProjection(projection) ||
      cursor.x < projection.rect.x || cursor.y < projection.rect.y ||
      cursor.x > projection.rect.x + projection.rect.width ||
      cursor.y > projection.rect.y + projection.rect.height) {
    return std::nullopt;
  }

  std::optional<GizmoHit> best;
  int best_priority = std::numeric_limits<int>::max();
  for (const GizmoHandleGeometry& handle : geometry.handles) {
    float distance = std::numeric_limits<float>::infinity();
    for (const GizmoLineSegment& segment : handle.segments) {
      const auto start = projectHitPoint(projection, segment.start);
      const auto end = projectHitPoint(projection, segment.end);
      if (!start.has_value() || !end.has_value()) {
        continue;
      }
      distance = std::min(distance,
                          pointSegmentDistance(cursor, *start, *end));
    }

    if (handle.polygon.size() >= 3u) {
      std::vector<ViewportPoint> polygon;
      polygon.reserve(handle.polygon.size());
      bool valid_polygon = true;
      for (const math::Vec3& point : handle.polygon) {
        const auto projected = projectHitPoint(projection, point);
        if (!projected.has_value()) {
          valid_polygon = false;
          break;
        }
        polygon.push_back(*projected);
      }
      if (valid_polygon) {
        if (pointInPolygon(cursor, polygon)) {
          distance = 0.0f;
        } else {
          for (size_t index = 0; index < polygon.size(); ++index) {
            distance = std::min(
                distance,
                pointSegmentDistance(
                    cursor, polygon[index],
                    polygon[(index + 1u) % polygon.size()]));
          }
        }
      }
    }

    if (distance > handle.pick_radius_pixels) {
      continue;
    }
    const int priority = hitPriority(handle.handle);
    if (!best.has_value() || distance < best->screen_distance_pixels - 0.01f ||
        (std::abs(distance - best->screen_distance_pixels) <= 0.01f &&
         priority < best_priority)) {
      best = GizmoHit{
          .handle = handle.handle,
          .screen_distance_pixels = distance,
      };
      best_priority = priority;
    }
  }
  return best;
}

bool GizmoDragState::begin(const GizmoDragBegin& desc,
                           ViewportPoint cursor) {
  if (active_ || desc.handle == GizmoHandle::None ||
      !validViewportProjection(desc.projection) ||
      !finiteTransform(desc.local_transform) ||
      !finiteTransform(desc.world_transform) ||
      (desc.parent_world_transform.has_value() &&
       !finiteTransform(*desc.parent_world_transform)) ||
      !std::isfinite(desc.world_size) || desc.world_size <= kEpsilon) {
    return false;
  }
  const auto ray = viewportPointToWorldRay(desc.projection, cursor);
  const auto camera_basis = viewportCameraBasis(desc.projection.camera);
  if (!ray.has_value() || !camera_basis.has_value()) {
    return false;
  }

  desc_ = desc;
  current_ = GizmoDragUpdate{
      .local_transform = desc.local_transform,
      .world_transform = desc.world_transform,
      .changed = false,
  };
  constraint_ = Constraint::None;
  axis_a_ = {};
  axis_b_ = {};
  plane_normal_ = {};
  start_point_ = {};
  start_vector_ = {};
  start_scalar_ = 0.0f;
  axis_uses_plane_ = false;
  axes_ = gizmoAxes(desc.world_transform, toolForHandle(desc.handle),
                    desc.space);
  const math::Vec3 pivot = desc.world_transform.position;
  const int axis_index = axisIndex(desc.handle);

  if (isMoveAxis(desc.handle) || isScaleAxis(desc.handle)) {
    axis_a_ = axes_[static_cast<size_t>(axis_index)];
    plane_normal_ = axisDragPlaneNormal(
        axis_a_, desc.projection.camera.position, pivot, *camera_basis);
    axis_uses_plane_ =
        math::lengthSquared(plane_normal_) > kEpsilon * kEpsilon &&
        intersectPlane(*ray, pivot, plane_normal_, start_point_);
    if (axis_uses_plane_) {
      start_scalar_ =
          math::dot(math::subtract(start_point_, pivot), axis_a_);
    } else if (!closestAxisParameter(*ray, pivot, axis_a_, start_scalar_)) {
      return false;
    }
    constraint_ = Constraint::Axis;
  } else if (desc.handle == GizmoHandle::MoveXY ||
             desc.handle == GizmoHandle::MoveXZ ||
             desc.handle == GizmoHandle::MoveYZ) {
    if (desc.handle == GizmoHandle::MoveXY) {
      axis_a_ = axes_[0];
      axis_b_ = axes_[1];
    } else if (desc.handle == GizmoHandle::MoveXZ) {
      axis_a_ = axes_[0];
      axis_b_ = axes_[2];
    } else {
      axis_a_ = axes_[1];
      axis_b_ = axes_[2];
    }
    plane_normal_ = math::normalize(math::cross(axis_a_, axis_b_));
    if (!intersectPlane(*ray, pivot, plane_normal_, start_point_)) {
      return false;
    }
    constraint_ = Constraint::Plane;
  } else if (desc.handle == GizmoHandle::MoveView) {
    axis_a_ = camera_basis->right;
    axis_b_ = camera_basis->up;
    plane_normal_ = camera_basis->forward;
    if (!intersectPlane(*ray, pivot, plane_normal_, start_point_)) {
      return false;
    }
    constraint_ = Constraint::Plane;
  } else if (isRotation(desc.handle)) {
    axis_a_ = desc.handle == GizmoHandle::RotateView
                  ? camera_basis->forward
                  : axes_[static_cast<size_t>(axis_index)];
    plane_normal_ = axis_a_;
    if (!intersectPlane(*ray, pivot, plane_normal_, start_point_)) {
      return false;
    }
    start_vector_ =
        math::normalize(math::subtract(start_point_, pivot));
    if (math::lengthSquared(start_vector_) <= kEpsilon * kEpsilon) {
      return false;
    }
    constraint_ = Constraint::Rotation;
  } else if (desc.handle == GizmoHandle::ScaleUniform) {
    plane_normal_ = camera_basis->forward;
    axis_a_ = math::normalize(math::add(camera_basis->right,
                                        camera_basis->up));
    if (!intersectPlane(*ray, pivot, plane_normal_, start_point_)) {
      return false;
    }
    constraint_ = Constraint::UniformScale;
  } else {
    return false;
  }

  active_ = true;
  completion_emitted_ = false;
  return true;
}

std::optional<GizmoDragUpdate> GizmoDragState::update(
    ViewportPoint cursor) {
  if (!active_) {
    return std::nullopt;
  }
  const auto ray = viewportPointToWorldRay(desc_.projection, cursor);
  if (!ray.has_value()) {
    return std::nullopt;
  }

  GizmoDragUpdate update{
      .local_transform = desc_.local_transform,
      .world_transform = desc_.world_transform,
      .changed = false,
  };
  const math::Vec3 pivot = desc_.world_transform.position;

  if (constraint_ == Constraint::Axis) {
    float scalar = 0.0f;
    if (axis_uses_plane_) {
      math::Vec3 point{};
      if (!intersectPlane(*ray, pivot, plane_normal_, point)) {
        return std::nullopt;
      }
      scalar = math::dot(math::subtract(point, pivot), axis_a_);
    } else if (!closestAxisParameter(*ray, pivot, axis_a_, scalar)) {
      return std::nullopt;
    }
    float delta = scalar - start_scalar_;
    if (isMoveAxis(desc_.handle)) {
      delta = snapDelta(delta, desc_.snap.translation_step,
                        desc_.snap.enabled);
      applyWorldPosition(desc_, addScaled(pivot, axis_a_, delta), update);
    } else {
      float scale_delta = delta / desc_.world_size;
      scale_delta = snapDelta(scale_delta, desc_.snap.scale_step,
                              desc_.snap.enabled);
      const float factor = 1.0f + scale_delta;
      math::Vec3 scale = desc_.local_transform.scale;
      const int axis_index = axisIndex(desc_.handle);
      setComponent(scale, axis_index,
                   component(scale, axis_index) * factor);
      applyLocalScale(desc_, scale, update);
    }
  } else if (constraint_ == Constraint::Plane) {
    math::Vec3 point{};
    if (!intersectPlane(*ray, pivot, plane_normal_, point)) {
      return std::nullopt;
    }
    const math::Vec3 raw_delta = math::subtract(point, start_point_);
    math::Vec3 delta{};
    if (desc_.handle == GizmoHandle::MoveView) {
      delta = math::add(
          math::scale(axis_a_, math::dot(raw_delta, axis_a_)),
          math::scale(axis_b_, math::dot(raw_delta, axis_b_)));
      if (desc_.snap.enabled) {
        delta.x = snapDelta(delta.x, desc_.snap.translation_step, true);
        delta.y = snapDelta(delta.y, desc_.snap.translation_step, true);
        delta.z = snapDelta(delta.z, desc_.snap.translation_step, true);
      }
    } else {
      const float amount_a = snapDelta(
          math::dot(raw_delta, axis_a_), desc_.snap.translation_step,
          desc_.snap.enabled);
      const float amount_b = snapDelta(
          math::dot(raw_delta, axis_b_), desc_.snap.translation_step,
          desc_.snap.enabled);
      delta = math::add(math::scale(axis_a_, amount_a),
                        math::scale(axis_b_, amount_b));
    }
    applyWorldPosition(desc_, math::add(pivot, delta), update);
  } else if (constraint_ == Constraint::Rotation) {
    math::Vec3 point{};
    if (!intersectPlane(*ray, pivot, plane_normal_, point)) {
      return std::nullopt;
    }
    const math::Vec3 vector =
        math::normalize(math::subtract(point, pivot));
    if (math::lengthSquared(vector) <= kEpsilon * kEpsilon) {
      return std::nullopt;
    }
    float angle = std::atan2(
        math::dot(axis_a_, math::cross(start_vector_, vector)),
        std::clamp(math::dot(start_vector_, vector), -1.0f, 1.0f));
    angle = snapDelta(
        angle, desc_.snap.rotation_step_degrees * kPi / 180.0f,
        desc_.snap.enabled);
    const math::Quat rotation = math::mul(
        axisAngle(axis_a_, angle), desc_.world_transform.rotation);
    applyWorldRotation(desc_, rotation, update);
  } else if (constraint_ == Constraint::UniformScale) {
    math::Vec3 point{};
    if (!intersectPlane(*ray, pivot, plane_normal_, point)) {
      return std::nullopt;
    }
    float scale_delta =
        math::dot(math::subtract(point, start_point_), axis_a_) /
        desc_.world_size;
    scale_delta = snapDelta(scale_delta, desc_.snap.scale_step,
                            desc_.snap.enabled);
    const float factor = 1.0f + scale_delta;
    applyLocalScale(desc_, math::scale(desc_.local_transform.scale, factor),
                    update);
  } else {
    return std::nullopt;
  }

  if (!finiteTransform(update.local_transform) ||
      !finiteTransform(update.world_transform)) {
    return std::nullopt;
  }
  update.changed = !sameTransform(update.local_transform,
                                  desc_.local_transform);
  current_ = update;
  return current_;
}

std::optional<GizmoDragCompletion> GizmoDragState::finish() {
  if (!active_ || completion_emitted_) {
    return std::nullopt;
  }
  active_ = false;
  completion_emitted_ = true;
  return GizmoDragCompletion{
      .before_local = desc_.local_transform,
      .before_world = desc_.world_transform,
      .after_local = current_.local_transform,
      .after_world = current_.world_transform,
      .changed = current_.changed,
      .commit = current_.changed,
      .cancelled = false,
  };
}

std::optional<GizmoDragCompletion> GizmoDragState::cancel() {
  if (!active_ || completion_emitted_) {
    return std::nullopt;
  }
  const bool was_changed = current_.changed;
  current_ = GizmoDragUpdate{
      .local_transform = desc_.local_transform,
      .world_transform = desc_.world_transform,
      .changed = false,
  };
  active_ = false;
  completion_emitted_ = true;
  return GizmoDragCompletion{
      .before_local = desc_.local_transform,
      .before_world = desc_.world_transform,
      .after_local = desc_.local_transform,
      .after_world = desc_.world_transform,
      .changed = was_changed,
      .commit = false,
      .cancelled = true,
  };
}

}  // namespace karma::tools::scene_editor
