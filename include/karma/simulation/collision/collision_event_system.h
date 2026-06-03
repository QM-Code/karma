#pragma once

#include <string_view>
#include <unordered_map>

#include "karma/world/components/collision_events.h"
#include "karma/world/ecs/world.h"
#include "karma/world/systems/system.h"

namespace karma::collision {

/// \ingroup karma_simulation
/// ECS overlap/trigger event system.
///
/// The system compares collider query results frame-to-frame and writes
/// `CollisionEventsComponent` buffers for entities with
/// `CollisionListenerComponent`.
class CollisionEventSystem : public systems::ISystem {
 public:
  /// Persisted overlap used to compute enter/stay/exit transitions.
  struct TrackedContact {
    ecs::Entity other{};
    ecs::queries::ColliderShape other_shape = ecs::queries::ColliderShape::Box;
    bool other_is_trigger = false;
  };

  using ContactMap = std::unordered_map<uint64_t, TrackedContact>;

  void update(ecs::World& world, float dt) override;
  std::string_view name() const override { return "CollisionEventSystem"; }

 private:
  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  void cleanupStale(ecs::World& world);

  std::unordered_map<uint64_t, ContactMap> previous_contacts_;
};

}  // namespace karma::collision
