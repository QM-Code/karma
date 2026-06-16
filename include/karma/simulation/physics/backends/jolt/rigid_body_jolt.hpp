#pragma once

#include "karma/simulation/physics/backend.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <optional>

namespace karma::physics_backend {

class PhysicsWorldJolt;

/// \ingroup karma_physics
/// Jolt implementation of `PhysicsRigidBodyBackend`.
class PhysicsRigidBodyJolt final : public PhysicsRigidBodyBackend {
public:
    PhysicsRigidBodyJolt() = default;
    PhysicsRigidBodyJolt(PhysicsWorldJolt* world, const JPH::BodyID& bodyId);
    ~PhysicsRigidBodyJolt() override;

    bool isValid() const override;
    glm::vec3 getPosition() const override;
    glm::quat getRotation() const override;
    glm::vec3 getVelocity() const override;
    glm::vec3 getAngularVelocity() const override;
    glm::vec3 getForwardVector() const override;
    bool isActive() const override;
    void setPosition(const glm::vec3& position) override;
    void setRotation(const glm::quat& rotation) override;
    void setPositionAndRotation(const glm::vec3& position, const glm::quat& rotation) override;
    void moveKinematic(const glm::vec3& targetPosition,
                       const glm::quat& targetRotation,
                       float deltaTime) override;
    void setVelocity(const glm::vec3& velocity) override;
    void setAngularVelocity(const glm::vec3& angularVelocity) override;
    void setLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                     const glm::vec3& angularVelocity) override;
    void addLinearVelocity(const glm::vec3& velocity) override;
    void addLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                     const glm::vec3& angularVelocity) override;
    glm::vec3 getPointVelocity(const glm::vec3& point) const override;
    void setKinematic(bool kinematic) override;
    void setMotionQuality(karma::physics::PhysicsMotionQuality quality) override;
    void setUseGravity(bool useGravity) override;
    void setGravityFactor(float gravityFactor) override;
    float getGravityFactor() const override;
    void setTrigger(bool trigger) override;
    void setFriction(float friction) override;
    float getFriction() const override;
    void setRestitution(float restitution) override;
    float getRestitution() const override;
    void setUseManifoldReduction(bool enabled) override;
    bool getUseManifoldReduction() const override;
    void setUserData(uint64_t userData) override;
    uint64_t getUserData() const override;
    void activate() override;
    void deactivate() override;
    void resetSleepTimer() override;
    bool setShape(const karma::physics::PhysicsShapeDesc& shape,
                  bool updateMassProperties,
                  bool activate) override;
    void addForce(const glm::vec3& force) override;
    void addForceAtPosition(const glm::vec3& force, const glm::vec3& position) override;
    void addTorque(const glm::vec3& torque) override;
    void addImpulse(const glm::vec3& impulse) override;
    void addImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& position) override;
    void addAngularImpulse(const glm::vec3& impulse) override;
    bool applyBuoyancyImpulse(const karma::physics::PhysicsBuoyancyDesc& desc) override;
    bool isGrounded(const glm::vec3& dimensions) const override;
    void destroy() override;
    std::uintptr_t nativeHandle() const override;

private:
    PhysicsWorldJolt* world_ = nullptr;
    std::optional<JPH::BodyID> body_;
};

} // namespace karma::physics_backend
