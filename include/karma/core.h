#pragma once





#include <cstdint>

namespace karma::core {

/// \ingroup karma_core
/// Generational handle used for ECS entities and scene-facing references.
///
/// The `index` selects a slot and `generation` prevents stale handles from
/// becoming valid after a slot is reused. Default-constructed handles are
/// invalid and can be used as nullable entity references.
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


#include <chrono>

namespace karma::core {

/// \ingroup karma_core
/// Monotonic clock used by runtime frame timing and diagnostics.
using SteadyClock = std::chrono::steady_clock;

/// Returns elapsed time in milliseconds between two steady-clock points.
inline double elapsedMilliseconds(SteadyClock::time_point start,
                                  SteadyClock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

/// Returns elapsed time in seconds between two steady-clock points.
inline float elapsedSeconds(SteadyClock::time_point start,
                            SteadyClock::time_point end) {
  return std::chrono::duration<float>(end - start).count();
}

/// Returns milliseconds elapsed since `start` and `SteadyClock::now()`.
inline double elapsedMillisecondsSince(SteadyClock::time_point start) {
  return elapsedMilliseconds(start, SteadyClock::now());
}

}  // namespace karma::core
