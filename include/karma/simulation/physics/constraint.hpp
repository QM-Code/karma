#pragma once

#include "karma/simulation/physics/backend.hpp"

#include <cstdint>
#include <memory>

namespace karma::physics {

/// \ingroup karma_physics
/// Move-only wrapper around a backend two-body constraint.
class Constraint {
public:
    Constraint() = default;
    explicit Constraint(std::unique_ptr<karma::physics_backend::PhysicsConstraintBackend> backend);
    Constraint(const Constraint&) = delete;
    Constraint& operator=(const Constraint&) = delete;
    Constraint(Constraint&& other) noexcept = default;
    Constraint& operator=(Constraint&& other) noexcept = default;
    ~Constraint();

    bool isValid() const;
    void setEnabled(bool enabled);
    void destroy();
    std::uintptr_t nativeHandle() const;

private:
    std::unique_ptr<karma::physics_backend::PhysicsConstraintBackend> backend_;
};

}  // namespace karma::physics
