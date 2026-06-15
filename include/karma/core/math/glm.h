#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/core/math/types.h"

namespace karma::math {

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
