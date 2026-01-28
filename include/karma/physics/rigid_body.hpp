#pragma once

#include "karma/physics/backend.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace karma::physics {

class RigidBody {
public:
    RigidBody() = default;
    explicit RigidBody(std::unique_ptr<karma::physics_backend::PhysicsRigidBodyBackend> backend);
    RigidBody(const RigidBody&) = delete;
    RigidBody& operator=(const RigidBody&) = delete;
    RigidBody(RigidBody&& other) noexcept = default;
    RigidBody& operator=(RigidBody&& other) noexcept = default;
    ~RigidBody();

    bool isValid() const;

    glm::vec3 getPosition() const;
    glm::quat getRotation() const;
    glm::vec3 getVelocity() const;
    glm::vec3 getAngularVelocity() const;
    glm::vec3 getForwardVector() const;

    void setPosition(const glm::vec3& position);
    void setRotation(const glm::quat& rotation);
    void setVelocity(const glm::vec3& velocity);
    void setAngularVelocity(const glm::vec3& angularVelocity);

    bool isGrounded(const glm::vec3& dimensions) const;

    void destroy();

    std::uintptr_t nativeHandle() const;

private:
    std::unique_ptr<karma::physics_backend::PhysicsRigidBodyBackend> backend_;
};

} // namespace karma::physics
