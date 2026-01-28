#pragma once

#include <cstdint>
#include <cstddef>

namespace karma::core {

using TypeId = uint32_t;

inline TypeId nextTypeId() {
  static TypeId counter = 1;
  return counter++;
}

template <typename T>
TypeId typeId() {
  static const TypeId id = nextTypeId();
  return id;
}

}  // namespace karma::core
