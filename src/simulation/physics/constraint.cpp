#include "karma/physics.h"

#include <utility>

#include "private/physics/objects.hpp"

namespace karma::physics {

Constraint::Constraint() = default;

Constraint::Constraint(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

Constraint::Constraint(Constraint&& other) noexcept = default;
Constraint& Constraint::operator=(Constraint&& other) noexcept = default;

Constraint::~Constraint() {
    destroy();
}

bool Constraint::isValid() const {
    return impl_ && impl_->backend && impl_->backend->isValid();
}

void Constraint::setEnabled(bool enabled) {
    if (impl_ && impl_->backend) {
        impl_->backend->setEnabled(enabled);
    }
}

void Constraint::destroy() {
    if (!impl_ || !impl_->backend) {
        return;
    }
    impl_->backend->destroy();
    impl_.reset();
}

std::uintptr_t Constraint::nativeHandle() const {
    return impl_ && impl_->backend ? impl_->backend->nativeHandle() : 0;
}

}  // namespace karma::physics
