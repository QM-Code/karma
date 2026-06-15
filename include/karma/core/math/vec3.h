#pragma once

#include <cmath>

#include "karma/core/math/scalar.h"
#include "karma/core/math/types.h"

namespace karma::math {

/// \ingroup karma_core
/// Adds two vectors component-wise.
inline Vec3 add(const Vec3& a, const Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

/// \ingroup karma_core
/// Subtracts `b` from `a` component-wise.
inline Vec3 subtract(const Vec3& a, const Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

/// \ingroup karma_core
/// Multiplies each component by `s`.
inline Vec3 scale(const Vec3& v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

/// \ingroup karma_core
/// Multiplies two vectors component-wise.
inline Vec3 multiply(const Vec3& a, const Vec3& b) {
  return {a.x * b.x, a.y * b.y, a.z * b.z};
}

/// \ingroup karma_core
/// Linearly interpolates between two vectors component-wise.
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
  return {
      lerp(a.x, b.x, t),
      lerp(a.y, b.y, t),
      lerp(a.z, b.z, t),
  };
}

/// \ingroup karma_core
/// Returns the dot product of two vectors.
inline float dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// \ingroup karma_core
/// Returns the right-handed cross product of two vectors.
inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y,
          a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}

/// \ingroup karma_core
/// Returns squared vector length without taking a square root.
inline float lengthSquared(const Vec3& v) {
  return dot(v, v);
}

/// \ingroup karma_core
/// Returns Euclidean vector length.
inline float length(const Vec3& v) {
  return std::sqrt(lengthSquared(v));
}

/// \ingroup karma_core
/// Returns a unit vector or zero when the input is too small to normalize.
inline Vec3 normalize(const Vec3& v) {
  const float len = length(v);
  if (len <= 0.0001f) {
    return {0.0f, 0.0f, 0.0f};
  }
  return {v.x / len, v.y / len, v.z / len};
}

}  // namespace karma::math
