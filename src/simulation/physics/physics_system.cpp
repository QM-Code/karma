#include "karma/simulation/physics/physics_system.h"

#include <cmath>

#include "karma/world/components/mesh.h"
#include "karma/world/components/visibility.h"

namespace karma::physics {

namespace {

math::Vec3 toVec3(const glm::vec3& v) {
  return {v.x, v.y, v.z};
}

glm::vec3 toGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

glm::quat toGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

math::Vec3 addVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

math::Vec3 subVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

bool nearlyEqualVec3(const math::Vec3& a, const math::Vec3& b, float eps = 1e-4f) {
  return std::abs(a.x - b.x) <= eps &&
         std::abs(a.y - b.y) <= eps &&
         std::abs(a.z - b.z) <= eps;
}

math::Vec3 negateVec3(const math::Vec3& v) {
  return {-v.x, -v.y, -v.z};
}

int colliderShapeKind(const ecs::World& world, ecs::Entity entity) {
  if (world.has<components::BoxColliderComponent>(entity)) {
    return 0;
  }
  if (world.has<components::SphereColliderComponent>(entity)) {
    return 1;
  }
  if (world.has<components::CapsuleColliderComponent>(entity)) {
    return 2;
  }
  return -1;
}

ecs::queries::ColliderShape colliderShape(const ecs::World& world, ecs::Entity entity) {
  if (world.has<components::BoxColliderComponent>(entity)) {
    return ecs::queries::ColliderShape::Box;
  }
  if (world.has<components::SphereColliderComponent>(entity)) {
    return ecs::queries::ColliderShape::Sphere;
  }
  if (world.has<components::CapsuleColliderComponent>(entity)) {
    return ecs::queries::ColliderShape::Capsule;
  }
  return ecs::queries::ColliderShape::Mesh;
}

bool colliderIsTrigger(const ecs::World& world, ecs::Entity entity) {
  if (world.has<components::BoxColliderComponent>(entity)) {
    return world.get<components::BoxColliderComponent>(entity).is_trigger;
  }
  if (world.has<components::SphereColliderComponent>(entity)) {
    return world.get<components::SphereColliderComponent>(entity).is_trigger;
  }
  if (world.has<components::CapsuleColliderComponent>(entity)) {
    return world.get<components::CapsuleColliderComponent>(entity).is_trigger;
  }
  if (world.has<components::MeshColliderComponent>(entity)) {
    return world.get<components::MeshColliderComponent>(entity).is_trigger;
  }
  return false;
}

ecs::Entity entityFromKey(uint64_t key) {
  return {static_cast<uint32_t>(key >> 32), static_cast<uint32_t>(key & 0xFFFFFFFFu)};
}

bool collisionEnabled(ecs::World& world, ecs::Entity entity) {
  if (!world.has<components::VisibilityComponent>(entity)) {
    return true;
  }
  return world.get<components::VisibilityComponent>(entity).collision_layer_mask != 0;
}

bool matchesCollisionLayerMask(const ecs::World& world, ecs::Entity entity, uint32_t mask) {
  if (!world.has<components::VisibilityComponent>(entity)) {
    return true;
  }
  return (world.get<components::VisibilityComponent>(entity).collision_layer_mask & mask) != 0u;
}

}

void PhysicsSystem::update(ecs::World& world, float dt) {
  syncRigidBodies(world);
  syncPlayerController(world, dt);
  physics_.update(dt);
  syncDynamicBodies(world);
  syncContactEvents(world);
  syncGroundContacts(world);
  cleanupStale(world);
}

void PhysicsSystem::syncRigidBodies(ecs::World& world) {
  world.forEach<components::RigidbodyComponent, components::TransformComponent, components::BoxColliderComponent>(
      [&](const ecs::Entity entity) {
    if (!collisionEnabled(world, entity)) {
      return;
    }
    const auto& collider = world.get<components::BoxColliderComponent>(entity);
    const math::Vec3 collider_center = collider.center;
    auto& transform = world.get<components::TransformComponent>(entity);
    const math::Vec3 raw_half_extents = collider.half_extents;
    const math::Vec3 scale = transform.getScale();
    const math::Vec3 collider_half_extents{
        std::abs(raw_half_extents.x * scale.x),
        std::abs(raw_half_extents.y * scale.y),
        std::abs(raw_half_extents.z * scale.z)};

    const uint64_t key = entityKey(entity);
    auto it = rigid_bodies_.find(key);
    auto state_it = box_collider_state_.find(key);
    const bool has_state = state_it != box_collider_state_.end();
    const bool center_changed = !has_state || !nearlyEqualVec3(state_it->second.center, collider_center);
    const bool half_extents_changed = !has_state || !nearlyEqualVec3(state_it->second.half_extents, collider_half_extents);

    auto& body = world.get<components::RigidbodyComponent>(entity);

    if (it == rigid_bodies_.end() || half_extents_changed) {
      glm::quat current_rot = toGlm(transform.getRotation());
      glm::vec3 current_vel = toGlm(body.velocity);
      glm::vec3 current_ang_vel = toGlm(body.angular_velocity);
      if (it != rigid_bodies_.end() && it->second.isValid()) {
        physics_entities_by_handle_.erase(it->second.nativeHandle());
        current_rot = it->second.getRotation();
        current_vel = it->second.getVelocity();
        current_ang_vel = it->second.getAngularVelocity();
        it->second.destroy();
      }
      PhysicsMaterial material;
      RigidBody rigid = physics_.createBoxBody(
          toGlm(collider_half_extents),
          body.mass,
          toGlm(addVec3(transform.getPosition(), collider_center)),
          material);
      rigid.setRotation(current_rot);
      rigid.setVelocity(current_vel);
      rigid.setAngularVelocity(current_ang_vel);
      rigid.setKinematic(body.is_kinematic);
      rigid.setUseGravity(body.use_gravity);
      rigid.setTrigger(body.is_trigger || collider.is_trigger);
      it = rigid_bodies_.insert_or_assign(key, std::move(rigid)).first;
      if (it->second.isValid()) {
        physics_entities_by_handle_[it->second.nativeHandle()] = entity;
      }
      box_collider_state_[key] = {collider_center, collider_half_extents};
    } else if (center_changed && it->second.isValid()) {
      it->second.setPosition(toGlm(addVec3(transform.getPosition(), collider_center)));
      state_it->second.center = collider_center;
    }

    const bool position_dirty = transform.position_dirty_ || center_changed;
    const bool rotation_dirty = transform.rotation_dirty_;
    if (position_dirty || rotation_dirty) {
      if (it->second.isValid()) {
        if (position_dirty) {
          it->second.setPosition(toGlm(addVec3(transform.getPosition(), collider_center)));
        }
        if (rotation_dirty) {
          it->second.setRotation(toGlm(transform.getRotation()));
        }
      }
      transform.position_dirty_ = false;
      transform.rotation_dirty_ = false;
    }

    if (it->second.isValid()) {
      const bool trigger = body.is_trigger || collider.is_trigger;
      it->second.setKinematic(body.is_kinematic);
      it->second.setUseGravity(body.use_gravity);
      it->second.setTrigger(trigger);
      if (body.is_kinematic) {
        it->second.setVelocity(toGlm(body.velocity));
        it->second.setAngularVelocity(toGlm(body.angular_velocity));
      }
    }
  });

  world.forEach<components::MeshColliderComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    if (world.has<components::RigidbodyComponent>(entity)) {
      return;
    }
    if (!collisionEnabled(world, entity)) {
      return;
    }
    const uint64_t key = entityKey(entity);
    if (static_bodies_.find(key) != static_bodies_.end()) {
      return;
    }
    if (!world.has<components::MeshComponent>(entity)) {
      return;
    }
    const auto& mesh = world.get<components::MeshComponent>(entity);
    StaticBody body = physics_.createStaticMesh(mesh.mesh_key);
    auto [body_it, inserted] = static_bodies_.emplace(key, std::move(body));
    (void)inserted;
    if (body_it->second.isValid()) {
      physics_entities_by_handle_[body_it->second.nativeHandle()] = entity;
    }
  });
}

void PhysicsSystem::syncDynamicBodies(ecs::World& world) {
  world.forEach<components::RigidbodyComponent, components::TransformComponent, components::BoxColliderComponent>(
      [&](const ecs::Entity entity) {
    if (!collisionEnabled(world, entity)) {
      return;
    }
    auto& body = world.get<components::RigidbodyComponent>(entity);
    if (body.is_kinematic) {
      return;
    }
    const uint64_t key = entityKey(entity);
    auto it = rigid_bodies_.find(key);
    if (it == rigid_bodies_.end()) {
      return;
    }
    if (!it->second.isValid()) {
      return;
    }
    auto& transform = world.get<components::TransformComponent>(entity);
    const auto& collider = world.get<components::BoxColliderComponent>(entity);
    const math::Vec3 collider_center = collider.center;
    const math::Vec3 body_pos = toVec3(it->second.getPosition());
    transform.setPositionFromPhysics(subVec3(body_pos, collider_center));
    transform.setRotationFromPhysics({it->second.getRotation().x, it->second.getRotation().y,
                                      it->second.getRotation().z, it->second.getRotation().w});
    body.velocity = toVec3(it->second.getVelocity());
    body.angular_velocity = toVec3(it->second.getAngularVelocity());
  });
}

void PhysicsSystem::syncContactEvents(ecs::World& world) {
  auto handle_for_entity = [&](ecs::Entity entity) -> std::uintptr_t {
    const uint64_t key = entityKey(entity);
    if (has_player_ && entity == player_entity_ && physics_.playerController()) {
      return player_native_handle_;
    }
    auto body_it = rigid_bodies_.find(key);
    if (body_it != rigid_bodies_.end() && body_it->second.isValid()) {
      return body_it->second.nativeHandle();
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
  if (has_player_ && physics_.playerController()) {
    physics_.playerController()->collectContacts(contacts);
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
          .point = self_is_a ? toVec3(contact.point_a) : toVec3(contact.point_b),
          .normal = self_is_a ? negateVec3(toVec3(contact.normal_a_to_b))
                              : toVec3(contact.normal_a_to_b),
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

void PhysicsSystem::syncPlayerController(ecs::World& world, float dt) {
  (void)dt;
  if (!has_player_) {
    world.forEach<components::PlayerControllerComponent, components::TransformComponent>(
        [&](const ecs::Entity entity) {
      if (!collisionEnabled(world, entity)) {
        return true;
      }
      glm::vec3 half_extents{};
      math::Vec3 center{};
      const int shape_kind = colliderShapeKind(world, entity);
      if (world.has<components::BoxColliderComponent>(entity)) {
        const auto& collider = world.get<components::BoxColliderComponent>(entity);
        half_extents = toGlm(collider.half_extents);
        center = collider.center;
      } else if (world.has<components::SphereColliderComponent>(entity)) {
        const auto& collider = world.get<components::SphereColliderComponent>(entity);
        half_extents = glm::vec3(collider.radius);
        center = collider.center;
      } else if (world.has<components::CapsuleColliderComponent>(entity)) {
        const auto& collider = world.get<components::CapsuleColliderComponent>(entity);
        half_extents = glm::vec3(collider.radius, collider.height * 0.5f, collider.radius);
        center = collider.center;
      } else {
        return true;
      }
      player_half_extents_ = {half_extents.x, half_extents.y, half_extents.z};
      player_center_ = center;
      player_shape_kind_ = shape_kind;
      auto& controller = physics_.createPlayer(half_extents * 2.0f);
      player_entity_ = entity;
      has_player_ = true;
      player_native_handle_ = controller.nativeHandle();
      if (player_native_handle_ != 0) {
        physics_entities_by_handle_[player_native_handle_] = entity;
      }
      auto& transform = world.get<components::TransformComponent>(entity);
      controller.setCenter(toGlm(center));
      controller.setPosition(toGlm(transform.getPosition()));
      return false;
    });
  }

  if (!has_player_) {
    return;
  }

  if (!physics_.playerController()) {
    return;
  }
  auto* controller = physics_.playerController();
  if (!controller) {
    return;
  }
  auto& transform = world.get<components::TransformComponent>(player_entity_);
  auto& input = world.get<components::PlayerControllerComponent>(player_entity_);

  glm::vec3 half_extents{};
  math::Vec3 center{};
  bool has_collider = true;
  const int shape_kind = colliderShapeKind(world, player_entity_);
  if (world.has<components::BoxColliderComponent>(player_entity_)) {
    const auto& collider = world.get<components::BoxColliderComponent>(player_entity_);
    half_extents = toGlm(collider.half_extents);
    center = collider.center;
  } else if (world.has<components::SphereColliderComponent>(player_entity_)) {
    const auto& collider = world.get<components::SphereColliderComponent>(player_entity_);
    half_extents = glm::vec3(collider.radius);
    center = collider.center;
  } else if (world.has<components::CapsuleColliderComponent>(player_entity_)) {
    const auto& collider = world.get<components::CapsuleColliderComponent>(player_entity_);
    half_extents = glm::vec3(collider.radius, collider.height * 0.5f, collider.radius);
    center = collider.center;
  } else {
    has_collider = false;
  }

  if (!has_collider) {
    return;
  }

  const math::Vec3 current_half_extents{half_extents.x, half_extents.y, half_extents.z};
  const bool size_changed = !nearlyEqualVec3(current_half_extents, player_half_extents_);
  const bool center_changed = !nearlyEqualVec3(center, player_center_);
  const bool shape_changed = shape_kind != player_shape_kind_;
  if (size_changed || shape_changed) {
    if (player_native_handle_ != 0) {
      physics_entities_by_handle_.erase(player_native_handle_);
    }
    const glm::quat current_rot = controller->getRotation();
    const glm::vec3 current_vel = controller->getVelocity();
    const glm::vec3 current_ang_vel = controller->getAngularVelocity();
    controller->destroy();
    auto& new_controller = physics_.createPlayer(half_extents * 2.0f);
    controller = &new_controller;
    controller->setCenter(toGlm(center));
    controller->setPosition(toGlm(transform.getPosition()));
    controller->setRotation(current_rot);
    controller->setVelocity(current_vel);
    controller->setAngularVelocity(current_ang_vel);
    player_native_handle_ = controller->nativeHandle();
    if (player_native_handle_ != 0) {
      physics_entities_by_handle_[player_native_handle_] = player_entity_;
    }
    player_half_extents_ = current_half_extents;
    player_center_ = center;
    player_shape_kind_ = shape_kind;
  }

  if (center_changed && !size_changed && !shape_changed) {
    controller->setCenter(toGlm(center));
    controller->setPosition(toGlm(transform.getPosition()));
    player_center_ = center;
  }

  const math::Vec3 desired = input.desiredVelocity();
  const math::Vec3 impulse = input.addVelocity();
  glm::vec3 velocity = toGlm(desired) + toGlm(impulse);
  controller->setVelocity(velocity);
  input.clearImpulse();

  transform.setPositionFromPhysics(
      toVec3(controller->getPosition()));
  transform.setRotationFromPhysics({controller->getRotation().x, controller->getRotation().y,
                                    controller->getRotation().z, controller->getRotation().w});
}

void PhysicsSystem::cleanupStale(ecs::World& world) {
  for (auto it = rigid_bodies_.begin(); it != rigid_bodies_.end();) {
    ecs::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) ||
        !world.has<components::RigidbodyComponent>(entity) ||
        !world.has<components::BoxColliderComponent>(entity)) {
      physics_entities_by_handle_.erase(it->second.nativeHandle());
      it->second.destroy();
      it = rigid_bodies_.erase(it);
      box_collider_state_.erase(entityKey(entity));
    } else {
      ++it;
    }
  }

  for (auto it = static_bodies_.begin(); it != static_bodies_.end();) {
    ecs::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) ||
        !world.has<components::MeshColliderComponent>(entity)) {
      physics_entities_by_handle_.erase(it->second.nativeHandle());
      it->second.destroy();
      it = static_bodies_.erase(it);
    } else {
      ++it;
    }
  }

  if (has_player_ &&
      (!world.isAlive(player_entity_) ||
       !world.has<components::PlayerControllerComponent>(player_entity_))) {
    if (physics_.playerController()) {
      physics_.playerController()->destroy();
    }
    if (player_native_handle_ != 0) {
      physics_entities_by_handle_.erase(player_native_handle_);
      player_native_handle_ = 0;
    }
    has_player_ = false;
  }

  for (auto it = previous_contacts_.begin(); it != previous_contacts_.end();) {
    const ecs::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) || !world.has<components::ContactListenerComponent>(entity)) {
      it = previous_contacts_.erase(it);
    } else {
      ++it;
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
    contact.point = hit != nullptr ? toVec3(hit->point) : math::Vec3{};
    contact.normal = hit != nullptr ? toVec3(hit->normal) : math::Vec3{0.0f, 1.0f, 0.0f};
  };

  world.forEach<components::GroundContactComponent>([&](const ecs::Entity entity) {
    if (!collisionEnabled(world, entity)) {
      apply_ground_state(entity, nullptr, {});
      return;
    }

    if (world.has<components::PlayerControllerComponent>(entity)) {
      PhysicsGroundContact hit{};
      const bool grounded = has_player_ && entity == player_entity_ &&
                            physics_.playerController() != nullptr &&
                            physics_.playerController()->getGroundContact(hit);
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
        !world.has<components::BoxColliderComponent>(entity) ||
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

    const auto& collider = world.get<components::BoxColliderComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);
    const math::Vec3 scale = transform.getScale();
    const glm::vec3 full_dimensions{
        std::abs(collider.half_extents.x * scale.x) * 2.0f,
        std::abs(collider.half_extents.y * scale.y) * 2.0f,
        std::abs(collider.half_extents.z * scale.z) * 2.0f,
    };
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
