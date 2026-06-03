#pragma once

#include <string_view>

#include "karma/world/ecs/world.h"

namespace karma::systems {

/// \ingroup karma_systems
/// Minimal ECS system interface used by `SystemGraph`.
class ISystem {
 public:
  virtual ~ISystem() = default;

  /// Human-readable system name used in diagnostics/debug UI.
  virtual std::string_view name() const = 0;
  /// Updates system-owned behavior for one frame or fixed step.
  virtual void update(ecs::World& world, float dt) = 0;
};

}  // namespace karma::systems
