#pragma once

#include <cmath>

#include "karma/components/transform.h"

namespace karma::math {

inline float dot(const components::Vec3& a, const components::Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline components::Vec3 cross(const components::Vec3& a, const components::Vec3& b) {
  return {a.y * b.z - a.z * b.y,
          a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}

inline float lengthSquared(const components::Vec3& v) {
  return dot(v, v);
}

inline float length(const components::Vec3& v) {
  return std::sqrt(lengthSquared(v));
}

inline components::Vec3 normalize(const components::Vec3& v) {
  const float len = length(v);
  if (len <= 0.0001f) {
    return {0.0f, 0.0f, 0.0f};
  }
  return {v.x / len, v.y / len, v.z / len};
}

}  // namespace karma::math
