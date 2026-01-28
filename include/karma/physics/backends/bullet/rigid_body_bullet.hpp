#pragma once

#include "karma/physics/backend.hpp"
#include <memory>

class btRigidBody;
class btCollisionShape;
class btMotionState;

namespace karma::physics_backend {

class PhysicsWorldBullet;

class PhysicsRigidBodyBullet final : public PhysicsRigidBodyBackend {
public:
    PhysicsRigidBodyBullet() = default;
    PhysicsRigidBodyBullet(PhysicsWorldBullet* world,
                           std::unique_ptr<btRigidBody> body,
                           std::unique_ptr<btCollisionShape> shape,
                           std::unique_ptr<btMotionState> motionState);
    ~PhysicsRigidBodyBullet() override;

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
    PhysicsWorldBullet* world_ = nullptr;
    std::unique_ptr<btRigidBody> body_;
    std::unique_ptr<btCollisionShape> shape_;
    std::unique_ptr<btMotionState> motionState_;
};

} // namespace karma::physics_backend
