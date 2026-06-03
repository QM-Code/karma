#pragma once

#include <string_view>
#include <unordered_map>

#include "karma/world/components/collider.h"
#include "karma/world/components/contact_events.h"
#include "karma/world/components/ground_contact.h"
#include "karma/world/components/player_controller.h"
#include "karma/world/components/rigidbody.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/collider_queries.h"
#include "karma/world/ecs/world.h"
#include "karma/simulation/physics/physics_world.hpp"
#include "karma/world/systems/system.h"

namespace karma::physics {

/// \ingroup karma_physics
/// Syncs ECS physics components with the configured physics backend.
///
/// The system creates/destroys backend bodies for ECS rigid bodies, applies
/// player-controller intent, steps physics, writes transforms, and emits
/// contact/ground-state components.
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
  void syncContactEvents(ecs::World& world);
  void syncGroundContacts(ecs::World& world);
  void cleanupStale(ecs::World& world);

  struct TrackedContact {
    ecs::Entity other{};
    ecs::queries::ColliderShape other_shape = ecs::queries::ColliderShape::Box;
    math::Vec3 point{};
    math::Vec3 normal{0.0f, 1.0f, 0.0f};
  };

  using ContactMap = std::unordered_map<uint64_t, TrackedContact>;

  World& physics_;
  std::unordered_map<uint64_t, RigidBody> rigid_bodies_;
  struct BoxColliderState {
    math::Vec3 center{};
    math::Vec3 half_extents{};
  };
  std::unordered_map<uint64_t, BoxColliderState> box_collider_state_;
  std::unordered_map<uint64_t, StaticBody> static_bodies_;
  std::unordered_map<std::uintptr_t, ecs::Entity> physics_entities_by_handle_;
  std::unordered_map<uint64_t, ContactMap> previous_contacts_;
  ecs::Entity player_entity_{};
  math::Vec3 player_half_extents_{-1.0f, -1.0f, -1.0f};
  math::Vec3 player_center_{};
  std::uintptr_t player_native_handle_ = 0;
  int player_shape_kind_ = -1;
  bool has_player_ = false;
};

}  // namespace karma::physics
