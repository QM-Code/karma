#include "karma/physics.h"
#include "physics_system_internal.h"

namespace karma::physics {

using namespace system_internal;

void PhysicsSystem::cleanupStale(world::World& world) {
  for (auto it = vehicles_.begin(); it != vehicles_.end();) {
    const world::Entity entity = entityFromKey(it->first);
    const bool remove = !world.isAlive(entity) ||
                        !world.has<components::PhysicsVehicleComponent>(entity) ||
                        !world.has<components::RigidbodyComponent>(entity) ||
                        !hasPhysicsCollider(world, entity);
    if (remove) {
      it->second.destroy();
      vehicle_signatures_.erase(it->first);
      it = vehicles_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = constraints_.begin(); it != constraints_.end();) {
    const world::Entity entity = entityFromKey(it->first);
    bool remove = !world.isAlive(entity) ||
                  !world.has<components::PhysicsConstraintComponent>(entity);
    if (!remove) {
      const auto& component = world.get<components::PhysicsConstraintComponent>(entity);
      remove = !world.isAlive(component.body_a) || !world.isAlive(component.body_b) ||
               !hasPhysicsCollider(world, component.body_a) ||
               !hasPhysicsCollider(world, component.body_b);
    }
    if (remove) {
      it->second.destroy();
      constraint_signatures_.erase(it->first);
      it = constraints_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = soft_bodies_.begin(); it != soft_bodies_.end();) {
    const world::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) || !world.has<components::PhysicsSoftBodyComponent>(entity)) {
      physics_entities_by_handle_.erase(it->second.nativeHandle());
      it->second.destroy();
      soft_body_signatures_.erase(it->first);
      it = soft_bodies_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = rigid_bodies_.begin(); it != rigid_bodies_.end();) {
    world::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) ||
        !world.has<components::RigidbodyComponent>(entity) ||
        !hasPhysicsCollider(world, entity)) {
      physics_entities_by_handle_.erase(it->second.nativeHandle());
      it->second.destroy();
      it = rigid_bodies_.erase(it);
      body_state_.erase(entityKey(entity));
    } else {
      ++it;
    }
  }

  for (auto it = static_bodies_.begin(); it != static_bodies_.end();) {
    world::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) ||
        world.has<components::RigidbodyComponent>(entity) ||
        world.has<components::CharacterControllerComponent>(entity) ||
        !hasPhysicsCollider(world, entity)) {
      physics_entities_by_handle_.erase(it->second.nativeHandle());
      it->second.destroy();
      it = static_bodies_.erase(it);
      body_state_.erase(entityKey(entity));
    } else {
      ++it;
    }
  }

  for (auto it = character_controllers_.begin(); it != character_controllers_.end();) {
    const world::Entity entity = entityFromKey(it->first);
    const bool remove = !world.isAlive(entity) ||
                        !world.has<components::CharacterControllerComponent>(entity) ||
                        !world.has<components::TransformComponent>(entity) ||
                        !hasPhysicsCollider(world, entity);
    if (remove) {
      if (it->second.native_handle != 0) {
        physics_entities_by_handle_.erase(it->second.native_handle);
        it->second.native_handle = 0;
      }
      it->second.controller.destroy();
      it = character_controllers_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = previous_contacts_.begin(); it != previous_contacts_.end();) {
    const world::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) || !world.has<components::ContactListenerComponent>(entity)) {
      it = previous_contacts_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace karma::physics
