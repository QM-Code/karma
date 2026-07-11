#include "scene_editor_colliders.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace karma::tools::scene_editor {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinimumExtent = 1.0e-5f;
constexpr uint32_t kMinimumSegments = 8u;
constexpr uint32_t kMaximumSegments = 64u;

class WireBuilder {
 public:
  WireBuilder(const scenes::SceneTransform& transform, uint32_t curve_segments)
      : transform_(transform),
        curve_segments_(std::clamp(curve_segments,
                                   kMinimumSegments,
                                   kMaximumSegments)) {}

  ColliderWireGeometry finish() { return std::move(geometry_); }

  void line(const math::Vec3& from, const math::Vec3& to) {
    const math::Vec3 world_from = point(from);
    const math::Vec3 world_to = point(to);
    if (!math::isFinite(world_from) || !math::isFinite(world_to)) return;
    geometry_.lines.push_back({.from = world_from, .to = world_to});
  }

  void circle(const math::Vec3& center,
              const math::Vec3& axis_u,
              const math::Vec3& axis_v,
              float radius) {
    radius = finiteMagnitude(radius);
    if (radius <= kMinimumExtent) return;
    for (uint32_t index = 0u; index < curve_segments_; ++index) {
      const float angle_a = 2.0f * kPi * static_cast<float>(index) /
                            static_cast<float>(curve_segments_);
      const float angle_b = 2.0f * kPi * static_cast<float>(index + 1u) /
                            static_cast<float>(curve_segments_);
      line(circlePoint(center, axis_u, axis_v, radius, angle_a),
           circlePoint(center, axis_u, axis_v, radius, angle_b));
    }
  }

  uint32_t curveSegments() const { return curve_segments_; }

 private:
  static float finiteMagnitude(float value) {
    return std::isfinite(value) ? std::abs(value) : 0.0f;
  }

  static math::Vec3 circlePoint(const math::Vec3& center,
                                const math::Vec3& axis_u,
                                const math::Vec3& axis_v,
                                float radius,
                                float angle) {
    return math::add(
        center,
        math::add(math::scale(axis_u, radius * std::cos(angle)),
                  math::scale(axis_v, radius * std::sin(angle))));
  }

  math::Vec3 point(const math::Vec3& local) const {
    const math::Vec3 scaled{
        local.x * transform_.scale.x,
        local.y * transform_.scale.y,
        local.z * transform_.scale.z,
    };
    return math::add(transform_.position,
                     math::rotateVec(transform_.rotation, scaled));
  }

  const scenes::SceneTransform& transform_;
  uint32_t curve_segments_ = 24u;
  ColliderWireGeometry geometry_{};
};

float magnitude(float value) {
  return std::isfinite(value) ? std::abs(value) : 0.0f;
}

math::Vec3 finiteMagnitude(const math::Vec3& value) {
  return {
      magnitude(value.x),
      magnitude(value.y),
      magnitude(value.z),
  };
}

void appendBox(WireBuilder& builder,
               const math::Vec3& center,
               const math::Vec3& half_extents) {
  const math::Vec3 half = finiteMagnitude(half_extents);
  const std::array<math::Vec3, 8> corners{{
      {center.x - half.x, center.y - half.y, center.z - half.z},
      {center.x + half.x, center.y - half.y, center.z - half.z},
      {center.x + half.x, center.y + half.y, center.z - half.z},
      {center.x - half.x, center.y + half.y, center.z - half.z},
      {center.x - half.x, center.y - half.y, center.z + half.z},
      {center.x + half.x, center.y - half.y, center.z + half.z},
      {center.x + half.x, center.y + half.y, center.z + half.z},
      {center.x - half.x, center.y + half.y, center.z + half.z},
  }};
  constexpr std::array<std::array<uint8_t, 2>, 12> edges{{
      {0u, 1u}, {1u, 2u}, {2u, 3u}, {3u, 0u},
      {4u, 5u}, {5u, 6u}, {6u, 7u}, {7u, 4u},
      {0u, 4u}, {1u, 5u}, {2u, 6u}, {3u, 7u},
  }};
  for (const auto& edge : edges) builder.line(corners[edge[0]], corners[edge[1]]);
}

void appendSphere(WireBuilder& builder,
                  const math::Vec3& center,
                  float radius) {
  radius = magnitude(radius);
  builder.circle(center, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, radius);
  builder.circle(center, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, radius);
  builder.circle(center, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, radius);
}

void appendCylinder(WireBuilder& builder,
                    const math::Vec3& center,
                    float radius,
                    float height) {
  radius = magnitude(radius);
  const float half_height = magnitude(height) * 0.5f;
  const math::Vec3 bottom{center.x, center.y - half_height, center.z};
  const math::Vec3 top{center.x, center.y + half_height, center.z};
  builder.circle(bottom, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, radius);
  builder.circle(top, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, radius);
  constexpr std::array<math::Vec3, 4> directions{{
      {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
  }};
  for (const math::Vec3& direction : directions) {
    builder.line(math::add(bottom, math::scale(direction, radius)),
                 math::add(top, math::scale(direction, radius)));
  }
}

void appendCapsuleProfile(WireBuilder& builder,
                          const math::Vec3& center,
                          float bottom_radius,
                          float top_radius,
                          float height) {
  bottom_radius = magnitude(bottom_radius);
  top_radius = magnitude(top_radius);
  const float half_height = magnitude(height) * 0.5f;
  // Karma collider heights describe the full end-to-end shape. Clamp the
  // sphere centers together when the requested height is shorter than the
  // combined caps.
  const float center_half_distance =
      std::max(0.0f, half_height - 0.5f * (bottom_radius + top_radius));
  const math::Vec3 bottom{center.x, center.y - center_half_distance, center.z};
  const math::Vec3 top{center.x, center.y + center_half_distance, center.z};
  builder.circle(bottom, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, bottom_radius);
  builder.circle(top, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, top_radius);

  constexpr std::array<float, 4> profile_angles{{0.0f, 0.5f * kPi, kPi, 1.5f * kPi}};
  const uint32_t arc_segments = std::max(4u, builder.curveSegments() / 2u);
  for (float around : profile_angles) {
    const math::Vec3 radial{std::cos(around), 0.0f, std::sin(around)};
    builder.line(math::add(bottom, math::scale(radial, bottom_radius)),
                 math::add(top, math::scale(radial, top_radius)));
    math::Vec3 previous = math::add(bottom, math::scale(radial, bottom_radius));
    for (uint32_t index = 1u; index <= arc_segments; ++index) {
      const float angle = kPi * 0.5f + kPi * static_cast<float>(index) /
                                           static_cast<float>(arc_segments);
      const math::Vec3 next = math::add(
          bottom,
          math::add(math::scale(radial, bottom_radius * std::cos(angle)),
                    math::Vec3{0.0f, bottom_radius * std::sin(angle), 0.0f}));
      builder.line(previous, next);
      previous = next;
    }
    previous = math::add(top, math::scale(radial, top_radius));
    for (uint32_t index = 1u; index <= arc_segments; ++index) {
      const float angle = -kPi * 0.5f + kPi * static_cast<float>(index) /
                                            static_cast<float>(arc_segments);
      const math::Vec3 next = math::add(
          top,
          math::add(math::scale(radial, top_radius * std::cos(angle)),
                    math::Vec3{0.0f, top_radius * std::sin(angle), 0.0f}));
      builder.line(previous, next);
      previous = next;
    }
  }
}

void appendMeshBounds(WireBuilder& builder,
                      const components::MeshColliderShape& mesh) {
  math::Vec3 minimum{-0.5f, -0.5f, -0.5f};
  math::Vec3 maximum{0.5f, 0.5f, 0.5f};
  bool have_bounds = false;
  for (const math::Vec3& vertex : mesh.vertices) {
    if (!math::isFinite(vertex)) continue;
    if (!have_bounds) {
      minimum = maximum = vertex;
      have_bounds = true;
      continue;
    }
    minimum.x = std::min(minimum.x, vertex.x);
    minimum.y = std::min(minimum.y, vertex.y);
    minimum.z = std::min(minimum.z, vertex.z);
    maximum.x = std::max(maximum.x, vertex.x);
    maximum.y = std::max(maximum.y, vertex.y);
    maximum.z = std::max(maximum.z, vertex.z);
  }
  appendBox(builder,
            math::scale(math::add(minimum, maximum), 0.5f),
            math::scale(math::subtract(maximum, minimum), 0.5f));
}

}  // namespace

ColliderWireGeometry buildColliderWireGeometry(
    const components::ColliderComponent& collider,
    const scenes::SceneTransform& world_transform,
    uint32_t curve_segments) {
  if (!math::isFinite(world_transform.position) ||
      !math::isFinite(world_transform.rotation) ||
      !math::isFinite(world_transform.scale)) {
    return {};
  }
  WireBuilder builder(world_transform, curve_segments);
  std::visit(
      [&](const auto& shape) {
        using Shape = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<Shape, components::BoxColliderShape>) {
          appendBox(builder, shape.center, shape.half_extents);
        } else if constexpr (std::is_same_v<Shape, components::SphereColliderShape>) {
          appendSphere(builder, shape.center, shape.radius);
        } else if constexpr (std::is_same_v<Shape, components::CapsuleColliderShape>) {
          appendCapsuleProfile(builder, shape.center, shape.radius, shape.radius,
                               shape.height);
        } else if constexpr (std::is_same_v<Shape, components::CylinderColliderShape>) {
          appendCylinder(builder, shape.center, shape.radius, shape.height);
        } else if constexpr (std::is_same_v<Shape, components::TaperedCapsuleColliderShape>) {
          appendCapsuleProfile(builder, shape.center, shape.bottom_radius,
                               shape.top_radius, shape.height);
        } else if constexpr (std::is_same_v<Shape, components::MeshColliderShape>) {
          appendMeshBounds(builder, shape);
        }
      },
      collider.shape);
  return builder.finish();
}

}  // namespace karma::tools::scene_editor
