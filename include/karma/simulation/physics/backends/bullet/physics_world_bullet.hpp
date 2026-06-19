#pragma once

#include "karma/simulation/physics/backend.hpp"
#include <memory>

class btBroadphaseInterface;
class btCollisionDispatcher;
class btDefaultCollisionConfiguration;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btGhostPairCallback;

namespace karma::physics_backend {

/// \ingroup karma_physics
/// Bullet implementation of `PhysicsWorldBackend`.
class PhysicsWorldBullet final : public PhysicsWorldBackend {
public:
    PhysicsWorldBullet();
    ~PhysicsWorldBullet() override;

    void update(float deltaTime) override;
    void setGravity(float gravity) override;
    std::unique_ptr<PhysicsRigidBodyBackend> createBody(const karma::physics::PhysicsBodyDesc& desc) override;
    std::unique_ptr<PhysicsConstraintBackend> createConstraint(
        const karma::physics::PhysicsConstraintDesc& desc,
        std::uintptr_t bodyA,
        std::uintptr_t bodyB) override;
    std::unique_ptr<PhysicsVehicleBackend> createVehicle(
        const karma::physics::PhysicsVehicleDesc& desc,
        std::uintptr_t body) override;
    std::unique_ptr<PhysicsSoftBodyBackend> createSoftBody(
        const karma::physics::PhysicsSoftBodyDesc& desc) override;
    std::unique_ptr<PhysicsRigidBodyBackend> createBoxBody(const glm::vec3& halfExtents,
                                                           float mass,
                                                           const glm::vec3& position,
                                                           const karma::physics::PhysicsMaterial& material) override;
    std::unique_ptr<PhysicsCharacterControllerBackend> createCharacterController(const glm::vec3& size) override;
    bool raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& hitPoint, glm::vec3& hitNormal) const override;
    bool raycastDetailed(const glm::vec3& from,
                         const glm::vec3& to,
                         karma::physics::PhysicsGroundContact& outHit) const override;
    bool castRay(const karma::physics::PhysicsRaycastDesc& desc,
                 karma::physics::PhysicsQueryHit& outHit) const override;
    void castRayAll(const karma::physics::PhysicsRaycastDesc& desc,
                    std::vector<karma::physics::PhysicsQueryHit>& outHits) const override;
    void collidePoint(const glm::vec3& point,
                      const karma::physics::PhysicsQueryFilter& filter,
                      std::vector<karma::physics::PhysicsQueryHit>& outHits) const override;
    void collideShape(const karma::physics::PhysicsShapeQueryDesc& desc,
                      std::vector<karma::physics::PhysicsQueryHit>& outHits) const override;
    void castShape(const karma::physics::PhysicsShapeCastDesc& desc,
                   std::vector<karma::physics::PhysicsQueryHit>& outHits) const override;
    void collectContacts(std::vector<karma::physics::PhysicsContact>& outContacts) const override;

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
