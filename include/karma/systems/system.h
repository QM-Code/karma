#pragma once

#include <string_view>

#include "karma/ecs/world.h"

namespace karma::systems {

class ISystem {
 public:
  virtual ~ISystem() = default;

  virtual std::string_view name() const = 0;
  virtual void update(ecs::World& world, float dt) = 0;
};

}  // namespace karma::systems
