#include "karma/physics/player_controller.hpp"
#include "karma/physics/backend.hpp"

namespace karma::physics {

PlayerController::PlayerController(std::unique_ptr<karma::physics_backend::PhysicsPlayerControllerBackend> backend)
    : backend_(std::move(backend)) {}

PlayerController::~PlayerController() {
    destroy();
}

glm::vec3 PlayerController::getPosition() const {
    return backend_ ? backend_->getPosition() : glm::vec3(0.0f);
}

glm::quat PlayerController::getRotation() const {
    return backend_ ? backend_->getRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::vec3 PlayerController::getVelocity() const {
    return backend_ ? backend_->getVelocity() : glm::vec3(0.0f);
}

glm::vec3 PlayerController::getAngularVelocity() const {
    return backend_ ? backend_->getAngularVelocity() : glm::vec3(0.0f);
}

glm::vec3 PlayerController::getForwardVector() const {
    return backend_ ? backend_->getForwardVector() : glm::vec3(0.0f, 0.0f, -1.0f);
}

void PlayerController::setHalfExtents(const glm::vec3& extents) {
    if (backend_) {
        backend_->setHalfExtents(extents);
    }
}

void PlayerController::update(float dt) {
    if (backend_) {
        backend_->update(dt);
    }
}

void PlayerController::setPosition(const glm::vec3& position) {
    if (backend_) {
        backend_->setPosition(position);
    }
}

void PlayerController::setRotation(const glm::quat& rotation) {
    if (backend_) {
        backend_->setRotation(rotation);
    }
}

void PlayerController::setVelocity(const glm::vec3& velocity) {
    if (backend_) {
        backend_->setVelocity(velocity);
    }
}

void PlayerController::setAngularVelocity(const glm::vec3& angularVelocity) {
    if (backend_) {
        backend_->setAngularVelocity(angularVelocity);
    }
}

bool PlayerController::isGrounded() const {
    return backend_ ? backend_->isGrounded() : false;
}

void PlayerController::destroy() {
    if (!backend_) {
        return;
    }
    backend_->destroy();
    backend_.reset();
}

} // namespace karma::physics
