#pragma once

#include "karma/physics.h"
#include "karma/physics.h"
#include "karma/physics.h"
#include "karma/physics.h"
#include "karma/physics.h"
#include "karma/physics.h"
#include "private/physics/backend.hpp"

namespace karma::physics {

struct Constraint::Impl {
  explicit Impl(std::unique_ptr<backend::PhysicsConstraintBackend> backend)
      : backend(std::move(backend)) {}

  std::unique_ptr<backend::PhysicsConstraintBackend> backend;
};

struct RigidBody::Impl {
  explicit Impl(std::unique_ptr<backend::PhysicsRigidBodyBackend> backend)
      : backend(std::move(backend)) {}

  std::unique_ptr<backend::PhysicsRigidBodyBackend> backend;
};

struct CharacterController::Impl {
  explicit Impl(std::unique_ptr<backend::PhysicsCharacterControllerBackend> backend)
      : backend(std::move(backend)) {}

  std::unique_ptr<backend::PhysicsCharacterControllerBackend> backend;
};

struct SoftBody::Impl {
  explicit Impl(std::unique_ptr<backend::PhysicsSoftBodyBackend> backend)
      : backend(std::move(backend)) {}

  std::unique_ptr<backend::PhysicsSoftBodyBackend> backend;
};

struct Vehicle::Impl {
  explicit Impl(std::unique_ptr<backend::PhysicsVehicleBackend> backend)
      : backend(std::move(backend)) {}

  std::unique_ptr<backend::PhysicsVehicleBackend> backend;
};

struct World::Impl {
  std::unique_ptr<backend::PhysicsWorldBackend> backend;
};

}  // namespace karma::physics
