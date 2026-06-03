#pragma once

#include <cstdint>
#include <cstddef>

namespace karma::core {

/// \ingroup karma_core
/// Runtime type identifier used by sparse component storage.
using TypeId = uint32_t;

/// Returns the next process-local type id.
///
/// Prefer `typeId<T>()` for component and storage code; this helper is exposed
/// for low-level type registry utilities.
inline TypeId nextTypeId() {
  static TypeId counter = 1;
  return counter++;
}

/// Returns a stable process-local id for `T`.
///
/// The id is stable for the lifetime of the process, but it is not serialized
/// and must not be persisted across runs.
template <typename T>
TypeId typeId() {
  static const TypeId id = nextTypeId();
  return id;
}

}  // namespace karma::core
