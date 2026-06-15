#include "karma/world/components/transform.h"

#include "karma/core/math/quat.h"
#include "karma/core/math/scalar.h"
#include "karma/core/math/vec3.h"

namespace karma::components {

TransformComponent::TransformComponent() = default;

TransformComponent::TransformComponent(const math::Vec3& position, const math::Quat& rotation,
                                       const math::Vec3& scale)
    : position_(position),
      rotation_(rotation),
      scale_(scale),
      previous_position_(position),
      previous_rotation_(rotation) {}

void TransformComponent::setPosition(const math::Vec3& position) {
  position_ = position;
  previous_position_ = position;
  position_dirty_ = true;
}

void TransformComponent::setRotation(const math::Quat& rotation) {
  rotation_ = rotation;
  previous_rotation_ = rotation;
  rotation_dirty_ = true;
}

void TransformComponent::setScale(const math::Vec3& scale) {
  scale_ = scale;
}

void TransformComponent::setPositionFromPhysics(const math::Vec3& position) {
  previous_position_ = position_;
  position_ = position;
  position_dirty_ = false;
}

void TransformComponent::setRotationFromPhysics(const math::Quat& rotation) {
  previous_rotation_ = rotation_;
  rotation_ = rotation;
  rotation_dirty_ = false;
}

math::Vec3 TransformComponent::getInterpolatedPosition(float alpha) const {
  return math::lerp(previous_position_, position_, math::clamp01(alpha));
}

math::Quat TransformComponent::getInterpolatedRotation(float alpha) const {
  return math::slerp(previous_rotation_, rotation_, math::clamp01(alpha));
}

}  // namespace karma::components
