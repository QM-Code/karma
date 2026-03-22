#pragma once

#include <string_view>
#include <unordered_map>

#include "karma/components/collision_events.h"
#include "karma/ecs/world.h"
#include "karma/systems/system.h"

namespace karma::collision {

class CollisionEventSystem : public systems::ISystem {
 public:
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
