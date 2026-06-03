#pragma once

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
