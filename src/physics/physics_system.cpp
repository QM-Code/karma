#include "karma/physics/physics_system.h"

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

bool isBoxCollider(const components::ColliderComponent& collider) {
  return collider.shape == components::ColliderComponent::Shape::Box;
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
  teleports_.clear();
  syncRigidBodies(world);
  syncPlayerController(world, dt);
  physics_.update(dt);
  syncDynamicBodies(world);
  applyTeleports(world);
  cleanupStale(world);
}

void PhysicsSystem::syncRigidBodies(ecs::World& world) {
  for (const ecs::Entity entity :
       world.view<components::TransformComponent, components::ColliderComponent, components::RigidbodyComponent>()) {
    if (!collisionEnabled(world, entity)) {
      continue;
    }
    const auto& collider = world.get<components::ColliderComponent>(entity);
    if (!isBoxCollider(collider)) {
      continue;
    }

    const uint64_t key = entityKey(entity);
    auto it = rigid_bodies_.find(key);
    if (it == rigid_bodies_.end()) {
      const auto& transform = world.get<components::TransformComponent>(entity);
      const auto& body = world.get<components::RigidbodyComponent>(entity);
      PhysicsMaterial material;
      RigidBody rigid = physics_.createBoxBody(
          toGlm(collider.half_extents),
          body.mass,
          toGlm(transform.position()),
          material);
      it = rigid_bodies_.emplace(key, std::move(rigid)).first;
    }

    auto& body = world.get<components::RigidbodyComponent>(entity);
    auto& transform = world.get<components::TransformComponent>(entity);
    transform.setHasPhysics(true);
    transform.setPhysicsWriteWarning(!body.is_kinematic);

    math::Vec3 teleport_position{};
    if (body.consumeTeleport(teleport_position)) {
      teleports_[key] = TeleportRequest{teleport_position, transform.rotation()};
      body.velocity = {0.0f, 0.0f, 0.0f};
      body.angular_velocity = {0.0f, 0.0f, 0.0f};
      continue;
    }

    if (body.is_kinematic) {
      if (!it->second.isValid()) {
        continue;
      }
      it->second.setPosition(toGlm(transform.position()));
      it->second.setRotation(toGlm(transform.rotation()));
      it->second.setVelocity(toGlm(body.velocity));
      it->second.setAngularVelocity(toGlm(body.angular_velocity));
      body.syncPosition(transform.position());
    }
  }

  for (const ecs::Entity entity :
       world.view<components::TransformComponent, components::ColliderComponent>()) {
    if (world.has<components::RigidbodyComponent>(entity)) {
      continue;
    }
    if (!collisionEnabled(world, entity)) {
      continue;
    }
    const auto& collider = world.get<components::ColliderComponent>(entity);
    if (collider.shape != components::ColliderComponent::Shape::Mesh) {
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

void PhysicsSystem::applyTeleports(ecs::World& world) {
  for (const auto& [key, teleport] : teleports_) {
    auto it = rigid_bodies_.find(key);
    if (it == rigid_bodies_.end()) {
      continue;
    }
    if (it->second.isValid()) {
      it->second.setPosition(toGlm(teleport.position));
      it->second.setRotation(toGlm(teleport.rotation));
      it->second.setVelocity({0.0f, 0.0f, 0.0f});
      it->second.setAngularVelocity({0.0f, 0.0f, 0.0f});
    }

    ecs::Entity entity = entityFromKey(key);
    if (!world.isAlive(entity)) {
      continue;
    }
    if (world.has<components::TransformComponent>(entity)) {
      auto& transform = world.get<components::TransformComponent>(entity);
      transform.setPosition(teleport.position, components::TransformWriteMode::AllowPhysics);
      transform.setRotation(teleport.rotation, components::TransformWriteMode::AllowPhysics);
    }
    if (world.has<components::RigidbodyComponent>(entity)) {
      auto& body = world.get<components::RigidbodyComponent>(entity);
      body.velocity = {0.0f, 0.0f, 0.0f};
      body.angular_velocity = {0.0f, 0.0f, 0.0f};
      body.syncPosition(teleport.position);
    }
  }
}

void PhysicsSystem::syncDynamicBodies(ecs::World& world) {
  for (const ecs::Entity entity :
       world.view<components::TransformComponent, components::ColliderComponent, components::RigidbodyComponent>()) {
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
    transform.setPosition(toVec3(it->second.getPosition()),
                          components::TransformWriteMode::AllowPhysics);
    transform.setRotation({it->second.getRotation().x, it->second.getRotation().y,
                           it->second.getRotation().z, it->second.getRotation().w},
                          components::TransformWriteMode::AllowPhysics);
    body.velocity = toVec3(it->second.getVelocity());
    body.angular_velocity = toVec3(it->second.getAngularVelocity());
    body.syncPosition(transform.position());
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
      const auto& collider = world.get<components::ColliderComponent>(entity);
      const glm::vec3 half_extents = toGlm(collider.half_extents);
      auto& controller = physics_.createPlayer(half_extents * 2.0f);
      player_entity_ = entity;
      has_player_ = true;
      auto& transform = world.get<components::TransformComponent>(entity);
      controller.setPosition(toGlm(transform.position()));
      break;
    }
  }

  if (!has_player_) {
    return;
  }

  if (!physics_.playerController()) {
    return;
  }
  auto& controller = *physics_.playerController();
  auto& transform = world.get<components::TransformComponent>(player_entity_);
  auto& input = world.get<components::PlayerControllerComponent>(player_entity_);

  const math::Vec3 desired = input.desiredVelocity();
  const math::Vec3 impulse = input.addVelocity();
  glm::vec3 velocity = toGlm(desired) + toGlm(impulse);
  controller.setVelocity(velocity);
  input.clearImpulse();

  transform.setPosition(toVec3(controller.getPosition()),
                        components::TransformWriteMode::AllowPhysics);
  transform.setRotation({controller.getRotation().x, controller.getRotation().y,
                         controller.getRotation().z, controller.getRotation().w},
                        components::TransformWriteMode::AllowPhysics);
}

void PhysicsSystem::cleanupStale(ecs::World& world) {
  for (auto it = rigid_bodies_.begin(); it != rigid_bodies_.end();) {
    ecs::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) ||
        !world.has<components::RigidbodyComponent>(entity)) {
      it->second.destroy();
      it = rigid_bodies_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = static_bodies_.begin(); it != static_bodies_.end();) {
    ecs::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) ||
        !world.has<components::ColliderComponent>(entity)) {
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
