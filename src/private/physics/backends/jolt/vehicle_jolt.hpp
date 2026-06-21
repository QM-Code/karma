#pragma once

#include "private/physics/backend.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>

namespace karma::physics::backend {

class PhysicsWorldJolt;

/// \ingroup karma_physics
/// Jolt-backed vehicle constraint/controller.
class PhysicsVehicleJolt final : public PhysicsVehicleBackend {
public:
    PhysicsVehicleJolt() = default;
    PhysicsVehicleJolt(PhysicsWorldJolt* world,
                       JPH::VehicleConstraint* vehicle,
                       JPH::VehicleCollisionTester* tester,
                       karma::physics::PhysicsVehicleControllerType controller,
                       bool manualTransmission);
    ~PhysicsVehicleJolt() override;

    bool isValid() const override;
    void setInput(const karma::physics::PhysicsVehicleInput& input) override;
    karma::physics::PhysicsVehicleState getState() const override;
    void setEnabled(bool enabled) override;
    void destroy() override;
    std::uintptr_t nativeHandle() const override;

private:
    PhysicsWorldJolt* world_ = nullptr;
    JPH::Ref<JPH::VehicleConstraint> vehicle_;
    JPH::Ref<JPH::VehicleCollisionTester> tester_;
    karma::physics::PhysicsVehicleControllerType controller_ =
        karma::physics::PhysicsVehicleControllerType::Wheeled;
    bool manual_transmission_ = false;
};

}  // namespace karma::physics::backend
