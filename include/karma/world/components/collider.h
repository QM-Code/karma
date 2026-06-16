#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "karma/world/components/transform.h"
#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Shared collider flags for physics and ECS collision queries.
struct ColliderComponent : ecs::ComponentTag {
  bool is_trigger = false;
  bool debug_draw = false;
};

/// \ingroup karma_components
/// Axis-aligned or transform-oriented box collider.
struct BoxColliderComponent : ColliderComponent {
  math::Vec3 center{};
  math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
};

/// \ingroup karma_components
/// Sphere collider.
struct SphereColliderComponent : ColliderComponent {
  math::Vec3 center{};
  float radius = 0.5f;
};

/// \ingroup karma_components
/// Capsule collider.
struct CapsuleColliderComponent : ColliderComponent {
  math::Vec3 center{};
  float radius = 0.5f;
  float height = 1.0f;
};

/// \ingroup karma_components
/// Cylinder collider aligned to the entity's local Y axis.
struct CylinderColliderComponent : ColliderComponent {
  math::Vec3 center{};
  float radius = 0.5f;
  float height = 1.0f;
  float convex_radius = 0.0f;
};

/// \ingroup karma_components
/// Tapered capsule collider aligned to the entity's local Y axis.
struct TaperedCapsuleColliderComponent : ColliderComponent {
  math::Vec3 center{};
  float top_radius = 0.5f;
  float bottom_radius = 0.5f;
  float height = 1.0f;
};

/// \ingroup karma_components
/// Convex hull collider built from local-space points.
struct ConvexHullColliderComponent : ColliderComponent {
  math::Vec3 center{};
  std::vector<math::Vec3> points;
  float convex_radius = 0.0f;
};

/// \ingroup karma_components
/// Single local-space triangle collider.
struct TriangleColliderComponent : ColliderComponent {
  std::array<math::Vec3, 3> points{};
  float convex_radius = 0.0f;
};

/// \ingroup karma_components
/// Height-field collider. Samples are row-major and require `sample_count * sample_count` values.
struct HeightFieldColliderComponent : ColliderComponent {
  std::vector<float> samples;
  uint32_t sample_count = 0;
  math::Vec3 offset{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  uint32_t block_size = 2;
  uint32_t bits_per_sample = 8;
};

/// \ingroup karma_components
/// Mesh collider using render or imported mesh geometry.
struct MeshColliderComponent : ColliderComponent {
  std::string mesh_path;
  std::vector<math::Vec3> vertices;
  std::vector<uint32_t> indices;
};

}  // namespace karma::components
