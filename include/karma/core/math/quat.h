#pragma once

#include <cmath>

#include "karma/core/math/types.h"

namespace karma::math {

/// \ingroup karma_core
/// Multiplies two quaternions.
inline Quat mul(const Quat& a, const Quat& b) {
  return {
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
  };
}

/// \ingroup karma_core
/// Returns the dot product of two quaternions.
inline float dot(const Quat& a, const Quat& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/// \ingroup karma_core
/// Returns squared quaternion length without taking a square root.
inline float lengthSquared(const Quat& q) {
  return dot(q, q);
}

/// \ingroup karma_core
/// Returns quaternion length.
inline float length(const Quat& q) {
  return std::sqrt(lengthSquared(q));
}

/// \ingroup karma_core
/// Returns a unit quaternion or identity when the input is too small to normalize.
inline Quat normalize(const Quat& q) {
  const float len = length(q);
  if (len <= 0.000001f) {
    return {};
  }
  return {q.x / len, q.y / len, q.z / len, q.w / len};
}

/// \ingroup karma_core
/// Returns the inverse of a unit quaternion.
inline Quat inverse(const Quat& q) {
  return {-q.x, -q.y, -q.z, q.w};
}

/// \ingroup karma_core
/// Spherically interpolates between two quaternions using the shortest path.
inline Quat slerp(const Quat& a, const Quat& b, float t) {
  Quat end = b;
  float cos_theta = dot(a, end);
  if (cos_theta < 0.0f) {
    end = {-end.x, -end.y, -end.z, -end.w};
    cos_theta = -cos_theta;
  }

  if (cos_theta > 0.9995f) {
    return normalize(Quat{
        a.x + (end.x - a.x) * t,
        a.y + (end.y - a.y) * t,
        a.z + (end.z - a.z) * t,
        a.w + (end.w - a.w) * t,
    });
  }

  const float theta = std::acos(cos_theta);
  const float sin_theta = std::sin(theta);
  const float weight_a = std::sin((1.0f - t) * theta) / sin_theta;
  const float weight_b = std::sin(t * theta) / sin_theta;
  return normalize(Quat{
      a.x * weight_a + end.x * weight_b,
      a.y * weight_a + end.y * weight_b,
      a.z * weight_a + end.z * weight_b,
      a.w * weight_a + end.w * weight_b,
  });
}

/// \ingroup karma_core
/// Builds a yaw-then-pitch orientation in radians.
inline Quat fromYawPitch(float yaw, float pitch) {
  const float half_yaw = yaw * 0.5f;
  const float half_pitch = pitch * 0.5f;
  const Quat qy{0.0f, std::sin(half_yaw), 0.0f, std::cos(half_yaw)};
  const Quat qx{std::sin(half_pitch), 0.0f, 0.0f, std::cos(half_pitch)};
  return mul(qy, qx);
}

/// \ingroup karma_core
/// Rotates a vector by a quaternion.
inline Vec3 rotateVec(const Quat& q, const Vec3& v) {
  const Quat vq{v.x, v.y, v.z, 0.0f};
  const Quat q_conj{-q.x, -q.y, -q.z, q.w};
  const Quat rq = mul(mul(q, vq), q_conj);
  return {rq.x, rq.y, rq.z};
}

}  // namespace karma::math
