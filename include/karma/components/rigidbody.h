#pragma once

#include "karma/components/transform.h"
#include "karma/ecs/component.h"

namespace karma::components {

class RigidbodyComponent : public ecs::ComponentTag {
 public:
  float mass = 1.0f;
  math::Vec3 velocity{};
  math::Vec3 angular_velocity{};
  bool is_kinematic = false;
  bool use_gravity = true;

  void setPosition(const math::Vec3& position) {
    position_ = position;
    teleport_position_ = position;
    teleport_ = true;
  }

  math::Vec3 getPosition() const { return position_; }

  bool consumeTeleport(math::Vec3& out_position) {
    if (!teleport_) {
      return false;
    }
    out_position = teleport_position_;
    teleport_ = false;
    return true;
  }

  void syncPosition(const math::Vec3& position) { position_ = position; }

 private:
  bool teleport_ = false;
  math::Vec3 teleport_position_{};
  math::Vec3 position_{};
};

}  // namespace karma::components
