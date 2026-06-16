#pragma once

#include "karma/simulation/physics/backend.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace karma::physics {

/// \ingroup karma_physics
/// Move-only wrapper around a backend dynamic rigid body.
class RigidBody {
public:
    RigidBody() = default;
    explicit RigidBody(std::unique_ptr<karma::physics_backend::PhysicsRigidBodyBackend> backend);
    RigidBody(const RigidBody&) = delete;
    RigidBody& operator=(const RigidBody&) = delete;
    RigidBody(RigidBody&& other) noexcept = default;
    RigidBody& operator=(RigidBody&& other) noexcept = default;
    ~RigidBody();

    /// Returns true when a backend body exists and is valid.
    bool isValid() const;

    glm::vec3 getPosition() const;
    glm::quat getRotation() const;
    glm::vec3 getVelocity() const;
    glm::vec3 getAngularVelocity() const;
    glm::vec3 getForwardVector() const;
    bool isActive() const;

    void setPosition(const glm::vec3& position);
    void setRotation(const glm::quat& rotation);
    void setPositionAndRotation(const glm::vec3& position, const glm::quat& rotation);
    void moveKinematic(const glm::vec3& targetPosition,
                       const glm::quat& targetRotation,
                       float deltaTime);
    void setVelocity(const glm::vec3& velocity);
    void setAngularVelocity(const glm::vec3& angularVelocity);
    void setLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                     const glm::vec3& angularVelocity);
    void addLinearVelocity(const glm::vec3& velocity);
    void addLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                     const glm::vec3& angularVelocity);
    glm::vec3 getPointVelocity(const glm::vec3& point) const;
    void setKinematic(bool kinematic);
    void setMotionQuality(PhysicsMotionQuality quality);
    void setUseGravity(bool useGravity);
    void setGravityFactor(float gravityFactor);
    float getGravityFactor() const;
    void setTrigger(bool trigger);
    void setFriction(float friction);
    float getFriction() const;
    void setRestitution(float restitution);
    float getRestitution() const;
    void setUseManifoldReduction(bool enabled);
    bool getUseManifoldReduction() const;
    void setUserData(uint64_t userData);
    uint64_t getUserData() const;
    void activate();
    void deactivate();
    void resetSleepTimer();
    bool setShape(const PhysicsShapeDesc& shape,
                  bool updateMassProperties = true,
                  bool activate = true);
    void addForce(const glm::vec3& force);
    void addForceAtPosition(const glm::vec3& force, const glm::vec3& position);
    void addTorque(const glm::vec3& torque);
    void addImpulse(const glm::vec3& impulse);
    void addImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& position);
    void addAngularImpulse(const glm::vec3& impulse);
    bool applyBuoyancyImpulse(const PhysicsBuoyancyDesc& desc);

    bool isGrounded(const glm::vec3& dimensions) const;

    /// Destroys the backend body.
    void destroy();

    /// Returns backend-native handle for diagnostics/contact mapping.
    std::uintptr_t nativeHandle() const;

private:
    std::unique_ptr<karma::physics_backend::PhysicsRigidBodyBackend> backend_;
};

} // namespace karma::physics
