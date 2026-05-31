#include "karma/world/components/transform.h"

#include <algorithm>

#include <glm/gtx/quaternion.hpp>

namespace karma::components {

namespace {

glm::quat toGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

math::Quat fromGlm(const glm::quat& q) {
  return {q.x, q.y, q.z, q.w};
}

float clampAlpha(float alpha) {
  return std::clamp(alpha, 0.0f, 1.0f);
}

}  // namespace

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
  const float t = clampAlpha(alpha);
  return {
      previous_position_.x + (position_.x - previous_position_.x) * t,
      previous_position_.y + (position_.y - previous_position_.y) * t,
      previous_position_.z + (position_.z - previous_position_.z) * t,
  };
}

math::Quat TransformComponent::getInterpolatedRotation(float alpha) const {
  const float t = clampAlpha(alpha);
  const glm::quat interpolated =
      glm::normalize(glm::slerp(toGlm(previous_rotation_), toGlm(rotation_), t));
  return fromGlm(interpolated);
}

}  // namespace karma::components
