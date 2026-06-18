#include "karma/simulation/physics/physics_system.h"
#include "physics_system_internal.h"

namespace karma::physics {

using namespace system_internal;

void PhysicsSystem::syncConstraints(ecs::World& world) {
  auto handle_for_entity = [&](ecs::Entity entity) -> std::uintptr_t {
    const uint64_t key = entityKey(entity);
    auto body_it = rigid_bodies_.find(key);
    if (body_it != rigid_bodies_.end() && body_it->second.isValid()) {
      return body_it->second.nativeHandle();
    }
    auto static_it = static_bodies_.find(key);
    if (static_it != static_bodies_.end() && static_it->second.isValid()) {
      return static_it->second.nativeHandle();
    }
    return 0;
  };

  world.forEach<components::PhysicsConstraintComponent>(
      [&](const ecs::Entity entity) {
    const auto& component = world.get<components::PhysicsConstraintComponent>(entity);
    if (!world.isAlive(component.body_a) || !world.isAlive(component.body_b)) {
      return;
    }

    const std::uintptr_t body_a = handle_for_entity(component.body_a);
    const std::uintptr_t body_b = handle_for_entity(component.body_b);
    if (body_a == 0 || body_b == 0 || body_a == body_b) {
      return;
    }

    PhysicsConstraintDesc desc = buildConstraintDesc(component);
    const std::size_t signature = constraintSignature(desc, body_a, body_b);
    const uint64_t key = entityKey(entity);

    auto it = constraints_.find(key);
    auto signature_it = constraint_signatures_.find(key);
    const bool needs_recreate =
        it == constraints_.end() ||
        signature_it == constraint_signatures_.end() ||
        signature_it->second != signature;

    if (!needs_recreate) {
      if (it->second.isValid()) {
        it->second.setEnabled(component.enabled);
      }
      return;
    }

    if (it != constraints_.end()) {
      it->second.destroy();
    }

    Constraint constraint = physics_.createConstraint(desc, body_a, body_b);
    it = constraints_.insert_or_assign(key, std::move(constraint)).first;
    constraint_signatures_[key] = signature;
    if (it->second.isValid()) {
      it->second.setEnabled(component.enabled);
    }
  });
}

}  // namespace karma::physics
