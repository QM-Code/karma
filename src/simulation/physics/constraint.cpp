#include "karma/simulation/physics/constraint.hpp"

#include <utility>

namespace karma::physics {

Constraint::Constraint(std::unique_ptr<karma::physics_backend::PhysicsConstraintBackend> backend)
    : backend_(std::move(backend)) {}

Constraint::~Constraint() {
    destroy();
}

bool Constraint::isValid() const {
    return backend_ && backend_->isValid();
}

void Constraint::setEnabled(bool enabled) {
    if (backend_) {
        backend_->setEnabled(enabled);
    }
}

void Constraint::destroy() {
    if (!backend_) {
        return;
    }
    backend_->destroy();
    backend_.reset();
}

std::uintptr_t Constraint::nativeHandle() const {
    return backend_ ? backend_->nativeHandle() : 0;
}

}  // namespace karma::physics
