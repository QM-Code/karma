#include "karma/physics/static_body.hpp"
#include "karma/physics/backend.hpp"

namespace karma::physics {

StaticBody::StaticBody(std::unique_ptr<karma::physics_backend::PhysicsStaticBodyBackend> backend)
    : backend_(std::move(backend)) {}

StaticBody::~StaticBody() {
    destroy();
}

bool StaticBody::isValid() const {
    return backend_ && backend_->isValid();
}

glm::vec3 StaticBody::getPosition() const {
    return backend_ ? backend_->getPosition() : glm::vec3(0.0f);
}

glm::quat StaticBody::getRotation() const {
    return backend_ ? backend_->getRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

void StaticBody::destroy() {
    if (!backend_) {
        return;
    }
    backend_->destroy();
    backend_.reset();
}

std::uintptr_t StaticBody::nativeHandle() const {
    return backend_ ? backend_->nativeHandle() : 0;
}

} // namespace karma::physics
