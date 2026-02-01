#include "karma/components/transform.h"

#include <spdlog/spdlog.h>

namespace karma::components {

TransformComponent::TransformComponent() = default;

TransformComponent::TransformComponent(const math::Vec3& position, const math::Quat& rotation,
                                       const math::Vec3& scale)
    : position_(position), rotation_(rotation), scale_(scale) {}

void TransformComponent::setPosition(const math::Vec3& position) {
  position_ = position;
  position_dirty_ = true;
}

void TransformComponent::setRotation(const math::Quat& rotation) {
  rotation_ = rotation;
  rotation_dirty_ = true;
}

void TransformComponent::setScale(const math::Vec3& scale) {
  scale_ = scale;
}

void TransformComponent::setPositionFromPhysics(const math::Vec3& position) {
  position_ = position;
  position_dirty_ = false;
}

void TransformComponent::setRotationFromPhysics(const math::Quat& rotation) {
  rotation_ = rotation;
  rotation_dirty_ = false;
}

}  // namespace karma::components
