#pragma once

#include <string_view>

#include "karma/world/systems/system.h"

namespace karma::visual {

class LightPulseSystem final : public systems::ISystem {
 public:
  std::string_view name() const override { return "LightPulseSystem"; }
  void update(ecs::World& world, float dt) override;
};

}  // namespace karma::visual
