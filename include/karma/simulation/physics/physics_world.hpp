#pragma once

#include "karma/simulation/physics/types.h"
#include "karma/simulation/physics/constraint.hpp"
#include "karma/simulation/physics/character_controller.hpp"
#include "karma/simulation/physics/rigid_body.hpp"
#include "karma/simulation/physics/soft_body.hpp"
#include "karma/simulation/physics/vehicle.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace karma::physics_backend {
class PhysicsWorldBackend;
}

namespace karma::physics {

/// \ingroup karma_physics
/// High-level physics world facade owned by `EngineApp`.
class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    /// Steps the physics backend.
    void update(float deltaTime);

    /// Sets world gravity.
    void setGravity(float gravity);

    /// Creates a rigid body from a backend-neutral body description.
    RigidBody createBody(const PhysicsBodyDesc& desc);

    /// Creates a two-body constraint from backend-native body handles.
    Constraint createConstraint(const PhysicsConstraintDesc& desc,
                                std::uintptr_t bodyA,
                                std::uintptr_t bodyB);
    /// Creates a vehicle constraint/controller attached to a rigid body handle.
    Vehicle createVehicle(const PhysicsVehicleDesc& desc, std::uintptr_t body);
    /// Creates a soft body from backend-neutral creation settings.
    SoftBody createSoftBody(const PhysicsSoftBodyDesc& desc);

    /// Creates a dynamic box body.
    RigidBody createBoxBody(const glm::vec3& halfExtents,
                            float mass,
                            const glm::vec3& position,
                            const PhysicsMaterial& material);

    /// Creates a character-controller body with an explicit size.
    CharacterController createCharacterController(const glm::vec3& size);

    /// Performs a simple raycast.
    bool raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& hitPoint, glm::vec3& hitNormal) const;
    /// Performs a raycast with support/contact metadata.
    bool raycastDetailed(const glm::vec3& from, const glm::vec3& to, PhysicsGroundContact& outHit) const;
    /// Performs a filtered raycast and returns the closest hit.
    bool castRay(const PhysicsRaycastDesc& desc, PhysicsQueryHit& outHit) const;
    /// Performs a filtered raycast and appends all hits sorted nearest first.
    void castRayAll(const PhysicsRaycastDesc& desc, std::vector<PhysicsQueryHit>& outHits) const;
    /// Appends all bodies containing `point`.
    void collidePoint(const glm::vec3& point,
                      const PhysicsQueryFilter& filter,
                      std::vector<PhysicsQueryHit>& outHits) const;
    /// Appends all bodies overlapping `desc.shape`.
    void collideShape(const PhysicsShapeQueryDesc& desc, std::vector<PhysicsQueryHit>& outHits) const;
    /// Appends all bodies hit by a linear shape cast.
    void castShape(const PhysicsShapeCastDesc& desc, std::vector<PhysicsQueryHit>& outHits) const;
    /// Collects backend contacts for this step.
    void collectContacts(std::vector<PhysicsContact>& outContacts) const;

private:
    std::unique_ptr<karma::physics_backend::PhysicsWorldBackend> backend_;
};

} // namespace karma::physics
