#pragma once

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

}  // namespace karma::math
