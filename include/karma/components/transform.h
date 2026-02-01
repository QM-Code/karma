#pragma once

#include "karma/ecs/component.h"
#include "karma/ecs/entity.h"
#include "karma/math/types.h"

namespace karma::physics {
class PhysicsSystem;
}

namespace karma::ecs {
class World;
}

namespace karma::components {

class TransformComponent : public ecs::ComponentTag {
 public:
  TransformComponent();
  TransformComponent(const math::Vec3& position, const math::Quat& rotation = {},
                     const math::Vec3& scale = {1.0f, 1.0f, 1.0f});

  const math::Vec3& getPosition() const { return position_; }
  const math::Quat& getRotation() const { return rotation_; }
  const math::Vec3& getScale() const { return scale_; }

  void setPosition(const math::Vec3& position);
  void setRotation(const math::Quat& rotation);
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
  bool position_dirty_ = false;
  bool rotation_dirty_ = false;
};

}  // namespace karma::components
