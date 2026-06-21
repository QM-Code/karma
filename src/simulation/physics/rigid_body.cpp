#include "karma/physics.h"
#include "private/physics/objects.hpp"

namespace karma::physics {

RigidBody::RigidBody() = default;

RigidBody::RigidBody(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

RigidBody::RigidBody(RigidBody&& other) noexcept = default;
RigidBody& RigidBody::operator=(RigidBody&& other) noexcept = default;

RigidBody::~RigidBody() {
    destroy();
}

bool RigidBody::isValid() const {
    return impl_ && impl_->backend && impl_->backend->isValid();
}

glm::vec3 RigidBody::getPosition() const {
    return impl_ && impl_->backend ? impl_->backend->getPosition() : glm::vec3(0.0f);
}

glm::quat RigidBody::getRotation() const {
    return impl_ && impl_->backend ? impl_->backend->getRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::vec3 RigidBody::getVelocity() const {
    return impl_ && impl_->backend ? impl_->backend->getVelocity() : glm::vec3(0.0f);
}

glm::vec3 RigidBody::getAngularVelocity() const {
    return impl_ && impl_->backend ? impl_->backend->getAngularVelocity() : glm::vec3(0.0f);
}

glm::vec3 RigidBody::getForwardVector() const {
    return impl_ && impl_->backend ? impl_->backend->getForwardVector() : glm::vec3(0.0f, 0.0f, -1.0f);
}

bool RigidBody::isActive() const {
    return impl_ && impl_->backend ? impl_->backend->isActive() : false;
}

void RigidBody::setPosition(const glm::vec3& position) {
    if (impl_ && impl_->backend) {
        impl_->backend->setPosition(position);
    }
}

void RigidBody::setRotation(const glm::quat& rotation) {
    if (impl_ && impl_->backend) {
        impl_->backend->setRotation(rotation);
    }
}

void RigidBody::setPositionAndRotation(const glm::vec3& position, const glm::quat& rotation) {
    if (impl_ && impl_->backend) {
        impl_->backend->setPositionAndRotation(position, rotation);
    }
}

void RigidBody::moveKinematic(const glm::vec3& targetPosition,
                              const glm::quat& targetRotation,
                              float deltaTime) {
    if (impl_ && impl_->backend) {
        impl_->backend->moveKinematic(targetPosition, targetRotation, deltaTime);
    }
}

void RigidBody::setVelocity(const glm::vec3& velocity) {
    if (impl_ && impl_->backend) {
        impl_->backend->setVelocity(velocity);
    }
}

void RigidBody::setAngularVelocity(const glm::vec3& angularVelocity) {
    if (impl_ && impl_->backend) {
        impl_->backend->setAngularVelocity(angularVelocity);
    }
}

void RigidBody::setLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                            const glm::vec3& angularVelocity) {
    if (impl_ && impl_->backend) {
        impl_->backend->setLinearAndAngularVelocity(linearVelocity, angularVelocity);
    }
}

void RigidBody::addLinearVelocity(const glm::vec3& velocity) {
    if (impl_ && impl_->backend) {
        impl_->backend->addLinearVelocity(velocity);
    }
}

void RigidBody::addLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                            const glm::vec3& angularVelocity) {
    if (impl_ && impl_->backend) {
        impl_->backend->addLinearAndAngularVelocity(linearVelocity, angularVelocity);
    }
}

glm::vec3 RigidBody::getPointVelocity(const glm::vec3& point) const {
    return impl_ && impl_->backend ? impl_->backend->getPointVelocity(point) : glm::vec3(0.0f);
}

void RigidBody::setKinematic(bool kinematic) {
    if (impl_ && impl_->backend) {
        impl_->backend->setKinematic(kinematic);
    }
}

void RigidBody::setMotionQuality(PhysicsMotionQuality quality) {
    if (impl_ && impl_->backend) {
        impl_->backend->setMotionQuality(quality);
    }
}

void RigidBody::setUseGravity(bool useGravity) {
    if (impl_ && impl_->backend) {
        impl_->backend->setUseGravity(useGravity);
    }
}

void RigidBody::setGravityFactor(float gravityFactor) {
    if (impl_ && impl_->backend) {
        impl_->backend->setGravityFactor(gravityFactor);
    }
}

float RigidBody::getGravityFactor() const {
    return impl_ && impl_->backend ? impl_->backend->getGravityFactor() : 0.0f;
}

void RigidBody::setTrigger(bool trigger) {
    if (impl_ && impl_->backend) {
        impl_->backend->setTrigger(trigger);
    }
}

void RigidBody::setFriction(float friction) {
    if (impl_ && impl_->backend) {
        impl_->backend->setFriction(friction);
    }
}

float RigidBody::getFriction() const {
    return impl_ && impl_->backend ? impl_->backend->getFriction() : 0.0f;
}

void RigidBody::setRestitution(float restitution) {
    if (impl_ && impl_->backend) {
        impl_->backend->setRestitution(restitution);
    }
}

float RigidBody::getRestitution() const {
    return impl_ && impl_->backend ? impl_->backend->getRestitution() : 0.0f;
}

void RigidBody::setUseManifoldReduction(bool enabled) {
    if (impl_ && impl_->backend) {
        impl_->backend->setUseManifoldReduction(enabled);
    }
}

bool RigidBody::getUseManifoldReduction() const {
    return impl_ && impl_->backend ? impl_->backend->getUseManifoldReduction() : false;
}

void RigidBody::setUserData(uint64_t userData) {
    if (impl_ && impl_->backend) {
        impl_->backend->setUserData(userData);
    }
}

uint64_t RigidBody::getUserData() const {
    return impl_ && impl_->backend ? impl_->backend->getUserData() : 0;
}

void RigidBody::activate() {
    if (impl_ && impl_->backend) {
        impl_->backend->activate();
    }
}

void RigidBody::deactivate() {
    if (impl_ && impl_->backend) {
        impl_->backend->deactivate();
    }
}

void RigidBody::resetSleepTimer() {
    if (impl_ && impl_->backend) {
        impl_->backend->resetSleepTimer();
    }
}

bool RigidBody::setShape(const PhysicsShapeDesc& shape,
                         bool updateMassProperties,
                         bool activate) {
    return impl_ && impl_->backend ? impl_->backend->setShape(shape, updateMassProperties, activate) : false;
}

void RigidBody::addForce(const glm::vec3& force) {
    if (impl_ && impl_->backend) {
        impl_->backend->addForce(force);
    }
}

void RigidBody::addForceAtPosition(const glm::vec3& force, const glm::vec3& position) {
    if (impl_ && impl_->backend) {
        impl_->backend->addForceAtPosition(force, position);
    }
}

void RigidBody::addTorque(const glm::vec3& torque) {
    if (impl_ && impl_->backend) {
        impl_->backend->addTorque(torque);
    }
}

void RigidBody::addImpulse(const glm::vec3& impulse) {
    if (impl_ && impl_->backend) {
        impl_->backend->addImpulse(impulse);
    }
}

void RigidBody::addImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& position) {
    if (impl_ && impl_->backend) {
        impl_->backend->addImpulseAtPosition(impulse, position);
    }
}

void RigidBody::addAngularImpulse(const glm::vec3& impulse) {
    if (impl_ && impl_->backend) {
        impl_->backend->addAngularImpulse(impulse);
    }
}

bool RigidBody::applyBuoyancyImpulse(const PhysicsBuoyancyDesc& desc) {
    return impl_ && impl_->backend ? impl_->backend->applyBuoyancyImpulse(desc) : false;
}

bool RigidBody::isGrounded(const glm::vec3& dimensions) const {
    return impl_ && impl_->backend ? impl_->backend->isGrounded(dimensions) : false;
}

void RigidBody::destroy() {
    if (!impl_ || !impl_->backend) {
        return;
    }
    impl_->backend->destroy();
    impl_.reset();
}

std::uintptr_t RigidBody::nativeHandle() const {
    return impl_ && impl_->backend ? impl_->backend->nativeHandle() : 0;
}

} // namespace karma::physics
