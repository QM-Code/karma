#include "karma/physics/backends/bullet/physics_world_bullet.hpp"
#include "karma/physics/backends/bullet/player_controller_bullet.hpp"
#include "karma/physics/backends/bullet/rigid_body_bullet.hpp"
#include "karma/physics/backends/bullet/static_body_bullet.hpp"
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>

namespace karma::physics_backend {

PhysicsWorldBullet::PhysicsWorldBullet() {
    collisionConfig_ = std::make_unique<btDefaultCollisionConfiguration>();
    dispatcher_ = std::make_unique<btCollisionDispatcher>(collisionConfig_.get());
    broadphase_ = std::make_unique<btDbvtBroadphase>();
    solver_ = std::make_unique<btSequentialImpulseConstraintSolver>();
    dynamicsWorld_ = std::make_unique<btDiscreteDynamicsWorld>(dispatcher_.get(),
                                                               broadphase_.get(),
                                                               solver_.get(),
                                                               collisionConfig_.get());
    dynamicsWorld_->setGravity(btVector3(0.0f, gravity_, 0.0f));

    ghostPairCallback_ = std::make_unique<btGhostPairCallback>();
    dynamicsWorld_->getBroadphase()->getOverlappingPairCache()->setInternalGhostPairCallback(ghostPairCallback_.get());
}

PhysicsWorldBullet::~PhysicsWorldBullet() {
    dynamicsWorld_.reset();
    solver_.reset();
    broadphase_.reset();
    dispatcher_.reset();
    collisionConfig_.reset();
    ghostPairCallback_.reset();
}

void PhysicsWorldBullet::update(float deltaTime) {
    if (!dynamicsWorld_) return;
    dynamicsWorld_->stepSimulation(deltaTime, 1, 1.0f / 60.0f);
}

void PhysicsWorldBullet::setGravity(float gravity) {
    gravity_ = gravity;
    if (dynamicsWorld_) {
        dynamicsWorld_->setGravity(btVector3(0.0f, gravity_, 0.0f));
    }
}

std::unique_ptr<PhysicsRigidBodyBackend> PhysicsWorldBullet::createBoxBody(const glm::vec3& halfExtents,
                                                                           float mass,
                                                                           const glm::vec3& position,
                                                                           const karma::physics::PhysicsMaterial& material) {
    if (!dynamicsWorld_) return std::make_unique<PhysicsRigidBodyBullet>();

    auto shape = std::make_unique<btBoxShape>(btVector3(halfExtents.x, halfExtents.y, halfExtents.z));
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(btVector3(position.x, position.y, position.z));
    auto motionState = std::make_unique<btDefaultMotionState>(transform);

    btVector3 inertia(0, 0, 0);
    if (mass > 0.0f) {
        shape->calculateLocalInertia(mass, inertia);
    }

    btRigidBody::btRigidBodyConstructionInfo info(mass, motionState.get(), shape.get(), inertia);
    info.m_friction = material.friction;
    info.m_restitution = material.restitution;

    auto body = std::make_unique<btRigidBody>(info);
    dynamicsWorld_->addRigidBody(body.get());

    return std::make_unique<PhysicsRigidBodyBullet>(this, std::move(body), std::move(shape), std::move(motionState));
}

std::unique_ptr<PhysicsPlayerControllerBackend> PhysicsWorldBullet::createPlayer(const glm::vec3& size) {
    const glm::vec3 halfExtents = size * 0.5f;
    return std::make_unique<PhysicsPlayerControllerBullet>(this, halfExtents, glm::vec3(0.0f, 2.0f, 0.0f));
}

std::unique_ptr<PhysicsStaticBodyBackend> PhysicsWorldBullet::createStaticMesh(const std::string& meshPath) {
    return PhysicsStaticBodyBullet::fromMesh(this, meshPath);
}

bool PhysicsWorldBullet::raycast(const glm::vec3& from,
                                 const glm::vec3& to,
                                 glm::vec3& hitPoint,
                                 glm::vec3& hitNormal) const {
    if (!dynamicsWorld_) return false;

    btVector3 btFrom(from.x, from.y, from.z);
    btVector3 btTo(to.x, to.y, to.z);
    btCollisionWorld::ClosestRayResultCallback callback(btFrom, btTo);
    dynamicsWorld_->rayTest(btFrom, btTo, callback);
    if (!callback.hasHit()) {
        return false;
    }

    const btVector3& hit = callback.m_hitPointWorld;
    const btVector3& normal = callback.m_hitNormalWorld;
    hitPoint = glm::vec3(hit.x(), hit.y(), hit.z());
    hitNormal = glm::vec3(normal.x(), normal.y(), normal.z());
    return true;
}

} // namespace karma::physics_backend
