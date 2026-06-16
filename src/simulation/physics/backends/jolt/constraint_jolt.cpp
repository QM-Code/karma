#include "karma/simulation/physics/backends/jolt/constraint_jolt.hpp"

#include "karma/simulation/physics/backends/jolt/physics_world_jolt.hpp"

#include <Jolt/Physics/Constraints/Constraint.h>

namespace karma::physics_backend {

PhysicsConstraintJolt::PhysicsConstraintJolt(PhysicsWorldJolt* world, JPH::Constraint* constraint)
    : world_(world), constraint_(constraint) {}

PhysicsConstraintJolt::~PhysicsConstraintJolt() {
    destroy();
}

bool PhysicsConstraintJolt::isValid() const {
    return world_ != nullptr && constraint_ != nullptr;
}

void PhysicsConstraintJolt::setEnabled(bool enabled) {
    if (constraint_ != nullptr) {
        constraint_->SetEnabled(enabled);
    }
}

void PhysicsConstraintJolt::destroy() {
    if (!world_ || constraint_ == nullptr) {
        return;
    }
    world_->removeConstraint(constraint_.GetPtr());
    constraint_ = nullptr;
    world_ = nullptr;
}

std::uintptr_t PhysicsConstraintJolt::nativeHandle() const {
    return reinterpret_cast<std::uintptr_t>(constraint_.GetPtr());
}

}  // namespace karma::physics_backend
