#pragma once

#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"
#include "karma/core/math/types.h"

namespace karma::physics {
class PhysicsSystem;
}

namespace karma::ecs {
class World;
}

namespace karma::components {

/// \ingroup karma_components
/// Local-space transform used by the scene hierarchy.
///
/// `scene::updateWorldTransforms(...)` composes this with parent local
/// transforms and writes the final world transform to `TransformComponent`.
struct LocalTransformComponent : ecs::ComponentTag {
  LocalTransformComponent() = default;
  LocalTransformComponent(const math::Vec3& position, const math::Quat& rotation = {},
                          const math::Vec3& scale = {1.0f, 1.0f, 1.0f})
      : position(position), rotation(rotation), scale(scale) {}

  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
};

/// \ingroup karma_components
/// World-space transform consumed by render, physics, audio, and gameplay.
///
/// The component stores previous position/rotation for interpolation. Setters
/// are the game-facing path; physics integration uses private friends to update
/// transforms without treating them as user-authored changes.
class TransformComponent : public ecs::ComponentTag {
 public:
  TransformComponent();
  TransformComponent(const math::Vec3& position, const math::Quat& rotation = {},
                     const math::Vec3& scale = {1.0f, 1.0f, 1.0f});

  /// Current world-space position.
  const math::Vec3& getPosition() const { return position_; }
  /// Current world-space rotation.
  const math::Quat& getRotation() const { return rotation_; }
  /// Current world-space scale.
  const math::Vec3& getScale() const { return scale_; }
  /// Interpolated position between previous and current values.
  math::Vec3 getInterpolatedPosition(float alpha) const;
  /// Interpolated rotation between previous and current values.
  math::Quat getInterpolatedRotation(float alpha) const;

  /// Sets world-space position and records interpolation history.
  void setPosition(const math::Vec3& position);
  /// Sets world-space rotation and records interpolation history.
  void setRotation(const math::Quat& rotation);
  /// Sets world-space scale.
  void setScale(const math::Vec3& scale);
  friend class ecs::World;
  friend class physics::PhysicsSystem;

 private:
  void setPositionFromPhysics(const math::Vec3& position);
  void setRotationFromPhysics(const math::Quat& rotation);

 private:
  math::Vec3 position_{};
  math::Quat rotation_{};
  math::Vec3 scale_{1.0f, 1.0f, 1.0f};
  math::Vec3 previous_position_{};
  math::Quat previous_rotation_{};
  bool position_dirty_ = false;
  bool rotation_dirty_ = false;
};

}  // namespace karma::components
