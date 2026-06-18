#pragma once

#include <cstdint>
#include <vector>

#include "karma/core/math/types.h"
#include "karma/world/ecs/component.h"

namespace karma::components {

enum class PhysicsSoftBodyPresetKind : uint8_t {
  Custom,
  Cloth,
  Cube,
  Sphere,
};

enum class PhysicsSoftBodyBendKind : uint8_t {
  None,
  Distance,
  Dihedral,
};

enum class PhysicsSoftBodyLraKind : uint8_t {
  None,
  EuclideanDistance,
  GeodesicDistance,
};

struct PhysicsSoftBodyVertex {
  math::Vec3 position{};
  math::Vec3 velocity{};
  float inverse_mass = 1.0f;
};

struct PhysicsSoftBodyFace {
  uint32_t vertex0 = 0;
  uint32_t vertex1 = 0;
  uint32_t vertex2 = 0;
  uint32_t material_index = 0;
};

struct PhysicsSoftBodyEdge {
  uint32_t vertex0 = 0;
  uint32_t vertex1 = 0;
  float compliance = 0.0f;
};

struct PhysicsSoftBodyVolume {
  uint32_t vertex0 = 0;
  uint32_t vertex1 = 0;
  uint32_t vertex2 = 0;
  uint32_t vertex3 = 0;
  float compliance = 0.0f;
};

struct PhysicsSoftBodyVertexAttributes {
  float compliance = 0.0f;
  float shear_compliance = 0.0f;
  float bend_compliance = 3.402823466e+38F;
  PhysicsSoftBodyLraKind lra_type = PhysicsSoftBodyLraKind::None;
  float lra_max_distance_multiplier = 1.0f;
};

/// \ingroup karma_components
/// Jolt-style soft body authored through ECS.
struct PhysicsSoftBodyComponent : ecs::ComponentTag {
  bool enabled = true;
  bool recreate = false;
  PhysicsSoftBodyPresetKind preset = PhysicsSoftBodyPresetKind::Custom;
  uint64_t user_data = 0;

  std::vector<PhysicsSoftBodyVertex> vertices;
  std::vector<PhysicsSoftBodyFace> faces;
  std::vector<PhysicsSoftBodyEdge> edges;
  std::vector<PhysicsSoftBodyVolume> volumes;
  std::vector<uint32_t> pinned_vertices;

  uint32_t grid_size_x = 12;
  uint32_t grid_size_y = 12;
  uint32_t grid_size_z = 4;
  float grid_spacing = 0.5f;
  float radius = 1.0f;
  uint32_t sphere_theta = 16;
  uint32_t sphere_phi = 8;
  bool pin_cloth_corners = true;

  bool create_constraints = true;
  bool optimize = true;
  PhysicsSoftBodyBendKind bend_type = PhysicsSoftBodyBendKind::Distance;
  PhysicsSoftBodyVertexAttributes vertex_attributes{};
  float angle_tolerance = 0.13962634f;
  float vertex_radius = 0.0f;

  float friction = 0.2f;
  float restitution = 0.0f;
  uint32_t collision_layers = 1u;
  uint32_t collides_with = 0xFFFFFFFFu;
  uint32_t solver_iterations = 5;
  float linear_damping = 0.1f;
  float max_linear_velocity = 500.0f;
  float pressure = 0.0f;
  float gravity_factor = 1.0f;
  bool update_position = true;
  bool make_rotation_identity = true;
  bool allow_sleeping = true;
  bool activate = true;
};

}  // namespace karma::components
