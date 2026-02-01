#pragma once

#include <string_view>
#include <unordered_map>

#include "karma/components/collider.h"
#include "karma/components/player_controller.h"
#include "karma/components/rigidbody.h"
#include "karma/components/transform.h"
#include "karma/ecs/world.h"
#include "karma/physics/physics_world.hpp"
#include "karma/systems/system.h"

namespace karma::physics {

class PhysicsSystem : public systems::ISystem {
 public:
  explicit PhysicsSystem(World& physics) : physics_(physics) {}

  void update(ecs::World& world, float dt) override;
 std::string_view name() const override { return "PhysicsSystem"; }

 private:
  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  void syncRigidBodies(ecs::World& world);
  void syncDynamicBodies(ecs::World& world);
  void syncPlayerController(ecs::World& world, float dt);
  void cleanupStale(ecs::World& world);

  World& physics_;
  std::unordered_map<uint64_t, RigidBody> rigid_bodies_;
  struct BoxColliderState {
    math::Vec3 center{};
    math::Vec3 half_extents{};
  };
  std::unordered_map<uint64_t, BoxColliderState> box_collider_state_;
  std::unordered_map<uint64_t, StaticBody> static_bodies_;
  ecs::Entity player_entity_{};
  math::Vec3 player_half_extents_{-1.0f, -1.0f, -1.0f};
  math::Vec3 player_center_{};
  int player_shape_kind_ = -1;
  bool has_player_ = false;
};

}  // namespace karma::physics
