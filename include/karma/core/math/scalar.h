#pragma once

#include <algorithm>

namespace karma::math {

/// \ingroup karma_core
/// Clamps a scalar to the inclusive range [`min_value`, `max_value`].
inline float clamp(float value, float min_value, float max_value) {
  return std::clamp(value, min_value, max_value);
}

/// \ingroup karma_core
/// Clamps a scalar to the normalized range [0, 1].
inline float clamp01(float value) {
  return clamp(value, 0.0f, 1.0f);
}

/// \ingroup karma_core
/// Linearly interpolates between two scalars.
inline float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

}  // namespace karma::math
