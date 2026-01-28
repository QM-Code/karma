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
  struct TeleportRequest {
    components::Vec3 position{};
    components::Quat rotation{};
  };

  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  void syncRigidBodies(ecs::World& world);
  void applyTeleports(ecs::World& world);
  void syncDynamicBodies(ecs::World& world);
  void syncPlayerController(ecs::World& world, float dt);
  void cleanupStale(ecs::World& world);

  World& physics_;
  std::unordered_map<uint64_t, RigidBody> rigid_bodies_;
  std::unordered_map<uint64_t, StaticBody> static_bodies_;
  std::unordered_map<uint64_t, TeleportRequest> teleports_;
  ecs::Entity player_entity_{};
  bool has_player_ = false;
};

}  // namespace karma::physics
