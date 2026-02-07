#include "karma/physics/physics_system.h"

#include <cmath>

#include "karma/components/mesh.h"
#include "karma/components/visibility.h"

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

ecs::Entity entityFromKey(uint64_t key) {
  return {static_cast<uint32_t>(key >> 32), static_cast<uint32_t>(key & 0xFFFFFFFFu)};
}

bool collisionEnabled(ecs::World& world, ecs::Entity entity) {
  if (!world.has<components::VisibilityComponent>(entity)) {
    return true;
  }
  return world.get<components::VisibilityComponent>(entity).collision_layer_mask != 0;
}

}

void PhysicsSystem::update(ecs::World& world, float dt) {
  syncRigidBodies(world);
  syncPlayerController(world, dt);
  physics_.update(dt);
  syncDynamicBodies(world);
  cleanupStale(world);
}

void PhysicsSystem::syncRigidBodies(ecs::World& world) {
  for (const ecs::Entity entity :
       world.view<components::TransformComponent, components::BoxColliderComponent, components::RigidbodyComponent>()) {
    if (!collisionEnabled(world, entity)) {
      continue;
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
  }

  for (const ecs::Entity entity :
       world.view<components::TransformComponent, components::MeshColliderComponent>()) {
    if (world.has<components::RigidbodyComponent>(entity)) {
      continue;
    }
    if (!collisionEnabled(world, entity)) {
      continue;
    }
    const uint64_t key = entityKey(entity);
    if (static_bodies_.find(key) != static_bodies_.end()) {
      continue;
    }
    if (!world.has<components::MeshComponent>(entity)) {
      continue;
    }
    const auto& mesh = world.get<components::MeshComponent>(entity);
    StaticBody body = physics_.createStaticMesh(mesh.mesh_key);
    static_bodies_.emplace(key, std::move(body));
  }
}

void PhysicsSystem::syncDynamicBodies(ecs::World& world) {
  for (const ecs::Entity entity :
       world.view<components::TransformComponent, components::BoxColliderComponent, components::RigidbodyComponent>()) {
    if (!collisionEnabled(world, entity)) {
      continue;
    }
    auto& body = world.get<components::RigidbodyComponent>(entity);
    if (body.is_kinematic) {
      continue;
    }
    const uint64_t key = entityKey(entity);
    auto it = rigid_bodies_.find(key);
    if (it == rigid_bodies_.end()) {
      continue;
    }
    if (!it->second.isValid()) {
      continue;
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
  }
}

void PhysicsSystem::syncPlayerController(ecs::World& world, float dt) {
  (void)dt;
  if (!has_player_) {
    for (const ecs::Entity entity :
         world.view<components::PlayerControllerComponent, components::TransformComponent>()) {
      if (!collisionEnabled(world, entity)) {
        continue;
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
        continue;
      }
      player_half_extents_ = {half_extents.x, half_extents.y, half_extents.z};
      player_center_ = center;
      player_shape_kind_ = shape_kind;
      auto& controller = physics_.createPlayer(half_extents * 2.0f);
      player_entity_ = entity;
      has_player_ = true;
      auto& transform = world.get<components::TransformComponent>(entity);
      controller.setCenter(toGlm(center));
      controller.setPosition(toGlm(transform.getPosition()));
      break;
    }
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
    has_player_ = false;
  }
}

}  // namespace karma::physics
