#include "karma/simulation/physics/vehicle.hpp"

#include <utility>

#include "karma/simulation/physics/backend.hpp"

namespace karma::physics {

Vehicle::Vehicle(std::unique_ptr<karma::physics_backend::PhysicsVehicleBackend> backend)
    : backend_(std::move(backend)) {}

Vehicle::~Vehicle() {
    destroy();
}

bool Vehicle::isValid() const {
    return backend_ && backend_->isValid();
}

void Vehicle::setInput(const PhysicsVehicleInput& input) {
    if (backend_) {
        backend_->setInput(input);
    }
}

PhysicsVehicleState Vehicle::getState() const {
    return backend_ ? backend_->getState() : PhysicsVehicleState{};
}

void Vehicle::setEnabled(bool enabled) {
    if (backend_) {
        backend_->setEnabled(enabled);
    }
}

void Vehicle::destroy() {
    if (!backend_) {
        return;
    }
    backend_->destroy();
    backend_.reset();
}

std::uintptr_t Vehicle::nativeHandle() const {
    return backend_ ? backend_->nativeHandle() : 0;
}

}  // namespace karma::physics
