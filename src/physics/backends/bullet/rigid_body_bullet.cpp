#include "karma/physics/backends/bullet/rigid_body_bullet.hpp"
#include "karma/physics/backends/bullet/physics_world_bullet.hpp"
#include <btBulletDynamicsCommon.h>

namespace {
inline glm::vec3 toGlm(const btVector3& v) { return glm::vec3(v.x(), v.y(), v.z()); }
inline btVector3 toBt(const glm::vec3& v) { return btVector3(v.x, v.y, v.z); }
}

namespace karma::physics_backend {

PhysicsRigidBodyBullet::PhysicsRigidBodyBullet(PhysicsWorldBullet* world,
                                               std::unique_ptr<btRigidBody> body,
                                               std::unique_ptr<btCollisionShape> shape,
                                               std::unique_ptr<btMotionState> motionState)
    : world_(world), body_(std::move(body)), shape_(std::move(shape)), motionState_(std::move(motionState)) {}

PhysicsRigidBodyBullet::~PhysicsRigidBodyBullet() {
    destroy();
}

bool PhysicsRigidBodyBullet::isValid() const {
    return world_ != nullptr && body_ != nullptr;
}

glm::vec3 PhysicsRigidBodyBullet::getPosition() const {
    if (!body_) return glm::vec3(0.0f);
    btTransform transform;
    body_->getMotionState()->getWorldTransform(transform);
    return toGlm(transform.getOrigin());
}

glm::quat PhysicsRigidBodyBullet::getRotation() const {
    if (!body_) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    btTransform transform;
    body_->getMotionState()->getWorldTransform(transform);
    const btQuaternion& q = transform.getRotation();
    return glm::quat(q.w(), q.x(), q.y(), q.z());
}

glm::vec3 PhysicsRigidBodyBullet::getVelocity() const {
    return body_ ? toGlm(body_->getLinearVelocity()) : glm::vec3(0.0f);
}

glm::vec3 PhysicsRigidBodyBullet::getAngularVelocity() const {
    return body_ ? toGlm(body_->getAngularVelocity()) : glm::vec3(0.0f);
}

glm::vec3 PhysicsRigidBodyBullet::getForwardVector() const {
    if (!body_) return glm::vec3(0.0f, 0.0f, -1.0f);
    const btQuaternion q = body_->getWorldTransform().getRotation();
    glm::quat rot(q.w(), q.x(), q.y(), q.z());
    return glm::normalize(rot * glm::vec3(0, 0, -1));
}

void PhysicsRigidBodyBullet::setPosition(const glm::vec3& position) {
    if (!body_) return;
    btTransform transform = body_->getWorldTransform();
    transform.setOrigin(toBt(position));
    body_->setWorldTransform(transform);
    if (body_->getMotionState()) {
        body_->getMotionState()->setWorldTransform(transform);
    }
    body_->activate(true);
}

void PhysicsRigidBodyBullet::setRotation(const glm::quat& rotation) {
    if (!body_) return;
    btTransform transform = body_->getWorldTransform();
    transform.setRotation(btQuaternion(rotation.x, rotation.y, rotation.z, rotation.w));
    body_->setWorldTransform(transform);
    if (body_->getMotionState()) {
        body_->getMotionState()->setWorldTransform(transform);
    }
    body_->activate(true);
}

void PhysicsRigidBodyBullet::setVelocity(const glm::vec3& velocity) {
    if (!body_) return;
    body_->setLinearVelocity(toBt(velocity));
    body_->activate(true);
}

void PhysicsRigidBodyBullet::setAngularVelocity(const glm::vec3& angularVelocity) {
    if (!body_) return;
    body_->setAngularVelocity(toBt(angularVelocity));
    body_->activate(true);
}

bool PhysicsRigidBodyBullet::isGrounded(const glm::vec3& dimensions) const {
    if (!world_ || !body_ || !world_->world()) return false;

    btBoxShape shape(btVector3(dimensions.x * 0.5f, dimensions.y * 0.5f, dimensions.z * 0.5f));
    btTransform startTransform = body_->getWorldTransform();
    btTransform endTransform = startTransform;
    endTransform.setOrigin(startTransform.getOrigin() + btVector3(0, -0.1f, 0));

    btCollisionWorld::ClosestConvexResultCallback callback(startTransform.getOrigin(), endTransform.getOrigin());
    world_->world()->convexSweepTest(&shape, startTransform, endTransform, callback);
    if (!callback.hasHit()) return false;

    btVector3 n = callback.m_hitNormalWorld;
    return n.dot(btVector3(0, 1, 0)) > 0.7f;
}

void PhysicsRigidBodyBullet::destroy() {
    if (!world_ || !body_) {
        return;
    }
    if (world_->world()) {
        world_->world()->removeRigidBody(body_.get());
    }
    body_.reset();
    motionState_.reset();
    shape_.reset();
    world_ = nullptr;
}

std::uintptr_t PhysicsRigidBodyBullet::nativeHandle() const {
    return reinterpret_cast<std::uintptr_t>(body_.get());
}

} // namespace karma::physics_backend
