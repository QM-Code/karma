#pragma once

#include <cstdint>

namespace karma::components {

enum class RenderLayer : uint8_t {
  Default = 0,
  World = 1,
  UI = 2,
  Skybox = 3,
  Effects = 4,
  Debug = 5
};

enum class CollisionLayer : uint8_t {
  Default = 0,
  Static = 1,
  Dynamic = 2,
  Character = 3,
  Trigger = 4,
  Projectile = 5
};

constexpr uint32_t layerBit(RenderLayer layer) {
  return 1u << static_cast<uint32_t>(layer);
}

constexpr uint32_t layerBit(CollisionLayer layer) {
  return 1u << static_cast<uint32_t>(layer);
}

}  // namespace karma::components
