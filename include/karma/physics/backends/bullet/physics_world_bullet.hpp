#pragma once

#include "karma/physics/backend.hpp"
#include <memory>

class btBroadphaseInterface;
class btCollisionDispatcher;
class btDefaultCollisionConfiguration;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btGhostPairCallback;

namespace karma::physics_backend {

class PhysicsWorldBullet final : public PhysicsWorldBackend {
public:
    PhysicsWorldBullet();
    ~PhysicsWorldBullet() override;

    void update(float deltaTime) override;
    void setGravity(float gravity) override;
    std::unique_ptr<PhysicsRigidBodyBackend> createBoxBody(const glm::vec3& halfExtents,
                                                           float mass,
                                                           const glm::vec3& position,
                                                           const karma::physics::PhysicsMaterial& material) override;
    std::unique_ptr<PhysicsPlayerControllerBackend> createPlayer(const glm::vec3& size) override;
    std::unique_ptr<PhysicsStaticBodyBackend> createStaticMesh(const std::string& meshPath) override;
    bool raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& hitPoint, glm::vec3& hitNormal) const override;

    btDiscreteDynamicsWorld* world() { return dynamicsWorld_.get(); }
    const btDiscreteDynamicsWorld* world() const { return dynamicsWorld_.get(); }

private:
    float gravity_ = -9.8f;
    std::unique_ptr<btBroadphaseInterface> broadphase_;
    std::unique_ptr<btDefaultCollisionConfiguration> collisionConfig_;
    std::unique_ptr<btCollisionDispatcher> dispatcher_;
    std::unique_ptr<btSequentialImpulseConstraintSolver> solver_;
    std::unique_ptr<btDiscreteDynamicsWorld> dynamicsWorld_;
    std::unique_ptr<btGhostPairCallback> ghostPairCallback_;
};

} // namespace karma::physics_backend
