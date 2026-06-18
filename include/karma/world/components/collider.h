#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "karma/world/components/transform.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/world.h"

namespace karma::components {

/// \ingroup karma_components
/// Collider shape kind exposed by public components and ECS query helpers.
enum class ColliderShapeType : uint8_t {
  Box,
  Sphere,
  Capsule,
  Cylinder,
  TaperedCapsule,
  ConvexHull,
  Triangle,
  HeightField,
  Mesh,
};

/// \ingroup karma_components
/// Axis-aligned or transform-oriented box collider.
struct BoxColliderShape {
  math::Vec3 center{};
  math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
};

/// \ingroup karma_components
/// Sphere collider.
struct SphereColliderShape {
  math::Vec3 center{};
  float radius = 0.5f;
};

/// \ingroup karma_components
/// Capsule collider.
struct CapsuleColliderShape {
  math::Vec3 center{};
  float radius = 0.5f;
  float height = 1.0f;
};

/// \ingroup karma_components
/// Cylinder collider aligned to the entity's local Y axis.
struct CylinderColliderShape {
  math::Vec3 center{};
  float radius = 0.5f;
  float height = 1.0f;
  float convex_radius = 0.0f;
};

/// \ingroup karma_components
/// Tapered capsule collider aligned to the entity's local Y axis.
struct TaperedCapsuleColliderShape {
  math::Vec3 center{};
  float top_radius = 0.5f;
  float bottom_radius = 0.5f;
  float height = 1.0f;
};

/// \ingroup karma_components
/// Convex hull collider built from local-space points.
struct ConvexHullColliderShape {
  math::Vec3 center{};
  std::vector<math::Vec3> points;
  float convex_radius = 0.0f;
};

/// \ingroup karma_components
/// Single local-space triangle collider.
struct TriangleColliderShape {
  std::array<math::Vec3, 3> points{};
  float convex_radius = 0.0f;
};

/// \ingroup karma_components
/// Height-field collider. Samples are row-major and require `sample_count * sample_count` values.
struct HeightFieldColliderShape {
  std::vector<float> samples;
  uint32_t sample_count = 0;
  math::Vec3 offset{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  uint32_t block_size = 2;
  uint32_t bits_per_sample = 8;
};

/// \ingroup karma_components
/// Mesh collider using render or imported mesh geometry.
struct MeshColliderShape {
  std::string mesh_path;
  std::vector<math::Vec3> vertices;
  std::vector<uint32_t> indices;
};

using ColliderShape = std::variant<BoxColliderShape,
                                   SphereColliderShape,
                                   CapsuleColliderShape,
                                   CylinderColliderShape,
                                   TaperedCapsuleColliderShape,
                                   ConvexHullColliderShape,
                                   TriangleColliderShape,
                                   HeightFieldColliderShape,
                                   MeshColliderShape>;

/// \ingroup karma_components
/// Single collider component tagged by shape variant.
struct ColliderComponent : ecs::ComponentTag {
  ColliderShapeType type = ColliderShapeType::Box;
  bool is_trigger = false;
  bool debug_draw = false;
  ColliderShape shape = BoxColliderShape{};

  static ColliderComponent box(BoxColliderShape shape = {},
                               bool is_trigger = false,
                               bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::Box,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent sphere(SphereColliderShape shape = {},
                                  bool is_trigger = false,
                                  bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::Sphere,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent capsule(CapsuleColliderShape shape = {},
                                   bool is_trigger = false,
                                   bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::Capsule,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent cylinder(CylinderColliderShape shape = {},
                                    bool is_trigger = false,
                                    bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::Cylinder,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent taperedCapsule(TaperedCapsuleColliderShape shape = {},
                                          bool is_trigger = false,
                                          bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::TaperedCapsule,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent convexHull(ConvexHullColliderShape shape = {},
                                      bool is_trigger = false,
                                      bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::ConvexHull,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent triangle(TriangleColliderShape shape = {},
                                    bool is_trigger = false,
                                    bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::Triangle,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent heightField(HeightFieldColliderShape shape = {},
                                       bool is_trigger = false,
                                       bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::HeightField,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent mesh(MeshColliderShape shape = {},
                                bool is_trigger = false,
                                bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::Mesh,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static void Validate(ecs::World& world, ecs::Entity entity) {
    if (!world.has<TransformComponent>(entity)) {
      throw std::runtime_error("ColliderComponent requires TransformComponent on the same entity.");
    }
  }
};

inline ColliderShapeType colliderShapeType(const ColliderShape& shape) {
  return std::visit(
      [](const auto& value) {
        using Shape = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Shape, BoxColliderShape>) {
          return ColliderShapeType::Box;
        } else if constexpr (std::is_same_v<Shape, SphereColliderShape>) {
          return ColliderShapeType::Sphere;
        } else if constexpr (std::is_same_v<Shape, CapsuleColliderShape>) {
          return ColliderShapeType::Capsule;
        } else if constexpr (std::is_same_v<Shape, CylinderColliderShape>) {
          return ColliderShapeType::Cylinder;
        } else if constexpr (std::is_same_v<Shape, TaperedCapsuleColliderShape>) {
          return ColliderShapeType::TaperedCapsule;
        } else if constexpr (std::is_same_v<Shape, ConvexHullColliderShape>) {
          return ColliderShapeType::ConvexHull;
        } else if constexpr (std::is_same_v<Shape, TriangleColliderShape>) {
          return ColliderShapeType::Triangle;
        } else if constexpr (std::is_same_v<Shape, HeightFieldColliderShape>) {
          return ColliderShapeType::HeightField;
        } else {
          return ColliderShapeType::Mesh;
        }
      },
      shape);
}

inline bool colliderTypeMatchesShape(const ColliderComponent& collider) {
  return collider.type == colliderShapeType(collider.shape);
}

inline bool isCharacterControllerShape(ColliderShapeType type) {
  return type == ColliderShapeType::Box;
}

}  // namespace karma::components
