#include "karma/simulation/physics/soft_body.hpp"

#include <utility>

#include "karma/simulation/physics/backend.hpp"

namespace karma::physics {

SoftBody::SoftBody(std::unique_ptr<karma::physics_backend::PhysicsSoftBodyBackend> backend)
    : backend_(std::move(backend)) {}

SoftBody::~SoftBody() {
    destroy();
}

bool SoftBody::isValid() const {
    return backend_ && backend_->isValid();
}

glm::vec3 SoftBody::getPosition() const {
    return backend_ ? backend_->getPosition() : glm::vec3(0.0f);
}

glm::quat SoftBody::getRotation() const {
    return backend_ ? backend_->getRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

bool SoftBody::isActive() const {
    return backend_ ? backend_->isActive() : false;
}

void SoftBody::setPressure(float pressure) {
    if (backend_) {
        backend_->setPressure(pressure);
    }
}

void SoftBody::setUpdatePosition(bool updatePosition) {
    if (backend_) {
        backend_->setUpdatePosition(updatePosition);
    }
}

void SoftBody::setEnableSkinConstraints(bool enabled) {
    if (backend_) {
        backend_->setEnableSkinConstraints(enabled);
    }
}

void SoftBody::setSkinnedMaxDistanceMultiplier(float multiplier) {
    if (backend_) {
        backend_->setSkinnedMaxDistanceMultiplier(multiplier);
    }
}

void SoftBody::setVertexPosition(uint32_t vertex, const glm::vec3& position, bool hardSkin) {
    if (backend_) {
        backend_->setVertexPosition(vertex, position, hardSkin);
    }
}

PhysicsSoftBodyState SoftBody::getState() const {
    return backend_ ? backend_->getState() : PhysicsSoftBodyState{};
}

void SoftBody::activate() {
    if (backend_) {
        backend_->activate();
    }
}

void SoftBody::deactivate() {
    if (backend_) {
        backend_->deactivate();
    }
}

void SoftBody::destroy() {
    if (!backend_) {
        return;
    }
    backend_->destroy();
    backend_.reset();
}

std::uintptr_t SoftBody::nativeHandle() const {
    return backend_ ? backend_->nativeHandle() : 0;
}

}  // namespace karma::physics
