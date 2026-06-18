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

namespace karma::scene {
class Scene;
void updateWorldTransforms(ecs::World& world, const Scene& scene);
}

namespace karma::components {

/// \ingroup karma_components
/// Authored local transform plus cached world transform.
///
/// Scene hierarchy code composes local values into the cached world values.
/// Systems such as rendering, physics, audio, and particles read the world
/// accessors. Physics integration uses private friends to update world values
/// without treating them as authored local changes.
class TransformComponent : public ecs::ComponentTag {
 public:
  TransformComponent();
  TransformComponent(const math::Vec3& position, const math::Quat& rotation = {},
                     const math::Vec3& scale = {1.0f, 1.0f, 1.0f});

  /// Authored local position.
  const math::Vec3& localPosition() const { return local_position_; }
  /// Authored local rotation.
  const math::Quat& localRotation() const { return local_rotation_; }
  /// Authored local scale.
  const math::Vec3& localScale() const { return local_scale_; }
  /// Cached world position.
  const math::Vec3& worldPosition() const { return world_position_; }
  /// Cached world rotation.
  const math::Quat& worldRotation() const { return world_rotation_; }
  /// Cached world scale.
  const math::Vec3& worldScale() const { return world_scale_; }

  /// Current world-space position.
  const math::Vec3& getPosition() const { return worldPosition(); }
  /// Current world-space rotation.
  const math::Quat& getRotation() const { return worldRotation(); }
  /// Current world-space scale.
  const math::Vec3& getScale() const { return worldScale(); }
  /// Interpolated position between previous and current values.
  math::Vec3 getInterpolatedPosition(float alpha) const;
  /// Interpolated rotation between previous and current values.
  math::Quat getInterpolatedRotation(float alpha) const;

  /// Sets authored local position and mirrors it to world for unparented use.
  void setLocalPosition(const math::Vec3& position);
  /// Sets authored local rotation and mirrors it to world for unparented use.
  void setLocalRotation(const math::Quat& rotation);
  /// Sets authored local scale and mirrors it to world for unparented use.
  void setLocalScale(const math::Vec3& scale);

  /// Sets cached world position and records interpolation history.
  void setWorldPosition(const math::Vec3& position);
  /// Sets cached world rotation and records interpolation history.
  void setWorldRotation(const math::Quat& rotation);
  /// Sets cached world scale.
  void setWorldScale(const math::Vec3& scale);

  /// Convenience setter for authored local position.
  void setPosition(const math::Vec3& position) { setLocalPosition(position); }
  /// Convenience setter for authored local rotation.
  void setRotation(const math::Quat& rotation) { setLocalRotation(rotation); }
  /// Convenience setter for authored local scale.
  void setScale(const math::Vec3& scale) { setLocalScale(scale); }

  friend class ecs::World;
  friend class physics::PhysicsSystem;
  friend void scene::updateWorldTransforms(ecs::World& world, const scene::Scene& scene);

 private:
  void setPositionFromPhysics(const math::Vec3& position);
  void setRotationFromPhysics(const math::Quat& rotation);
  void setWorldFromHierarchy(const math::Vec3& position,
                             const math::Quat& rotation,
                             const math::Vec3& scale,
                             bool reset_history);

 private:
  math::Vec3 local_position_{};
  math::Quat local_rotation_{};
  math::Vec3 local_scale_{1.0f, 1.0f, 1.0f};
  math::Vec3 world_position_{};
  math::Quat world_rotation_{};
  math::Vec3 world_scale_{1.0f, 1.0f, 1.0f};
  math::Vec3 previous_position_{};
  math::Quat previous_rotation_{};
  bool position_dirty_ = false;
  bool rotation_dirty_ = false;
  bool hierarchy_initialized_ = false;
};

}  // namespace karma::components
