#include "karma/components/transform.h"

#include <spdlog/spdlog.h>

namespace karma::components {

TransformComponent::TransformComponent() = default;

TransformComponent::TransformComponent(const Vec3& position, const Quat& rotation,
                                       const Vec3& scale)
    : position_(position), rotation_(rotation), scale_(scale) {}

void TransformComponent::setPosition(const Vec3& position, TransformWriteMode mode) {
  warnIfPhysics("position", mode);
  position_ = position;
}

void TransformComponent::setRotation(const Quat& rotation, TransformWriteMode mode) {
  warnIfPhysics("rotation", mode);
  rotation_ = rotation;
}

void TransformComponent::setScale(const Vec3& scale, TransformWriteMode mode) {
  warnIfPhysics("scale", mode);
  scale_ = scale;
}

void TransformComponent::warnIfPhysics(const char* action, TransformWriteMode mode) const {
  if (mode == TransformWriteMode::AllowPhysics) {
    return;
  }
  if (has_physics_ && warn_on_physics_write_) {
    spdlog::warn("Karma: Setting transform {} while a RigidbodyComponent is attached. "
                 "Use RigidbodyComponent::setPosition or mark the body kinematic.",
                 action);
  }
}

}  // namespace karma::components
