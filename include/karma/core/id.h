#pragma once

#include <cstdint>

namespace karma::core {

struct EntityId {
  uint32_t index = kInvalidIndex;
  uint32_t generation = 0;

  static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

  constexpr bool isValid() const { return index != kInvalidIndex; }

  friend constexpr bool operator==(const EntityId& a, const EntityId& b) {
    return a.index == b.index && a.generation == b.generation;
  }

  friend constexpr bool operator!=(const EntityId& a, const EntityId& b) {
    return !(a == b);
  }
};

}  // namespace karma::core
