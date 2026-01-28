#include "karma/physics/backends/bullet/player_controller_bullet.hpp"
#include "karma/physics/backends/bullet/physics_world_bullet.hpp"
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <BulletDynamics/Character/btKinematicCharacterController.h>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace {
inline btVector3 toBt(const glm::vec3& v) { return btVector3(v.x, v.y, v.z); }
inline glm::vec3 toGlm(const btVector3& v) { return glm::vec3(v.x(), v.y(), v.z()); }
}

namespace karma::physics_backend {

PhysicsPlayerControllerBullet::PhysicsPlayerControllerBullet(PhysicsWorldBullet* world,
                                                             const glm::vec3& halfExtents,
                                                             const glm::vec3& startPosition)
    : world_(world), halfExtents(halfExtents) {
    rebuildController(startPosition + glm::vec3(0.0f, halfExtents.y, 0.0f));
}

PhysicsPlayerControllerBullet::~PhysicsPlayerControllerBullet() {
    destroy();
}

void PhysicsPlayerControllerBullet::rebuildController(const glm::vec3& centerPosition) {
    if (!world_ || !world_->world()) return;

    if (controller_) {
        world_->world()->removeAction(controller_.get());
    }
    if (ghost_) {
        world_->world()->removeCollisionObject(ghost_.get());
    }

    shape_ = std::make_unique<btBoxShape>(btVector3(halfExtents.x, halfExtents.y, halfExtents.z));
    ghost_ = std::make_unique<btPairCachingGhostObject>();

    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(toBt(centerPosition));
    ghost_->setWorldTransform(transform);
    ghost_->setCollisionShape(shape_.get());
    ghost_->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

    controller_ = std::make_unique<btKinematicCharacterController>(ghost_.get(), shape_.get(), stepHeight);
    controller_->setGravity(btVector3(0.0f, -gravityMagnitude, 0.0f));

    world_->world()->addCollisionObject(ghost_.get(),
                                        btBroadphaseProxy::CharacterFilter,
                                        btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter);
    world_->world()->addAction(controller_.get());
    lastPosition = getPosition();
}

glm::vec3 PhysicsPlayerControllerBullet::getPosition() const {
    if (!ghost_) return glm::vec3(0.0f);
    btTransform transform = ghost_->getWorldTransform();
    btVector3 pos = transform.getOrigin();
    return glm::vec3(pos.x(), pos.y(), pos.z()) - glm::vec3(0.0f, halfExtents.y, 0.0f);
}

glm::quat PhysicsPlayerControllerBullet::getRotation() const { return rotation; }
glm::vec3 PhysicsPlayerControllerBullet::getVelocity() const { return actualVelocity; }
glm::vec3 PhysicsPlayerControllerBullet::getAngularVelocity() const { return angularVelocity; }

glm::vec3 PhysicsPlayerControllerBullet::getForwardVector() const {
    return rotation * glm::vec3(0, 0, -1);
}

void PhysicsPlayerControllerBullet::setHalfExtents(const glm::vec3& extents) {
    halfExtents = extents;
    rebuildController(getPosition() + glm::vec3(0.0f, halfExtents.y, 0.0f));
}

void PhysicsPlayerControllerBullet::update(float dt) {
    if (!controller_ || dt <= 0.0f) return;

    if (world_ && world_->world()) {
        const btVector3 g = world_->world()->getGravity();
        gravityMagnitude = std::abs(static_cast<float>(g.y()));
        controller_->setGravity(btVector3(0.0f, -gravityMagnitude, 0.0f));
    }

    const bool grounded = controller_->onGround();
    btVector3 walkDir(desiredVelocity.x, 0.0f, desiredVelocity.z);
    controller_->setWalkDirection(walkDir * btScalar(dt));

    if (grounded && desiredVelocity.y > 0.0f) {
        controller_->setJumpSpeed(desiredVelocity.y);
        controller_->jump(btVector3(0, 1, 0));
    }

    const glm::vec3 currentPos = getPosition();
    actualVelocity = (currentPos - lastPosition) / dt;
    lastPosition = currentPos;

    if (glm::dot(angularVelocity, angularVelocity) > 0.f) {
        glm::quat dq = glm::quat(0, angularVelocity.x, angularVelocity.y, angularVelocity.z) * rotation;
        rotation = glm::normalize(rotation + 0.5f * dq * dt);
    }
}

void PhysicsPlayerControllerBullet::setPosition(const glm::vec3& position) {
    if (!ghost_) return;
    btTransform transform = ghost_->getWorldTransform();
    transform.setOrigin(toBt(position + glm::vec3(0.0f, halfExtents.y, 0.0f)));
    ghost_->setWorldTransform(transform);
    if (controller_ && world_ && world_->world()) {
        controller_->reset(world_->world());
    }
    lastPosition = position;
}

void PhysicsPlayerControllerBullet::setRotation(const glm::quat& rotationIn) {
    rotation = glm::normalize(rotationIn);
}

void PhysicsPlayerControllerBullet::setVelocity(const glm::vec3& velocity) {
    desiredVelocity = velocity;
}

void PhysicsPlayerControllerBullet::setAngularVelocity(const glm::vec3& angularVelocityIn) {
    angularVelocity = angularVelocityIn;
}

bool PhysicsPlayerControllerBullet::isGrounded() const {
    return controller_ ? controller_->onGround() : false;
}

void PhysicsPlayerControllerBullet::destroy() {
    if (world_ && world_->world()) {
        if (controller_) {
            world_->world()->removeAction(controller_.get());
        }
        if (ghost_) {
            world_->world()->removeCollisionObject(ghost_.get());
        }
    }
    controller_.reset();
    ghost_.reset();
    shape_.reset();
    world_ = nullptr;
}

} // namespace karma::physics_backend
