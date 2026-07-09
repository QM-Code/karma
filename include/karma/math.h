#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace karma::math {

/// \ingroup karma_core
/// Small engine-facing 3D vector used by public ECS and runtime APIs.
struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

/// \ingroup karma_core
/// Quaternion stored as `(x, y, z, w)` with identity as the default value.
struct Quat {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
};

/// \ingroup karma_core
/// Linear RGBA color used by renderer-facing and effect-facing APIs.
struct Color {
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
};

/// \ingroup karma_core
/// Returns whether a scalar is finite.
inline bool isFinite(float value) {
  return std::isfinite(value);
}

/// \ingroup karma_core
/// Returns whether every vector component is finite.
inline bool isFinite(const Vec3& value) {
  return isFinite(value.x) && isFinite(value.y) && isFinite(value.z);
}

/// \ingroup karma_core
/// Returns whether every quaternion component is finite.
inline bool isFinite(const Quat& value) {
  return isFinite(value.x) && isFinite(value.y) && isFinite(value.z) &&
         isFinite(value.w);
}

/// \ingroup karma_core
/// Returns whether every color component is finite.
inline bool isFinite(const Color& value) {
  return isFinite(value.r) && isFinite(value.g) && isFinite(value.b) &&
         isFinite(value.a);
}

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
/// Returns a unit vector or zero when the input is invalid or too small.
inline Vec3 normalize(const Vec3& v) {
  constexpr float kMinLength = 1.0e-4f;
  if (!isFinite(v)) {
    return {};
  }
  const float max_component =
      std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
  if (max_component == 0.0f) {
    return {};
  }
  const Vec3 scaled{v.x / max_component,
                    v.y / max_component,
                    v.z / max_component};
  const float scaled_length = std::sqrt(lengthSquared(scaled));
  if (max_component <= kMinLength / scaled_length) {
    return {};
  }
  return scale(scaled, 1.0f / scaled_length);
}

/// \ingroup karma_core
/// Multiplies two quaternions.
inline Quat mul(const Quat& a, const Quat& b) {
  return {
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
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
/// Returns a unit quaternion or identity when the input is invalid or too small.
inline Quat normalize(const Quat& q) {
  constexpr float kMinLength = 1.0e-6f;
  if (!isFinite(q)) {
    return {};
  }
  const float max_component = std::max(
      {std::abs(q.x), std::abs(q.y), std::abs(q.z), std::abs(q.w)});
  if (max_component == 0.0f) {
    return {};
  }
  const Quat scaled{q.x / max_component,
                    q.y / max_component,
                    q.z / max_component,
                    q.w / max_component};
  const float scaled_length = std::sqrt(lengthSquared(scaled));
  if (max_component <= kMinLength / scaled_length) {
    return {};
  }
  const float inverse_scaled_length = 1.0f / scaled_length;
  return {scaled.x * inverse_scaled_length,
          scaled.y * inverse_scaled_length,
          scaled.z * inverse_scaled_length,
          scaled.w * inverse_scaled_length};
}

/// \ingroup karma_core
/// Returns a quaternion inverse, or identity when the input is invalid or too small.
inline Quat inverse(const Quat& q) {
  constexpr float kMinLength = 1.0e-6f;
  if (!isFinite(q)) {
    return {};
  }
  const float max_component = std::max(
      {std::abs(q.x), std::abs(q.y), std::abs(q.z), std::abs(q.w)});
  if (max_component == 0.0f) {
    return {};
  }
  const Quat scaled{q.x / max_component,
                    q.y / max_component,
                    q.z / max_component,
                    q.w / max_component};
  const float scaled_length_squared = lengthSquared(scaled);
  if (max_component <=
      kMinLength / std::sqrt(scaled_length_squared)) {
    return {};
  }
  const float inverse_scale =
      (1.0f / max_component) / scaled_length_squared;
  return {-scaled.x * inverse_scale,
          -scaled.y * inverse_scale,
          -scaled.z * inverse_scale,
          scaled.w * inverse_scale};
}

/// \ingroup karma_core
/// Spherically interpolates between two quaternions using the shortest path.
inline Quat slerp(const Quat& a, const Quat& b, float t) {
  const Quat start = normalize(a);
  Quat end = normalize(b);
  float cos_theta = dot(start, end);
  if (cos_theta < 0.0f) {
    end = {-end.x, -end.y, -end.z, -end.w};
    cos_theta = -cos_theta;
  }
  cos_theta = clamp(cos_theta, -1.0f, 1.0f);

  if (cos_theta > 0.9995f) {
    return normalize(Quat{
        start.x + (end.x - start.x) * t,
        start.y + (end.y - start.y) * t,
        start.z + (end.z - start.z) * t,
        start.w + (end.w - start.w) * t,
    });
  }

  const float theta = std::acos(cos_theta);
  const float sin_theta = std::sin(theta);
  if (std::abs(sin_theta) <= 1.0e-6f) {
    return start;
  }
  const float weight_a = std::sin((1.0f - t) * theta) / sin_theta;
  const float weight_b = std::sin(t * theta) / sin_theta;
  return normalize(Quat{
      start.x * weight_a + end.x * weight_b,
      start.y * weight_a + end.y * weight_b,
      start.z * weight_a + end.z * weight_b,
      start.w * weight_a + end.w * weight_b,
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
/// Rotates a vector by a quaternion. Non-unit quaternions are normalized first.
inline Vec3 rotateVec(const Quat& q, const Vec3& v) {
  const Quat unit = normalize(q);
  const Vec3 imaginary{unit.x, unit.y, unit.z};
  return add(add(scale(imaginary, 2.0f * dot(imaginary, v)),
                 scale(v, unit.w * unit.w - dot(imaginary, imaginary))),
             scale(cross(imaginary, v), 2.0f * unit.w));
}

/// \ingroup karma_core
/// Converts an engine vector to a GLM vector.
inline glm::vec3 toGlm(const Vec3& v) {
  return {v.x, v.y, v.z};
}

/// \ingroup karma_core
/// Converts a GLM vector to an engine vector.
inline Vec3 fromGlm(const glm::vec3& v) {
  return {v.x, v.y, v.z};
}

/// \ingroup karma_core
/// Converts an engine quaternion `(x, y, z, w)` to GLM's constructor order `(w, x, y, z)`.
inline glm::quat toGlm(const Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

/// \ingroup karma_core
/// Converts a GLM quaternion to the engine storage order `(x, y, z, w)`.
inline Quat fromGlm(const glm::quat& q) {
  return {q.x, q.y, q.z, q.w};
}

}  // namespace karma::math
