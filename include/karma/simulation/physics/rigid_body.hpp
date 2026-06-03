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

    void setPosition(const glm::vec3& position);
    void setRotation(const glm::quat& rotation);
    void setVelocity(const glm::vec3& velocity);
    void setAngularVelocity(const glm::vec3& angularVelocity);
    void setKinematic(bool kinematic);
    void setUseGravity(bool useGravity);
    void setTrigger(bool trigger);

    bool isGrounded(const glm::vec3& dimensions) const;

    /// Destroys the backend body.
    void destroy();

    /// Returns backend-native handle for diagnostics/contact mapping.
    std::uintptr_t nativeHandle() const;

private:
    std::unique_ptr<karma::physics_backend::PhysicsRigidBodyBackend> backend_;
};

} // namespace karma::physics
