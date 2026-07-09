#include "karma/physics.h"

#include "karma/components.h"

namespace karma::physics {

namespace {

bool hasQuerySourceCollider(const world::World& world, world::Entity entity) {
  const auto* collider = world.tryGet<components::ColliderComponent>(entity);
  if (collider == nullptr) {
    return false;
  }
  const components::ColliderShapeType type =
      components::colliderShapeType(collider->shape);
  return type == components::ColliderShapeType::Box ||
         type == components::ColliderShapeType::Sphere ||
         type == components::ColliderShapeType::Capsule;
}

bool colliderIsTrigger(const world::World& world, world::Entity entity) {
  const auto* collider = world.tryGet<components::ColliderComponent>(entity);
  return collider != nullptr && collider->is_trigger;
}

bool includeContact(components::CollisionListenMode mode, bool other_is_trigger) {
  switch (mode) {
    case components::CollisionListenMode::All:
      return true;
    case components::CollisionListenMode::TriggersOnly:
      return other_is_trigger;
    case components::CollisionListenMode::SolidsOnly:
      return !other_is_trigger;
  }
  return true;
}

components::CollisionContact toCollisionContact(
    const CollisionEventSystem::TrackedContact& tracked) {
  return {
      .other = tracked.other,
      .other_shape = tracked.other_shape,
      .other_is_trigger = tracked.other_is_trigger,
  };
}

}  // namespace

void CollisionEventSystem::update(world::World& world, float /*dt*/) {
  cleanupStale(world);

  for (const world::Entity entity : world.view<components::CollisionListenerComponent>()) {
    auto& listener = world.get<components::CollisionListenerComponent>(entity);
    if (!world.has<components::CollisionEventsComponent>(entity)) {
      world.add(entity, components::CollisionEventsComponent{});
    }
    auto& events = world.get<components::CollisionEventsComponent>(entity);
    events.clearTransient();

    const uint64_t key = entityKey(entity);
    auto previous_it = previous_contacts_.find(key);
    if (!listener.enabled || !hasQuerySourceCollider(world, entity)) {
      if (previous_it != previous_contacts_.end()) {
        events.exited.reserve(previous_it->second.size());
        for (const auto& [other_key, tracked] : previous_it->second) {
          (void)other_key;
          events.exited.push_back(toCollisionContact(tracked));
        }
        previous_contacts_.erase(previous_it);
      }
      continue;
    }

    const auto overlaps = world::queries::findOverlappingColliders(
        world,
        entity,
        world::queries::OverlapFilter{
            .only_triggers = false,
            .collision_layer_mask = listener.collision_layer_mask,
            .skip_self = true,
        });

    ContactMap current_contacts;
    current_contacts.reserve(overlaps.size());
    const ContactMap* previous_contacts =
        previous_it != previous_contacts_.end() ? &previous_it->second : nullptr;

    for (const auto& overlap : overlaps) {
      const bool other_is_trigger = colliderIsTrigger(world, overlap.entity);
      if (!includeContact(listener.mode, other_is_trigger)) {
        continue;
      }

      const uint64_t other_key = entityKey(overlap.entity);
      TrackedContact tracked{
          .other = overlap.entity,
          .other_shape = overlap.shape,
          .other_is_trigger = other_is_trigger,
      };
      current_contacts[other_key] = tracked;
      events.active.push_back(toCollisionContact(tracked));

      if (previous_contacts == nullptr ||
          previous_contacts->find(other_key) == previous_contacts->end()) {
        events.entered.push_back(toCollisionContact(tracked));
      } else if (listener.emit_stay) {
        events.stayed.push_back(toCollisionContact(tracked));
      }
    }

    if (previous_contacts != nullptr) {
      for (const auto& [other_key, tracked] : *previous_contacts) {
        if (current_contacts.find(other_key) == current_contacts.end()) {
          events.exited.push_back(toCollisionContact(tracked));
        }
      }
    }

    if (current_contacts.empty()) {
      previous_contacts_.erase(key);
    } else {
      previous_contacts_[key] = std::move(current_contacts);
    }
  }
}

void CollisionEventSystem::cleanupStale(world::World& world) {
  for (auto it = previous_contacts_.begin(); it != previous_contacts_.end();) {
    const world::Entity listener = {
        static_cast<uint32_t>(it->first >> 32),
        static_cast<uint32_t>(it->first & 0xFFFFFFFFu),
    };
    if (!world.isAlive(listener) || !world.has<components::CollisionListenerComponent>(listener)) {
      it = previous_contacts_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace karma::physics
