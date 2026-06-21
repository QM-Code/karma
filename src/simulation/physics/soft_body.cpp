#include "karma/physics.h"

#include <utility>

#include "private/physics/objects.hpp"

namespace karma::physics {

SoftBody::SoftBody() = default;

SoftBody::SoftBody(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

SoftBody::SoftBody(SoftBody&& other) noexcept = default;
SoftBody& SoftBody::operator=(SoftBody&& other) noexcept = default;

SoftBody::~SoftBody() {
    destroy();
}

bool SoftBody::isValid() const {
    return impl_ && impl_->backend && impl_->backend->isValid();
}

glm::vec3 SoftBody::getPosition() const {
    return impl_ && impl_->backend ? impl_->backend->getPosition() : glm::vec3(0.0f);
}

glm::quat SoftBody::getRotation() const {
    return impl_ && impl_->backend ? impl_->backend->getRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

bool SoftBody::isActive() const {
    return impl_ && impl_->backend ? impl_->backend->isActive() : false;
}

void SoftBody::setPressure(float pressure) {
    if (impl_ && impl_->backend) {
        impl_->backend->setPressure(pressure);
    }
}

void SoftBody::setUpdatePosition(bool updatePosition) {
    if (impl_ && impl_->backend) {
        impl_->backend->setUpdatePosition(updatePosition);
    }
}

void SoftBody::setEnableSkinConstraints(bool enabled) {
    if (impl_ && impl_->backend) {
        impl_->backend->setEnableSkinConstraints(enabled);
    }
}

void SoftBody::setSkinnedMaxDistanceMultiplier(float multiplier) {
    if (impl_ && impl_->backend) {
        impl_->backend->setSkinnedMaxDistanceMultiplier(multiplier);
    }
}

void SoftBody::setVertexPosition(uint32_t vertex, const glm::vec3& position, bool hardSkin) {
    if (impl_ && impl_->backend) {
        impl_->backend->setVertexPosition(vertex, position, hardSkin);
    }
}

PhysicsSoftBodyState SoftBody::getState() const {
    return impl_ && impl_->backend ? impl_->backend->getState() : PhysicsSoftBodyState{};
}

void SoftBody::activate() {
    if (impl_ && impl_->backend) {
        impl_->backend->activate();
    }
}

void SoftBody::deactivate() {
    if (impl_ && impl_->backend) {
        impl_->backend->deactivate();
    }
}

void SoftBody::destroy() {
    if (!impl_ || !impl_->backend) {
        return;
    }
    impl_->backend->destroy();
    impl_.reset();
}

std::uintptr_t SoftBody::nativeHandle() const {
    return impl_ && impl_->backend ? impl_->backend->nativeHandle() : 0;
}

}  // namespace karma::physics
