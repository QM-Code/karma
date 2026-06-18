#include "karma/simulation/physics/physics_system.h"
#include "physics_system_internal.h"

#include <vector>

namespace karma::physics {

using namespace system_internal;

void PhysicsSystem::syncContactEvents(ecs::World& world) {
  auto handle_for_entity = [&](ecs::Entity entity) -> std::uintptr_t {
    const uint64_t key = entityKey(entity);
    auto character_it = character_controllers_.find(key);
    if (character_it != character_controllers_.end()) {
      return character_it->second.native_handle;
    }
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

  auto to_contact_event = [](const TrackedContact& tracked) {
    return components::ContactEvent{
        .other = tracked.other,
        .other_shape = tracked.other_shape,
        .point = tracked.point,
        .normal = tracked.normal,
    };
  };

  std::vector<PhysicsContact> contacts;
  physics_.collectContacts(contacts);
  for (const auto& [key, state] : character_controllers_) {
    (void)key;
    state.controller.collectContacts(contacts);
  }

  for (const ecs::Entity entity : world.view<components::ContactListenerComponent>()) {
    auto& listener = world.get<components::ContactListenerComponent>(entity);
    if (!world.has<components::ContactEventsComponent>(entity)) {
      world.add(entity, components::ContactEventsComponent{});
    }
    auto& events = world.get<components::ContactEventsComponent>(entity);
    events.clearTransient();

    const uint64_t key = entityKey(entity);
    if (!listener.enabled || !collisionEnabled(world, entity)) {
      previous_contacts_.erase(key);
      continue;
    }

    const std::uintptr_t self_handle = handle_for_entity(entity);
    if (self_handle == 0) {
      previous_contacts_.erase(key);
      continue;
    }

    ContactMap current_contacts;
    auto previous_it = previous_contacts_.find(key);
    const ContactMap* previous = previous_it != previous_contacts_.end() ? &previous_it->second : nullptr;

    for (const PhysicsContact& contact : contacts) {
      const bool self_is_a = contact.handle_a == self_handle;
      const bool self_is_b = contact.handle_b == self_handle;
      if (!self_is_a && !self_is_b) {
        continue;
      }

      const std::uintptr_t other_handle = self_is_a ? contact.handle_b : contact.handle_a;
      auto other_it = physics_entities_by_handle_.find(other_handle);
      if (other_it == physics_entities_by_handle_.end()) {
        continue;
      }

      const ecs::Entity other = other_it->second;
      if (!world.isAlive(other) || other == entity) {
        continue;
      }
      if (!collisionEnabled(world, other) ||
          !matchesCollisionLayerMask(world, other, listener.collision_layer_mask)) {
        continue;
      }
      if (colliderIsTrigger(world, entity) || colliderIsTrigger(world, other)) {
        continue;
      }

      const uint64_t other_key = entityKey(other);
      TrackedContact tracked{
          .other = other,
          .other_shape = colliderShape(world, other),
          .point = self_is_a ? math::fromGlm(contact.point_a) : math::fromGlm(contact.point_b),
          .normal = self_is_a ? negateVec3(math::fromGlm(contact.normal_a_to_b))
                              : math::fromGlm(contact.normal_a_to_b),
      };

      current_contacts[other_key] = tracked;
      events.active.push_back(to_contact_event(tracked));

      if (previous == nullptr || previous->find(other_key) == previous->end()) {
        events.entered.push_back(to_contact_event(tracked));
      } else if (listener.emit_stay) {
        events.stayed.push_back(to_contact_event(tracked));
      }
    }

    if (previous != nullptr) {
      for (const auto& [other_key, tracked] : *previous) {
        if (current_contacts.find(other_key) == current_contacts.end()) {
          events.exited.push_back(to_contact_event(tracked));
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
void PhysicsSystem::syncGroundContacts(ecs::World& world) {
  constexpr float kGroundNormalThreshold = 0.7f;
  constexpr float kGroundProbeInset = 0.02f;
  constexpr float kGroundProbeDistance = 0.2f;

  auto apply_ground_state = [&](ecs::Entity entity,
                                const PhysicsGroundContact* hit,
                                ecs::Entity support_entity) {
    auto& contact = world.get<components::GroundContactComponent>(entity);
    const bool was_grounded = contact.grounded;
    contact.clearTransient();
    contact.grounded = hit != nullptr && hit->grounded;
    contact.entered = contact.grounded && !was_grounded;
    contact.exited = !contact.grounded && was_grounded;
    contact.has_support = hit != nullptr && hit->grounded;
    contact.support_entity = support_entity;
    contact.point = hit != nullptr ? math::fromGlm(hit->point) : math::Vec3{};
    contact.normal = hit != nullptr ? math::fromGlm(hit->normal) : math::Vec3{0.0f, 1.0f, 0.0f};
  };

  world.forEach<components::GroundContactComponent>([&](const ecs::Entity entity) {
    if (!collisionEnabled(world, entity)) {
      apply_ground_state(entity, nullptr, {});
      return;
    }

    if (world.has<components::CharacterControllerComponent>(entity)) {
      PhysicsGroundContact hit{};
      auto controller_it = character_controllers_.find(entityKey(entity));
      const bool grounded = controller_it != character_controllers_.end() &&
                            controller_it->second.controller.getGroundContact(hit);
      ecs::Entity support_entity{};
      if (grounded) {
        auto support_it = physics_entities_by_handle_.find(hit.support_handle);
        if (support_it != physics_entities_by_handle_.end()) {
          support_entity = support_it->second;
        }
      }
      apply_ground_state(entity, grounded ? &hit : nullptr, support_entity);
      return;
    }

    if (!world.has<components::RigidbodyComponent>(entity) ||
        !hasPhysicsCollider(world, entity) ||
        !world.has<components::TransformComponent>(entity)) {
      apply_ground_state(entity, nullptr, {});
      return;
    }

    const uint64_t key = entityKey(entity);
    auto body_it = rigid_bodies_.find(key);
    if (body_it == rigid_bodies_.end() || !body_it->second.isValid()) {
      apply_ground_state(entity, nullptr, {});
      return;
    }

    const auto& transform = world.get<components::TransformComponent>(entity);
    const glm::vec3 full_dimensions = groundProbeDimensions(world, entity, transform);
    if (!body_it->second.isGrounded(full_dimensions)) {
      apply_ground_state(entity, nullptr, {});
      return;
    }

    PhysicsGroundContact hit{};
    bool resolved_support = false;
    const std::uintptr_t self_handle = body_it->second.nativeHandle();
    const glm::vec3 body_position = body_it->second.getPosition();
    const float half_height = full_dimensions.y * 0.5f;

    const glm::vec3 probe_from = body_position + glm::vec3(0.0f, half_height - kGroundProbeInset, 0.0f);
    const glm::vec3 probe_to = body_position - glm::vec3(0.0f, half_height + kGroundProbeDistance, 0.0f);

    if (physics_.raycastDetailed(probe_from, probe_to, hit)) {
      if (hit.support_handle == self_handle) {
        const glm::vec3 retry_from = hit.point - glm::vec3(0.0f, kGroundProbeInset * 2.0f, 0.0f);
        const glm::vec3 retry_to = retry_from - glm::vec3(0.0f, kGroundProbeDistance, 0.0f);
        PhysicsGroundContact retry_hit{};
        if (physics_.raycastDetailed(retry_from, retry_to, retry_hit)) {
          hit = retry_hit;
        }
      }

      resolved_support = hit.support_handle != 0 &&
                         hit.support_handle != self_handle &&
                         hit.normal.y > kGroundNormalThreshold;
    }

    ecs::Entity support_entity{};
    if (resolved_support) {
      auto support_it = physics_entities_by_handle_.find(hit.support_handle);
      if (support_it != physics_entities_by_handle_.end() && support_it->second != entity) {
        support_entity = support_it->second;
      } else {
        resolved_support = false;
      }
    }

    if (resolved_support) {
      apply_ground_state(entity, &hit, support_entity);
      return;
    }

    PhysicsGroundContact grounded_hit{};
    grounded_hit.grounded = true;
    apply_ground_state(entity, &grounded_hit, {});
  });
}

}  // namespace karma::physics
