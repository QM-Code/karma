#pragma once

#include <cstdint>
#include <memory>

#include "karma/simulation/physics/types.h"

namespace karma::physics_backend {
class PhysicsVehicleBackend;
}

namespace karma::physics {

/// \ingroup karma_physics
/// Move-only wrapper around a backend vehicle constraint/controller.
class Vehicle {
public:
    Vehicle() = default;
    explicit Vehicle(std::unique_ptr<karma::physics_backend::PhysicsVehicleBackend> backend);
    Vehicle(const Vehicle&) = delete;
    Vehicle& operator=(const Vehicle&) = delete;
    Vehicle(Vehicle&& other) noexcept = default;
    Vehicle& operator=(Vehicle&& other) noexcept = default;
    ~Vehicle();

    bool isValid() const;
    void setInput(const PhysicsVehicleInput& input);
    PhysicsVehicleState getState() const;
    void setEnabled(bool enabled);
    void destroy();
    std::uintptr_t nativeHandle() const;

private:
    std::unique_ptr<karma::physics_backend::PhysicsVehicleBackend> backend_;
};

}  // namespace karma::physics
