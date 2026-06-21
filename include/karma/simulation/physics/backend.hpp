#pragma once

#include "karma/simulation/physics/types.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace karma::physics_backend {

/// \ingroup karma_physics
/// Backend interface for a two-body physics constraint.
class PhysicsConstraintBackend {
public:
    virtual ~PhysicsConstraintBackend() = default;
    virtual bool isValid() const = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual void destroy() = 0;
    virtual std::uintptr_t nativeHandle() const = 0;
};

/// \ingroup karma_physics
/// Backend interface for a dynamic rigid body.
class PhysicsRigidBodyBackend {
public:
    virtual ~PhysicsRigidBodyBackend() = default;
    virtual bool isValid() const = 0;
    virtual glm::vec3 getPosition() const = 0;
    virtual glm::quat getRotation() const = 0;
    virtual glm::vec3 getVelocity() const = 0;
    virtual glm::vec3 getAngularVelocity() const = 0;
    virtual glm::vec3 getForwardVector() const = 0;
    virtual bool isActive() const = 0;
    virtual void setPosition(const glm::vec3& position) = 0;
    virtual void setRotation(const glm::quat& rotation) = 0;
    virtual void setPositionAndRotation(const glm::vec3& position, const glm::quat& rotation) = 0;
    virtual void moveKinematic(const glm::vec3& targetPosition,
                               const glm::quat& targetRotation,
                               float deltaTime) = 0;
    virtual void setVelocity(const glm::vec3& velocity) = 0;
    virtual void setAngularVelocity(const glm::vec3& angularVelocity) = 0;
    virtual void setLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                             const glm::vec3& angularVelocity) = 0;
    virtual void addLinearVelocity(const glm::vec3& velocity) = 0;
    virtual void addLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                             const glm::vec3& angularVelocity) = 0;
    virtual glm::vec3 getPointVelocity(const glm::vec3& point) const = 0;
    virtual void setKinematic(bool kinematic) = 0;
    virtual void setMotionQuality(karma::physics::PhysicsMotionQuality quality) = 0;
    virtual void setUseGravity(bool useGravity) = 0;
    virtual void setGravityFactor(float gravityFactor) = 0;
    virtual float getGravityFactor() const = 0;
    virtual void setTrigger(bool trigger) = 0;
    virtual void setFriction(float friction) = 0;
    virtual float getFriction() const = 0;
    virtual void setRestitution(float restitution) = 0;
    virtual float getRestitution() const = 0;
    virtual void setUseManifoldReduction(bool enabled) = 0;
    virtual bool getUseManifoldReduction() const = 0;
    virtual void setUserData(uint64_t userData) = 0;
    virtual uint64_t getUserData() const = 0;
    virtual void activate() = 0;
    virtual void deactivate() = 0;
    virtual void resetSleepTimer() = 0;
    virtual bool setShape(const karma::physics::PhysicsShapeDesc& shape,
                          bool updateMassProperties,
                          bool activate) = 0;
    virtual void addForce(const glm::vec3& force) = 0;
    virtual void addForceAtPosition(const glm::vec3& force, const glm::vec3& position) = 0;
    virtual void addTorque(const glm::vec3& torque) = 0;
    virtual void addImpulse(const glm::vec3& impulse) = 0;
    virtual void addImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& position) = 0;
    virtual void addAngularImpulse(const glm::vec3& impulse) = 0;
    virtual bool applyBuoyancyImpulse(const karma::physics::PhysicsBuoyancyDesc& desc) = 0;
    virtual bool isGrounded(const glm::vec3& dimensions) const = 0;
    virtual void destroy() = 0;
    virtual std::uintptr_t nativeHandle() const = 0;
};

/// \ingroup karma_physics
/// Backend interface for a vehicle constraint/controller.
class PhysicsVehicleBackend {
public:
    virtual ~PhysicsVehicleBackend() = default;
    virtual bool isValid() const = 0;
    virtual void setInput(const karma::physics::PhysicsVehicleInput& input) = 0;
    virtual karma::physics::PhysicsVehicleState getState() const = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual void destroy() = 0;
    virtual std::uintptr_t nativeHandle() const = 0;
};

/// \ingroup karma_physics
/// Backend interface for a soft body.
class PhysicsSoftBodyBackend {
public:
    virtual ~PhysicsSoftBodyBackend() = default;
    virtual bool isValid() const = 0;
    virtual glm::vec3 getPosition() const = 0;
    virtual glm::quat getRotation() const = 0;
    virtual bool isActive() const = 0;
    virtual void setPressure(float pressure) = 0;
    virtual void setUpdatePosition(bool updatePosition) = 0;
    virtual void setEnableSkinConstraints(bool enabled) = 0;
    virtual void setSkinnedMaxDistanceMultiplier(float multiplier) = 0;
    virtual void setVertexPosition(uint32_t vertex, const glm::vec3& position, bool hardSkin) = 0;
    virtual karma::physics::PhysicsSoftBodyState getState() const = 0;
    virtual void activate() = 0;
    virtual void deactivate() = 0;
    virtual void destroy() = 0;
    virtual std::uintptr_t nativeHandle() const = 0;
};

/// \ingroup karma_physics
/// Backend interface for a character-controller body.
class PhysicsCharacterControllerBackend {
public:
    virtual ~PhysicsCharacterControllerBackend() = default;
    virtual glm::vec3 getPosition() const = 0;
    virtual glm::quat getRotation() const = 0;
    virtual glm::vec3 getVelocity() const = 0;
    virtual glm::vec3 getAngularVelocity() const = 0;
    virtual glm::vec3 getForwardVector() const = 0;
    virtual void setHalfExtents(const glm::vec3& extents) = 0;
    virtual void setCenter(const glm::vec3& center) = 0;
    virtual void update(float dt) = 0;
    virtual void setPosition(const glm::vec3& position) = 0;
    virtual void setRotation(const glm::quat& rotation) = 0;
    virtual void setVelocity(const glm::vec3& velocity) = 0;
    virtual void setAngularVelocity(const glm::vec3& angularVelocity) = 0;
    virtual bool isGrounded() const = 0;
    virtual bool getGroundContact(karma::physics::PhysicsGroundContact& outContact) const = 0;
    virtual void collectContacts(std::vector<karma::physics::PhysicsContact>& outContacts) const = 0;
    virtual void destroy() = 0;
    virtual std::uintptr_t nativeHandle() const = 0;
};

/// \ingroup karma_physics
/// Backend interface for the physics world.
class PhysicsWorldBackend {
public:
    virtual ~PhysicsWorldBackend() = default;
    virtual void update(float deltaTime) = 0;
    virtual void setGravity(float gravity) = 0;
    virtual std::unique_ptr<PhysicsRigidBodyBackend> createBody(const karma::physics::PhysicsBodyDesc& desc) = 0;
    virtual std::unique_ptr<PhysicsConstraintBackend> createConstraint(
        const karma::physics::PhysicsConstraintDesc& desc,
        std::uintptr_t bodyA,
        std::uintptr_t bodyB) = 0;
    virtual std::unique_ptr<PhysicsVehicleBackend> createVehicle(
        const karma::physics::PhysicsVehicleDesc& desc,
        std::uintptr_t body) = 0;
    virtual std::unique_ptr<PhysicsSoftBodyBackend> createSoftBody(
        const karma::physics::PhysicsSoftBodyDesc& desc) = 0;
    virtual std::unique_ptr<PhysicsRigidBodyBackend> createBoxBody(const glm::vec3& halfExtents,
                                                                   float mass,
                                                                   const glm::vec3& position,
                                                                   const karma::physics::PhysicsMaterial& material) = 0;
    virtual std::unique_ptr<PhysicsCharacterControllerBackend> createCharacterController(const glm::vec3& size) = 0;
    virtual bool raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& hitPoint, glm::vec3& hitNormal) const = 0;
    virtual bool raycastDetailed(const glm::vec3& from,
                                 const glm::vec3& to,
                                 karma::physics::PhysicsGroundContact& outHit) const = 0;
    virtual bool castRay(const karma::physics::PhysicsRaycastDesc& desc,
                         karma::physics::PhysicsQueryHit& outHit) const = 0;
    virtual void castRayAll(const karma::physics::PhysicsRaycastDesc& desc,
                            std::vector<karma::physics::PhysicsQueryHit>& outHits) const = 0;
    virtual void collidePoint(const glm::vec3& point,
                              const karma::physics::PhysicsQueryFilter& filter,
                              std::vector<karma::physics::PhysicsQueryHit>& outHits) const = 0;
    virtual void collideShape(const karma::physics::PhysicsShapeQueryDesc& desc,
                              std::vector<karma::physics::PhysicsQueryHit>& outHits) const = 0;
    virtual void castShape(const karma::physics::PhysicsShapeCastDesc& desc,
                           std::vector<karma::physics::PhysicsQueryHit>& outHits) const = 0;
    virtual void collectContacts(std::vector<karma::physics::PhysicsContact>& outContacts) const = 0;
};

/// Creates the configured physics backend.
std::unique_ptr<PhysicsWorldBackend> CreatePhysicsWorldBackend();

} // namespace karma::physics_backend
