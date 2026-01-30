#pragma once

#include "karma/ecs/component.h"
#include "karma/math/types.h"

namespace karma::components {

enum class TransformWriteMode {
  WarnOnPhysics,
  AllowPhysics
};

class TransformComponent : public ecs::ComponentTag {
 public:
  TransformComponent();
  TransformComponent(const math::Vec3& position, const math::Quat& rotation = {},
                     const math::Vec3& scale = {1.0f, 1.0f, 1.0f});

  const math::Vec3& position() const { return position_; }
  const math::Quat& rotation() const { return rotation_; }
  const math::Vec3& scale() const { return scale_; }

  void setPosition(const math::Vec3& position,
                   TransformWriteMode mode = TransformWriteMode::WarnOnPhysics);
  void setRotation(const math::Quat& rotation,
                   TransformWriteMode mode = TransformWriteMode::WarnOnPhysics);
  void setScale(const math::Vec3& scale,
                TransformWriteMode mode = TransformWriteMode::WarnOnPhysics);

  void setHasPhysics(bool has_physics) { has_physics_ = has_physics; }
  void setPhysicsWriteWarning(bool enabled) { warn_on_physics_write_ = enabled; }

 private:
  void warnIfPhysics(const char* action, TransformWriteMode mode) const;

  math::Vec3 position_{};
  math::Quat rotation_{};
  math::Vec3 scale_{1.0f, 1.0f, 1.0f};
  bool has_physics_ = false;
  bool warn_on_physics_write_ = true;
};

}  // namespace karma::components
