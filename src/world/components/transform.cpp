#include "karma/world/components/transform.h"

#include <cmath>

#include "karma/core/math/quat.h"
#include "karma/core/math/scalar.h"
#include "karma/core/math/vec3.h"

namespace karma::components {

namespace {

bool nearlyEqual(const math::Vec3& a, const math::Vec3& b) {
  constexpr float kEpsilon = 0.000001f;
  return std::abs(a.x - b.x) <= kEpsilon &&
         std::abs(a.y - b.y) <= kEpsilon &&
         std::abs(a.z - b.z) <= kEpsilon;
}

bool nearlyEqual(const math::Quat& a, const math::Quat& b) {
  constexpr float kEpsilon = 0.000001f;
  return std::abs(a.x - b.x) <= kEpsilon &&
         std::abs(a.y - b.y) <= kEpsilon &&
         std::abs(a.z - b.z) <= kEpsilon &&
         std::abs(a.w - b.w) <= kEpsilon;
}

}  // namespace

TransformComponent::TransformComponent() = default;

TransformComponent::TransformComponent(const math::Vec3& position, const math::Quat& rotation,
                                       const math::Vec3& scale)
    : local_position_(position),
      local_rotation_(rotation),
      local_scale_(scale),
      world_position_(position),
      world_rotation_(rotation),
      world_scale_(scale),
      previous_position_(position),
      previous_rotation_(rotation) {}

void TransformComponent::setLocalPosition(const math::Vec3& position) {
  local_position_ = position;
  setWorldPosition(position);
  hierarchy_initialized_ = false;
}

void TransformComponent::setLocalRotation(const math::Quat& rotation) {
  local_rotation_ = rotation;
  setWorldRotation(rotation);
  hierarchy_initialized_ = false;
}

void TransformComponent::setLocalScale(const math::Vec3& scale) {
  local_scale_ = scale;
  setWorldScale(scale);
  hierarchy_initialized_ = false;
}

void TransformComponent::setWorldPosition(const math::Vec3& position) {
  world_position_ = position;
  previous_position_ = position;
  position_dirty_ = true;
}

void TransformComponent::setWorldRotation(const math::Quat& rotation) {
  world_rotation_ = rotation;
  previous_rotation_ = rotation;
  rotation_dirty_ = true;
}

void TransformComponent::setWorldScale(const math::Vec3& scale) {
  world_scale_ = scale;
}

void TransformComponent::setPositionFromPhysics(const math::Vec3& position) {
  previous_position_ = world_position_;
  world_position_ = position;
  local_position_ = position;
  position_dirty_ = false;
  hierarchy_initialized_ = true;
}

void TransformComponent::setRotationFromPhysics(const math::Quat& rotation) {
  previous_rotation_ = world_rotation_;
  world_rotation_ = rotation;
  local_rotation_ = rotation;
  rotation_dirty_ = false;
  hierarchy_initialized_ = true;
}

void TransformComponent::setWorldFromHierarchy(const math::Vec3& position,
                                               const math::Quat& rotation,
                                               const math::Vec3& scale,
                                               bool reset_history) {
  if (reset_history || !hierarchy_initialized_) {
    world_position_ = position;
    world_rotation_ = rotation;
    world_scale_ = scale;
    previous_position_ = position;
    previous_rotation_ = rotation;
    position_dirty_ = false;
    rotation_dirty_ = false;
    hierarchy_initialized_ = true;
    return;
  }

  if (!nearlyEqual(position, world_position_)) {
    previous_position_ = world_position_;
    world_position_ = position;
  }
  if (!nearlyEqual(rotation, world_rotation_)) {
    previous_rotation_ = world_rotation_;
    world_rotation_ = rotation;
  }
  if (!nearlyEqual(scale, world_scale_)) {
    world_scale_ = scale;
  }
  position_dirty_ = false;
  rotation_dirty_ = false;
}

math::Vec3 TransformComponent::getInterpolatedPosition(float alpha) const {
  return math::lerp(previous_position_, world_position_, math::clamp01(alpha));
}

math::Quat TransformComponent::getInterpolatedRotation(float alpha) const {
  return math::slerp(previous_rotation_, world_rotation_, math::clamp01(alpha));
}

}  // namespace karma::components
