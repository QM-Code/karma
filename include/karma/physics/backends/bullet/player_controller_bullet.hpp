#pragma once

#include "karma/physics/backend.hpp"
#include <memory>

class btPairCachingGhostObject;
class btConvexShape;
class btKinematicCharacterController;

namespace karma::physics_backend {

class PhysicsWorldBullet;

class PhysicsPlayerControllerBullet final : public PhysicsPlayerControllerBackend {
public:
    PhysicsPlayerControllerBullet(PhysicsWorldBullet* world,
                                  const glm::vec3& halfExtents,
                                  const glm::vec3& startPosition);
    ~PhysicsPlayerControllerBullet() override;

    glm::vec3 getPosition() const override;
    glm::quat getRotation() const override;
    glm::vec3 getVelocity() const override;
    glm::vec3 getAngularVelocity() const override;
    glm::vec3 getForwardVector() const override;
    void setHalfExtents(const glm::vec3& extents) override;
    void update(float dt) override;
    void setPosition(const glm::vec3& position) override;
    void setRotation(const glm::quat& rotation) override;
    void setVelocity(const glm::vec3& velocity) override;
    void setAngularVelocity(const glm::vec3& angularVelocity) override;
    bool isGrounded() const override;
    void destroy() override;

private:
    void rebuildController(const glm::vec3& centerPosition);

    PhysicsWorldBullet* world_ = nullptr;
    std::unique_ptr<btPairCachingGhostObject> ghost_;
    std::unique_ptr<btConvexShape> shape_;
    std::unique_ptr<btKinematicCharacterController> controller_;
    glm::vec3 halfExtents{0.5f, 1.0f, 0.5f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 desiredVelocity{0.0f};
    glm::vec3 actualVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    glm::vec3 lastPosition{0.0f};
    float gravityMagnitude = 9.8f;
    float stepHeight = 0.35f;
};

} // namespace karma::physics_backend
