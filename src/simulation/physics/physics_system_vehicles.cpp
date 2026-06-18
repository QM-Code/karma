#include "karma/simulation/physics/physics_system.h"
#include "physics_system_internal.h"

namespace karma::physics {

using namespace system_internal;

void PhysicsSystem::syncVehicles(ecs::World& world) {
  auto body_handle_for_entity = [&](ecs::Entity entity) -> std::uintptr_t {
    const uint64_t key = entityKey(entity);
    auto body_it = rigid_bodies_.find(key);
    if (body_it != rigid_bodies_.end() && body_it->second.isValid()) {
      return body_it->second.nativeHandle();
    }
    return 0;
  };

  world.forEach<components::PhysicsVehicleComponent, components::RigidbodyComponent>(
      [&](const ecs::Entity entity) {
    auto& component = world.get<components::PhysicsVehicleComponent>(entity);
    const uint64_t key = entityKey(entity);
    const std::uintptr_t body = body_handle_for_entity(entity);

    if (!component.enabled || body == 0 || component.wheels.empty()) {
      auto it = vehicles_.find(key);
      if (it != vehicles_.end()) {
        it->second.destroy();
        vehicles_.erase(it);
      }
      vehicle_signatures_.erase(key);
      return;
    }

    PhysicsVehicleDesc desc = buildVehicleDesc(component);
    const std::size_t signature = vehicleSignature(desc, body);
    auto it = vehicles_.find(key);
    auto signature_it = vehicle_signatures_.find(key);
    const bool needs_recreate =
        it == vehicles_.end() ||
        signature_it == vehicle_signatures_.end() ||
        signature_it->second != signature;

    if (needs_recreate) {
      if (it != vehicles_.end()) {
        it->second.destroy();
      }
      Vehicle vehicle = physics_.createVehicle(desc, body);
      it = vehicles_.insert_or_assign(key, std::move(vehicle)).first;
      vehicle_signatures_[key] = signature;
    }

    if (it->second.isValid()) {
      it->second.setEnabled(component.enabled);
      it->second.setInput(buildVehicleInput(component.input));
    }
  });
}

}  // namespace karma::physics
