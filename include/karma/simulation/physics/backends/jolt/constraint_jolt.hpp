#pragma once

#include "karma/simulation/physics/backend.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <cstdint>

namespace JPH {
class Constraint;
}

namespace karma::physics_backend {

class PhysicsWorldJolt;

/// \ingroup karma_physics
/// Jolt implementation of `PhysicsConstraintBackend`.
class PhysicsConstraintJolt final : public PhysicsConstraintBackend {
public:
    PhysicsConstraintJolt() = default;
    PhysicsConstraintJolt(PhysicsWorldJolt* world, JPH::Constraint* constraint);
    ~PhysicsConstraintJolt() override;

    bool isValid() const override;
    void setEnabled(bool enabled) override;
    void destroy() override;
    std::uintptr_t nativeHandle() const override;

private:
    PhysicsWorldJolt* world_ = nullptr;
    JPH::Ref<JPH::Constraint> constraint_;
};

}  // namespace karma::physics_backend
