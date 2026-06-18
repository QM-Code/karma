#include "karma/simulation/physics/physics_system.h"
#include "physics_system_internal.h"

namespace karma::physics {

using namespace system_internal;

namespace {

template <typename State>
void removeCharacterHandle(State& state,
                           std::unordered_map<std::uintptr_t, ecs::Entity>& entities_by_handle) {
  if (state.native_handle != 0) {
    entities_by_handle.erase(state.native_handle);
    state.native_handle = 0;
  }
}

void publishInvalidController(components::CharacterControllerComponent& component) {
  component.velocity = {};
  component.angular_velocity = {};
  component.forward = {0.0f, 0.0f, -1.0f};
  component.grounded = false;
}

}  // namespace

void PhysicsSystem::syncCharacterControllerObject(ecs::World& world) {
  world.forEach<components::CharacterControllerComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    auto& component = world.get<components::CharacterControllerComponent>(entity);
    const uint64_t key = entityKey(entity);

    if (!component.enabled || !collisionEnabled(world, entity)) {
      auto it = character_controllers_.find(key);
      if (it != character_controllers_.end()) {
        removeCharacterHandle(it->second, physics_entities_by_handle_);
        it->second.controller.destroy();
        character_controllers_.erase(it);
      }
      publishInvalidController(component);
      return;
    }

    const ControllerShapeInfo shape = controllerShapeInfo(world, entity);
    if (!shape.valid) {
      auto it = character_controllers_.find(key);
      if (it != character_controllers_.end()) {
        removeCharacterHandle(it->second, physics_entities_by_handle_);
        it->second.controller.destroy();
        character_controllers_.erase(it);
      }
      publishInvalidController(component);
      return;
    }

    auto& transform = world.get<components::TransformComponent>(entity);
    const math::Vec3 half_extents{shape.half_extents.x,
                                  shape.half_extents.y,
                                  shape.half_extents.z};

    auto it = character_controllers_.find(key);
    const bool needs_create = it == character_controllers_.end();
    const bool size_changed =
        !needs_create && !nearlyEqualVec3(half_extents, it->second.half_extents);
    const bool center_changed =
        !needs_create && !nearlyEqualVec3(shape.center, it->second.center);
    const bool shape_changed =
        !needs_create && shape.shape_kind != it->second.shape_kind;

    if (needs_create || size_changed || shape_changed) {
      glm::quat rotation = math::toGlm(transform.getRotation());
      glm::vec3 velocity{};
      glm::vec3 angular_velocity{};
      if (!needs_create) {
        removeCharacterHandle(it->second, physics_entities_by_handle_);
        rotation = it->second.controller.getRotation();
        velocity = it->second.controller.getVelocity();
        angular_velocity = it->second.controller.getAngularVelocity();
        it->second.controller.destroy();
      }

      CharacterControllerState state{};
      state.controller = physics_.createCharacterController(shape.half_extents * 2.0f);
      state.half_extents = half_extents;
      state.center = shape.center;
      state.shape_kind = shape.shape_kind;
      state.controller.setCenter(math::toGlm(shape.center));
      state.controller.setPosition(math::toGlm(transform.getPosition()));
      state.controller.setRotation(rotation);
      state.controller.setVelocity(velocity);
      state.controller.setAngularVelocity(angular_velocity);
      state.native_handle = state.controller.nativeHandle();
      if (state.native_handle != 0) {
        physics_entities_by_handle_[state.native_handle] = entity;
      }
      it = character_controllers_.insert_or_assign(key, std::move(state)).first;
    }

    auto& state = it->second;
    if (center_changed && !size_changed && !shape_changed) {
      state.controller.setCenter(math::toGlm(shape.center));
      state.controller.setPosition(math::toGlm(transform.getPosition()));
      state.center = shape.center;
    }

    if (transform.position_dirty_) {
      state.controller.setPosition(math::toGlm(transform.getPosition()));
      transform.position_dirty_ = false;
    }
    if (transform.rotation_dirty_) {
      state.controller.setRotation(math::toGlm(transform.getRotation()));
      transform.rotation_dirty_ = false;
    }
  });
}

void PhysicsSystem::applyCharacterControllerInput(ecs::World& world, float dt) {
  if (dt <= 0.0f) {
    return;
  }

  for (auto& [key, state] : character_controllers_) {
    const ecs::Entity entity = entityFromKey(key);
    if (!world.isAlive(entity) ||
        !world.has<components::CharacterControllerComponent>(entity)) {
      continue;
    }

    auto& component = world.get<components::CharacterControllerComponent>(entity);
    if (!component.enabled) {
      continue;
    }

    const math::Vec3 desired = component.desiredVelocity();
    const math::Vec3 impulse = component.addVelocity();
    const glm::vec3 velocity = math::toGlm(desired) + math::toGlm(impulse);
    state.controller.setVelocity(velocity);
    state.controller.setAngularVelocity(math::toGlm(component.desiredAngularVelocity()));
    component.clearImpulse();
    state.controller.update(dt);
  }
}

void PhysicsSystem::syncCharacterControllerTransform(ecs::World& world) {
  for (auto& [key, state] : character_controllers_) {
    const ecs::Entity entity = entityFromKey(key);
    if (!world.isAlive(entity) ||
        !world.has<components::CharacterControllerComponent>(entity) ||
        !world.has<components::TransformComponent>(entity)) {
      continue;
    }

    auto& transform = world.get<components::TransformComponent>(entity);
    auto& component = world.get<components::CharacterControllerComponent>(entity);
    transform.setPositionFromPhysics(math::fromGlm(state.controller.getPosition()));
    transform.setRotationFromPhysics(math::fromGlm(state.controller.getRotation()));
    component.velocity = math::fromGlm(state.controller.getVelocity());
    component.angular_velocity = math::fromGlm(state.controller.getAngularVelocity());
    component.forward = math::fromGlm(state.controller.getForwardVector());
    component.grounded = state.controller.isGrounded();
  }
}

}  // namespace karma::physics
