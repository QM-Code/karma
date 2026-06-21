#include "private/physics/backends/bullet/rigid_body_bullet.hpp"
#include "private/physics/backends/bullet/physics_world_bullet.hpp"
#include <btBulletDynamicsCommon.h>

namespace {
inline glm::vec3 toGlm(const btVector3& v) { return glm::vec3(v.x(), v.y(), v.z()); }
inline btVector3 toBt(const glm::vec3& v) { return btVector3(v.x, v.y, v.z); }
}

namespace karma::physics::backend {

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

bool PhysicsRigidBodyBullet::isActive() const {
    return body_ ? body_->isActive() : false;
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

void PhysicsRigidBodyBullet::setPositionAndRotation(const glm::vec3& position, const glm::quat& rotation) {
    if (!body_) return;
    btTransform transform = body_->getWorldTransform();
    transform.setOrigin(toBt(position));
    transform.setRotation(btQuaternion(rotation.x, rotation.y, rotation.z, rotation.w));
    body_->setWorldTransform(transform);
    if (body_->getMotionState()) {
        body_->getMotionState()->setWorldTransform(transform);
    }
    body_->activate(true);
}

void PhysicsRigidBodyBullet::moveKinematic(const glm::vec3& targetPosition,
                                           const glm::quat& targetRotation,
                                           float /*deltaTime*/) {
    setPositionAndRotation(targetPosition, targetRotation);
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

void PhysicsRigidBodyBullet::setLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                                         const glm::vec3& angularVelocity) {
    if (!body_) return;
    body_->setLinearVelocity(toBt(linearVelocity));
    body_->setAngularVelocity(toBt(angularVelocity));
    body_->activate(true);
}

void PhysicsRigidBodyBullet::addLinearVelocity(const glm::vec3& velocity) {
    if (!body_) return;
    body_->setLinearVelocity(body_->getLinearVelocity() + toBt(velocity));
    body_->activate(true);
}

void PhysicsRigidBodyBullet::addLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                                         const glm::vec3& angularVelocity) {
    if (!body_) return;
    body_->setLinearVelocity(body_->getLinearVelocity() + toBt(linearVelocity));
    body_->setAngularVelocity(body_->getAngularVelocity() + toBt(angularVelocity));
    body_->activate(true);
}

glm::vec3 PhysicsRigidBodyBullet::getPointVelocity(const glm::vec3& point) const {
    if (!body_) return glm::vec3(0.0f);
    const btVector3 rel_pos = toBt(point) - body_->getCenterOfMassPosition();
    return toGlm(body_->getVelocityInLocalPoint(rel_pos));
}

void PhysicsRigidBodyBullet::setKinematic(bool kinematic) {
    if (!body_) return;
    int flags = body_->getCollisionFlags();
    if (kinematic) {
        flags |= btCollisionObject::CF_KINEMATIC_OBJECT;
        body_->setActivationState(DISABLE_DEACTIVATION);
    } else {
        flags &= ~btCollisionObject::CF_KINEMATIC_OBJECT;
        body_->setActivationState(ACTIVE_TAG);
    }
    body_->setCollisionFlags(flags);
}

void PhysicsRigidBodyBullet::setMotionQuality(karma::physics::PhysicsMotionQuality /*quality*/) {}

void PhysicsRigidBodyBullet::setUseGravity(bool useGravity) {
    if (!body_ || !world_ || !world_->world()) return;
    if (useGravity) {
        body_->setGravity(world_->world()->getGravity());
    } else {
        body_->setGravity(btVector3(0.0f, 0.0f, 0.0f));
    }
    body_->activate(true);
}

void PhysicsRigidBodyBullet::setGravityFactor(float gravityFactor) {
    if (!body_ || !world_ || !world_->world()) return;
    body_->setGravity(world_->world()->getGravity() * gravityFactor);
    body_->activate(true);
}

float PhysicsRigidBodyBullet::getGravityFactor() const {
    return 1.0f;
}

void PhysicsRigidBodyBullet::setTrigger(bool trigger) {
    if (!body_) return;
    int flags = body_->getCollisionFlags();
    if (trigger) {
        flags |= btCollisionObject::CF_NO_CONTACT_RESPONSE;
    } else {
        flags &= ~btCollisionObject::CF_NO_CONTACT_RESPONSE;
    }
    body_->setCollisionFlags(flags);
    body_->activate(true);
}

void PhysicsRigidBodyBullet::setFriction(float friction) {
    if (body_) body_->setFriction(friction);
}

float PhysicsRigidBodyBullet::getFriction() const {
    return body_ ? body_->getFriction() : 0.0f;
}

void PhysicsRigidBodyBullet::setRestitution(float restitution) {
    if (body_) body_->setRestitution(restitution);
}

float PhysicsRigidBodyBullet::getRestitution() const {
    return body_ ? body_->getRestitution() : 0.0f;
}

void PhysicsRigidBodyBullet::setUseManifoldReduction(bool /*enabled*/) {}

bool PhysicsRigidBodyBullet::getUseManifoldReduction() const {
    return false;
}

void PhysicsRigidBodyBullet::setUserData(uint64_t userData) {
    if (body_) body_->setUserPointer(reinterpret_cast<void*>(static_cast<std::uintptr_t>(userData)));
}

uint64_t PhysicsRigidBodyBullet::getUserData() const {
    return body_ ? reinterpret_cast<std::uintptr_t>(body_->getUserPointer()) : 0;
}

void PhysicsRigidBodyBullet::activate() {
    if (body_) body_->activate(true);
}

void PhysicsRigidBodyBullet::deactivate() {
    if (body_) body_->forceActivationState(ISLAND_SLEEPING);
}

void PhysicsRigidBodyBullet::resetSleepTimer() {
    if (body_) body_->activate(true);
}

bool PhysicsRigidBodyBullet::setShape(const karma::physics::PhysicsShapeDesc& /*shape*/,
                                      bool /*updateMassProperties*/,
                                      bool /*activate*/) {
    return false;
}

void PhysicsRigidBodyBullet::addForce(const glm::vec3& force) {
    if (!body_) return;
    body_->applyCentralForce(toBt(force));
    body_->activate(true);
}

void PhysicsRigidBodyBullet::addForceAtPosition(const glm::vec3& force, const glm::vec3& position) {
    if (!body_) return;
    const btVector3 rel_pos = toBt(position) - body_->getCenterOfMassPosition();
    body_->applyForce(toBt(force), rel_pos);
    body_->activate(true);
}

void PhysicsRigidBodyBullet::addTorque(const glm::vec3& torque) {
    if (!body_) return;
    body_->applyTorque(toBt(torque));
    body_->activate(true);
}

void PhysicsRigidBodyBullet::addImpulse(const glm::vec3& impulse) {
    if (!body_) return;
    body_->applyCentralImpulse(toBt(impulse));
    body_->activate(true);
}

void PhysicsRigidBodyBullet::addImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& position) {
    if (!body_) return;
    const btVector3 rel_pos = toBt(position) - body_->getCenterOfMassPosition();
    body_->applyImpulse(toBt(impulse), rel_pos);
    body_->activate(true);
}

void PhysicsRigidBodyBullet::addAngularImpulse(const glm::vec3& impulse) {
    if (!body_) return;
    body_->applyTorqueImpulse(toBt(impulse));
    body_->activate(true);
}

bool PhysicsRigidBodyBullet::applyBuoyancyImpulse(const karma::physics::PhysicsBuoyancyDesc& /*desc*/) {
    return false;
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

} // namespace karma::physics::backend
