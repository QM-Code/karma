#include "karma/physics/rigid_body.hpp"
#include "karma/physics/backend.hpp"

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
