#pragma once

#include "karma/ecs/component.h"

namespace karma::components {

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct Quat {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
};

enum class TransformWriteMode {
  WarnOnPhysics,
  AllowPhysics
};

class TransformComponent : public ecs::ComponentTag {
 public:
  TransformComponent();
  TransformComponent(const Vec3& position, const Quat& rotation = {},
                     const Vec3& scale = {1.0f, 1.0f, 1.0f});

  const Vec3& position() const { return position_; }
  const Quat& rotation() const { return rotation_; }
  const Vec3& scale() const { return scale_; }

  void setPosition(const Vec3& position,
                   TransformWriteMode mode = TransformWriteMode::WarnOnPhysics);
  void setRotation(const Quat& rotation,
                   TransformWriteMode mode = TransformWriteMode::WarnOnPhysics);
  void setScale(const Vec3& scale,
                TransformWriteMode mode = TransformWriteMode::WarnOnPhysics);

  void setHasPhysics(bool has_physics) { has_physics_ = has_physics; }
  void setPhysicsWriteWarning(bool enabled) { warn_on_physics_write_ = enabled; }

 private:
  void warnIfPhysics(const char* action, TransformWriteMode mode) const;

  Vec3 position_{};
  Quat rotation_{};
  Vec3 scale_{1.0f, 1.0f, 1.0f};
  bool has_physics_ = false;
  bool warn_on_physics_write_ = true;
};

}  // namespace karma::components
