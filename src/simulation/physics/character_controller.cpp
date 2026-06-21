#include "karma/physics.h"
#include "private/physics/objects.hpp"

namespace karma::physics {

CharacterController::CharacterController() = default;

CharacterController::CharacterController(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

CharacterController::CharacterController(CharacterController&& other) noexcept = default;
CharacterController& CharacterController::operator=(CharacterController&& other) noexcept = default;

CharacterController::~CharacterController() {
    destroy();
}

glm::vec3 CharacterController::getPosition() const {
    return impl_ && impl_->backend ? impl_->backend->getPosition() : glm::vec3(0.0f);
}

glm::quat CharacterController::getRotation() const {
    return impl_ && impl_->backend ? impl_->backend->getRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::vec3 CharacterController::getVelocity() const {
    return impl_ && impl_->backend ? impl_->backend->getVelocity() : glm::vec3(0.0f);
}

glm::vec3 CharacterController::getAngularVelocity() const {
    return impl_ && impl_->backend ? impl_->backend->getAngularVelocity() : glm::vec3(0.0f);
}

glm::vec3 CharacterController::getForwardVector() const {
    return impl_ && impl_->backend ? impl_->backend->getForwardVector() : glm::vec3(0.0f, 0.0f, -1.0f);
}

void CharacterController::setHalfExtents(const glm::vec3& extents) {
    if (impl_ && impl_->backend) {
        impl_->backend->setHalfExtents(extents);
    }
}

void CharacterController::setCenter(const glm::vec3& center) {
    center_ = center;
    if (impl_ && impl_->backend) {
        impl_->backend->setCenter(center);
    }
}

void CharacterController::update(float dt) {
    if (impl_ && impl_->backend) {
        impl_->backend->update(dt);
    }
}

void CharacterController::setPosition(const glm::vec3& position) {
    if (impl_ && impl_->backend) {
        impl_->backend->setPosition(position);
    }
}

void CharacterController::setRotation(const glm::quat& rotation) {
    if (impl_ && impl_->backend) {
        impl_->backend->setRotation(rotation);
    }
}

void CharacterController::setVelocity(const glm::vec3& velocity) {
    if (impl_ && impl_->backend) {
        impl_->backend->setVelocity(velocity);
    }
}

void CharacterController::setAngularVelocity(const glm::vec3& angularVelocity) {
    if (impl_ && impl_->backend) {
        impl_->backend->setAngularVelocity(angularVelocity);
    }
}

bool CharacterController::isGrounded() const {
    return impl_ && impl_->backend ? impl_->backend->isGrounded() : false;
}

bool CharacterController::getGroundContact(PhysicsGroundContact& outContact) const {
    return impl_ && impl_->backend ? impl_->backend->getGroundContact(outContact) : false;
}

void CharacterController::collectContacts(std::vector<PhysicsContact>& outContacts) const {
    if (impl_ && impl_->backend) {
        impl_->backend->collectContacts(outContacts);
    }
}

std::uintptr_t CharacterController::nativeHandle() const {
    return impl_ && impl_->backend ? impl_->backend->nativeHandle() : 0;
}

void CharacterController::destroy() {
    if (!impl_ || !impl_->backend) {
        return;
    }
    impl_->backend->destroy();
    impl_.reset();
}

} // namespace karma::physics
