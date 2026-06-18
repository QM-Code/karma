#include "karma/simulation/physics/character_controller.hpp"
#include "karma/simulation/physics/backend.hpp"

namespace karma::physics {

CharacterController::CharacterController(std::unique_ptr<karma::physics_backend::PhysicsCharacterControllerBackend> backend)
    : backend_(std::move(backend)) {}

CharacterController::~CharacterController() {
    destroy();
}

glm::vec3 CharacterController::getPosition() const {
    return backend_ ? backend_->getPosition() : glm::vec3(0.0f);
}

glm::quat CharacterController::getRotation() const {
    return backend_ ? backend_->getRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::vec3 CharacterController::getVelocity() const {
    return backend_ ? backend_->getVelocity() : glm::vec3(0.0f);
}

glm::vec3 CharacterController::getAngularVelocity() const {
    return backend_ ? backend_->getAngularVelocity() : glm::vec3(0.0f);
}

glm::vec3 CharacterController::getForwardVector() const {
    return backend_ ? backend_->getForwardVector() : glm::vec3(0.0f, 0.0f, -1.0f);
}

void CharacterController::setHalfExtents(const glm::vec3& extents) {
    if (backend_) {
        backend_->setHalfExtents(extents);
    }
}

void CharacterController::setCenter(const glm::vec3& center) {
    center_ = center;
    if (backend_) {
        backend_->setCenter(center);
    }
}

void CharacterController::update(float dt) {
    if (backend_) {
        backend_->update(dt);
    }
}

void CharacterController::setPosition(const glm::vec3& position) {
    if (backend_) {
        backend_->setPosition(position);
    }
}

void CharacterController::setRotation(const glm::quat& rotation) {
    if (backend_) {
        backend_->setRotation(rotation);
    }
}

void CharacterController::setVelocity(const glm::vec3& velocity) {
    if (backend_) {
        backend_->setVelocity(velocity);
    }
}

void CharacterController::setAngularVelocity(const glm::vec3& angularVelocity) {
    if (backend_) {
        backend_->setAngularVelocity(angularVelocity);
    }
}

bool CharacterController::isGrounded() const {
    return backend_ ? backend_->isGrounded() : false;
}

bool CharacterController::getGroundContact(PhysicsGroundContact& outContact) const {
    return backend_ ? backend_->getGroundContact(outContact) : false;
}

void CharacterController::collectContacts(std::vector<PhysicsContact>& outContacts) const {
    if (backend_) {
        backend_->collectContacts(outContacts);
    }
}

std::uintptr_t CharacterController::nativeHandle() const {
    return backend_ ? backend_->nativeHandle() : 0;
}

void CharacterController::destroy() {
    if (!backend_) {
        return;
    }
    backend_->destroy();
    backend_.reset();
}

} // namespace karma::physics
