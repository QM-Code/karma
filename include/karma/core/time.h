#pragma once

#include <chrono>

namespace karma::core {

using SteadyClock = std::chrono::steady_clock;

inline double elapsedMilliseconds(SteadyClock::time_point start,
                                  SteadyClock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

inline float elapsedSeconds(SteadyClock::time_point start,
                            SteadyClock::time_point end) {
  return std::chrono::duration<float>(end - start).count();
}

inline double elapsedMillisecondsSince(SteadyClock::time_point start) {
  return elapsedMilliseconds(start, SteadyClock::now());
}

}  // namespace karma::core
