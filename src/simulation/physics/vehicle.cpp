#include "karma/physics.h"

#include <utility>

#include "private/physics/objects.hpp"

namespace karma::physics {

Vehicle::Vehicle() = default;

Vehicle::Vehicle(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

Vehicle::Vehicle(Vehicle&& other) noexcept = default;
Vehicle& Vehicle::operator=(Vehicle&& other) noexcept = default;

Vehicle::~Vehicle() {
    destroy();
}

bool Vehicle::isValid() const {
    return impl_ && impl_->backend && impl_->backend->isValid();
}

void Vehicle::setInput(const PhysicsVehicleInput& input) {
    if (impl_ && impl_->backend) {
        impl_->backend->setInput(input);
    }
}

PhysicsVehicleState Vehicle::getState() const {
    return impl_ && impl_->backend ? impl_->backend->getState() : PhysicsVehicleState{};
}

void Vehicle::setEnabled(bool enabled) {
    if (impl_ && impl_->backend) {
        impl_->backend->setEnabled(enabled);
    }
}

void Vehicle::destroy() {
    if (!impl_ || !impl_->backend) {
        return;
    }
    impl_->backend->destroy();
    impl_.reset();
}

std::uintptr_t Vehicle::nativeHandle() const {
    return impl_ && impl_->backend ? impl_->backend->nativeHandle() : 0;
}

}  // namespace karma::physics
