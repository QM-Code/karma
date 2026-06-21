#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#include <glm/glm.hpp>

#include "karma/math.h"
#include "karma/physics.h"

namespace karma::physics::system_internal {

using MeshColliderGeometryResolver =
    std::function<const MeshColliderGeometry*(std::string_view mesh_key)>;

bool nearlyEqualVec3(const math::Vec3& a, const math::Vec3& b, float eps = 1e-4f);
math::Vec3 negateVec3(const math::Vec3& v);

int colliderShapeKind(const world::World& world, world::Entity entity);
components::ColliderShapeType colliderShape(const world::World& world, world::Entity entity);
bool colliderIsTrigger(const world::World& world, world::Entity entity);
bool hasPhysicsCollider(const world::World& world, world::Entity entity);
world::Entity entityFromKey(uint64_t key);

bool collisionEnabled(world::World& world, world::Entity entity);
uint32_t collisionLayers(const world::World& world, world::Entity entity);
bool matchesCollisionLayerMask(const world::World& world, world::Entity entity, uint32_t mask);

PhysicsMotionType toPhysicsMotion(const components::RigidbodyComponent& body);
PhysicsBodyDesc buildBodyDesc(const world::World& world,
                              world::Entity entity,
                              const components::TransformComponent& transform,
                              const components::RigidbodyComponent* rigidbody,
                              PhysicsMotionType fallback_motion,
                              const MeshColliderGeometryResolver& resolve_mesh_geometry);
std::size_t bodySignature(const PhysicsBodyDesc& desc);

PhysicsConstraintDesc buildConstraintDesc(const components::PhysicsConstraintComponent& component);
std::size_t constraintSignature(const PhysicsConstraintDesc& desc,
                                std::uintptr_t body_a,
                                std::uintptr_t body_b);

PhysicsVehicleDesc buildVehicleDesc(const components::PhysicsVehicleComponent& component);
PhysicsVehicleInput buildVehicleInput(const components::PhysicsVehicleInputState& input);
std::size_t vehicleSignature(const PhysicsVehicleDesc& desc, std::uintptr_t body);

PhysicsSoftBodyDesc buildSoftBodyDesc(const components::PhysicsSoftBodyComponent& component,
                                      const components::TransformComponent* transform);
std::size_t softBodySignature(const PhysicsSoftBodyDesc& desc);

glm::vec3 groundProbeDimensions(const world::World& world,
                                world::Entity entity,
                                const components::TransformComponent& transform);

struct ControllerShapeInfo {
  glm::vec3 half_extents{};
  math::Vec3 center{};
  int shape_kind = -1;
  bool valid = false;
};

ControllerShapeInfo controllerShapeInfo(const world::World& world, world::Entity entity);

}  // namespace karma::physics::system_internal
