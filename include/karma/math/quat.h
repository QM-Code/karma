#pragma once

#include <cmath>

#include "karma/components/transform.h"

namespace karma::math {

inline components::Quat mul(const components::Quat& a, const components::Quat& b) {
  return {
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
  };
}

inline components::Quat fromYawPitch(float yaw, float pitch) {
  const float half_yaw = yaw * 0.5f;
  const float half_pitch = pitch * 0.5f;
  const components::Quat qy{0.0f, std::sin(half_yaw), 0.0f, std::cos(half_yaw)};
  const components::Quat qx{std::sin(half_pitch), 0.0f, 0.0f, std::cos(half_pitch)};
  return mul(qy, qx);
}

inline components::Vec3 rotateVec(const components::Quat& q, const components::Vec3& v) {
  const components::Quat vq{v.x, v.y, v.z, 0.0f};
  const components::Quat q_conj{-q.x, -q.y, -q.z, q.w};
  const components::Quat rq = mul(mul(q, vq), q_conj);
  return {rq.x, rq.y, rq.z};
}

}  // namespace karma::math
