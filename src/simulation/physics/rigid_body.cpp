#include "karma/simulation/physics/rigid_body.hpp"
#include "karma/simulation/physics/backend.hpp"

namespace karma::physics {

RigidBody::RigidBody(std::unique_ptr<karma::physics_backend::PhysicsRigidBodyBackend> backend)
    : backend_(std::move(backend)) {}

RigidBody::~RigidBody() {
    destroy();
}

bool RigidBody::isValid() const {
    return backend_ && backend_->isValid();
}

glm::vec3 RigidBody::getPosition() const {
    return backend_ ? backend_->getPosition() : glm::vec3(0.0f);
}

glm::quat RigidBody::getRotation() const {
    return backend_ ? backend_->getRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::vec3 RigidBody::getVelocity() const {
    return backend_ ? backend_->getVelocity() : glm::vec3(0.0f);
}

glm::vec3 RigidBody::getAngularVelocity() const {
    return backend_ ? backend_->getAngularVelocity() : glm::vec3(0.0f);
}

glm::vec3 RigidBody::getForwardVector() const {
    return backend_ ? backend_->getForwardVector() : glm::vec3(0.0f, 0.0f, -1.0f);
}

bool RigidBody::isActive() const {
    return backend_ ? backend_->isActive() : false;
}

void RigidBody::setPosition(const glm::vec3& position) {
    if (backend_) {
        backend_->setPosition(position);
    }
}

void RigidBody::setRotation(const glm::quat& rotation) {
    if (backend_) {
        backend_->setRotation(rotation);
    }
}

void RigidBody::setPositionAndRotation(const glm::vec3& position, const glm::quat& rotation) {
    if (backend_) {
        backend_->setPositionAndRotation(position, rotation);
    }
}

void RigidBody::moveKinematic(const glm::vec3& targetPosition,
                              const glm::quat& targetRotation,
                              float deltaTime) {
    if (backend_) {
        backend_->moveKinematic(targetPosition, targetRotation, deltaTime);
    }
}

void RigidBody::setVelocity(const glm::vec3& velocity) {
    if (backend_) {
        backend_->setVelocity(velocity);
    }
}

void RigidBody::setAngularVelocity(const glm::vec3& angularVelocity) {
    if (backend_) {
        backend_->setAngularVelocity(angularVelocity);
    }
}

void RigidBody::setLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                            const glm::vec3& angularVelocity) {
    if (backend_) {
        backend_->setLinearAndAngularVelocity(linearVelocity, angularVelocity);
    }
}

void RigidBody::addLinearVelocity(const glm::vec3& velocity) {
    if (backend_) {
        backend_->addLinearVelocity(velocity);
    }
}

void RigidBody::addLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                            const glm::vec3& angularVelocity) {
    if (backend_) {
        backend_->addLinearAndAngularVelocity(linearVelocity, angularVelocity);
    }
}

glm::vec3 RigidBody::getPointVelocity(const glm::vec3& point) const {
    return backend_ ? backend_->getPointVelocity(point) : glm::vec3(0.0f);
}

void RigidBody::setKinematic(bool kinematic) {
    if (backend_) {
        backend_->setKinematic(kinematic);
    }
}

void RigidBody::setMotionQuality(PhysicsMotionQuality quality) {
    if (backend_) {
        backend_->setMotionQuality(quality);
    }
}

void RigidBody::setUseGravity(bool useGravity) {
    if (backend_) {
        backend_->setUseGravity(useGravity);
    }
}

void RigidBody::setGravityFactor(float gravityFactor) {
    if (backend_) {
        backend_->setGravityFactor(gravityFactor);
    }
}

float RigidBody::getGravityFactor() const {
    return backend_ ? backend_->getGravityFactor() : 0.0f;
}

void RigidBody::setTrigger(bool trigger) {
    if (backend_) {
        backend_->setTrigger(trigger);
    }
}

void RigidBody::setFriction(float friction) {
    if (backend_) {
        backend_->setFriction(friction);
    }
}

float RigidBody::getFriction() const {
    return backend_ ? backend_->getFriction() : 0.0f;
}

void RigidBody::setRestitution(float restitution) {
    if (backend_) {
        backend_->setRestitution(restitution);
    }
}

float RigidBody::getRestitution() const {
    return backend_ ? backend_->getRestitution() : 0.0f;
}

void RigidBody::setUseManifoldReduction(bool enabled) {
    if (backend_) {
        backend_->setUseManifoldReduction(enabled);
    }
}

bool RigidBody::getUseManifoldReduction() const {
    return backend_ ? backend_->getUseManifoldReduction() : false;
}

void RigidBody::setUserData(uint64_t userData) {
    if (backend_) {
        backend_->setUserData(userData);
    }
}

uint64_t RigidBody::getUserData() const {
    return backend_ ? backend_->getUserData() : 0;
}

void RigidBody::activate() {
    if (backend_) {
        backend_->activate();
    }
}

void RigidBody::deactivate() {
    if (backend_) {
        backend_->deactivate();
    }
}

void RigidBody::resetSleepTimer() {
    if (backend_) {
        backend_->resetSleepTimer();
    }
}

bool RigidBody::setShape(const PhysicsShapeDesc& shape,
                         bool updateMassProperties,
                         bool activate) {
    return backend_ ? backend_->setShape(shape, updateMassProperties, activate) : false;
}

void RigidBody::addForce(const glm::vec3& force) {
    if (backend_) {
        backend_->addForce(force);
    }
}

void RigidBody::addForceAtPosition(const glm::vec3& force, const glm::vec3& position) {
    if (backend_) {
        backend_->addForceAtPosition(force, position);
    }
}

void RigidBody::addTorque(const glm::vec3& torque) {
    if (backend_) {
        backend_->addTorque(torque);
    }
}

void RigidBody::addImpulse(const glm::vec3& impulse) {
    if (backend_) {
        backend_->addImpulse(impulse);
    }
}

void RigidBody::addImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& position) {
    if (backend_) {
        backend_->addImpulseAtPosition(impulse, position);
    }
}

void RigidBody::addAngularImpulse(const glm::vec3& impulse) {
    if (backend_) {
        backend_->addAngularImpulse(impulse);
    }
}

bool RigidBody::applyBuoyancyImpulse(const PhysicsBuoyancyDesc& desc) {
    return backend_ ? backend_->applyBuoyancyImpulse(desc) : false;
}

bool RigidBody::isGrounded(const glm::vec3& dimensions) const {
    return backend_ ? backend_->isGrounded(dimensions) : false;
}

void RigidBody::destroy() {
    if (!backend_) {
        return;
    }
    backend_->destroy();
    backend_.reset();
}

std::uintptr_t RigidBody::nativeHandle() const {
    return backend_ ? backend_->nativeHandle() : 0;
}

} // namespace karma::physics
