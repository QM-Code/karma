#pragma once

#include "karma/physics/backend.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <optional>

namespace karma::physics_backend {

class PhysicsWorldJolt;

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
    void setPosition(const glm::vec3& position) override;
    void setRotation(const glm::quat& rotation) override;
    void setVelocity(const glm::vec3& velocity) override;
    void setAngularVelocity(const glm::vec3& angularVelocity) override;
    bool isGrounded(const glm::vec3& dimensions) const override;
    void destroy() override;
    std::uintptr_t nativeHandle() const override;

private:
    PhysicsWorldJolt* world_ = nullptr;
    std::optional<JPH::BodyID> body_;
};

} // namespace karma::physics_backend
