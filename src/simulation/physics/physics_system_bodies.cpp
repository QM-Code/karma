#include "karma/physics.h"
#include "physics_system_internal.h"

#include <cmath>

namespace karma::physics {

using namespace system_internal;

void PhysicsSystem::syncRigidBodies(world::World& world) {
  const MeshColliderGeometryResolver resolve_mesh_geometry =
      [this](std::string_view mesh_key) -> const MeshColliderGeometry* {
        return resolveMeshColliderGeometry(mesh_key);
      };

  world.forEach<components::RigidbodyComponent, components::TransformComponent>(
      [&](const world::Entity entity) {
    if (!hasPhysicsCollider(world, entity)) {
      return;
    }
    if (!collisionEnabled(world, entity)) {
      return;
    }
    auto& transform = world.get<components::TransformComponent>(entity);
    auto& body = world.get<components::RigidbodyComponent>(entity);
    PhysicsBodyDesc desc =
        buildBodyDesc(world, entity, transform, &body, PhysicsMotionType::Dynamic,
                      resolve_mesh_geometry);
    const std::size_t signature = bodySignature(desc);

    const uint64_t key = entityKey(entity);
    auto static_it = static_bodies_.find(key);
    if (static_it != static_bodies_.end()) {
      physics_entities_by_handle_.erase(static_it->second.nativeHandle());
      static_it->second.destroy();
      static_bodies_.erase(static_it);
      body_state_.erase(key);
    }

    auto it = rigid_bodies_.find(key);
    auto state_it = body_state_.find(key);
    const bool needs_recreate =
        it == rigid_bodies_.end() ||
        state_it == body_state_.end() ||
        state_it->second.signature != signature;

    if (needs_recreate) {
      if (it != rigid_bodies_.end() && it->second.isValid()) {
        physics_entities_by_handle_.erase(it->second.nativeHandle());
        desc.rotation = it->second.getRotation();
        desc.linear_velocity = it->second.getVelocity();
        desc.angular_velocity = it->second.getAngularVelocity();
        it->second.destroy();
      }
      RigidBody rigid = physics_.createBody(desc);
      it = rigid_bodies_.insert_or_assign(key, std::move(rigid)).first;
      if (it->second.isValid()) {
        physics_entities_by_handle_[it->second.nativeHandle()] = entity;
      }
      body_state_[key] = BodyState{.signature = signature};
    }

    const bool position_dirty = transform.position_dirty_;
    const bool rotation_dirty = transform.rotation_dirty_;
    if (position_dirty || rotation_dirty) {
      if (it->second.isValid()) {
        if (position_dirty) {
          it->second.setPosition(math::toGlm(transform.getPosition()));
        }
        if (rotation_dirty) {
          it->second.setRotation(math::toGlm(transform.getRotation()));
        }
      }
      transform.position_dirty_ = false;
      transform.rotation_dirty_ = false;
    }

    if (it->second.isValid()) {
      if (desc.motion == PhysicsMotionType::Kinematic) {
        it->second.setKinematic(true);
      } else if (desc.motion == PhysicsMotionType::Dynamic) {
        it->second.setKinematic(false);
      }
      if (!body.use_gravity) {
        it->second.setUseGravity(false);
      } else if (std::abs(body.gravity_factor - 1.0f) <= 0.0001f) {
        it->second.setUseGravity(true);
      }
      it->second.setTrigger(body.is_trigger || colliderIsTrigger(world, entity));
      if (desc.motion == PhysicsMotionType::Kinematic) {
        it->second.setVelocity(math::toGlm(body.velocity));
        it->second.setAngularVelocity(math::toGlm(body.angular_velocity));
      }
    }
  });

  world.forEach<components::TransformComponent>(
      [&](const world::Entity entity) {
    if (world.has<components::RigidbodyComponent>(entity)) {
      return;
    }
    if (world.has<components::CharacterControllerComponent>(entity)) {
      return;
    }
    if (!hasPhysicsCollider(world, entity)) {
      return;
    }
    if (!collisionEnabled(world, entity)) {
      return;
    }
    auto& transform = world.get<components::TransformComponent>(entity);
    PhysicsBodyDesc desc =
        buildBodyDesc(world, entity, transform, nullptr, PhysicsMotionType::Static,
                      resolve_mesh_geometry);
    desc.motion = PhysicsMotionType::Static;
    desc.mass = 0.0f;
    const std::size_t signature = bodySignature(desc);

    const uint64_t key = entityKey(entity);
    auto it = static_bodies_.find(key);
    auto state_it = body_state_.find(key);
    const bool needs_recreate =
        it == static_bodies_.end() ||
        state_it == body_state_.end() ||
        state_it->second.signature != signature;

    if (needs_recreate) {
      if (it != static_bodies_.end() && it->second.isValid()) {
        physics_entities_by_handle_.erase(it->second.nativeHandle());
        it->second.destroy();
      }
      RigidBody body = physics_.createBody(desc);
      it = static_bodies_.insert_or_assign(key, std::move(body)).first;
      if (it->second.isValid()) {
        physics_entities_by_handle_[it->second.nativeHandle()] = entity;
      }
      body_state_[key] = BodyState{.signature = signature};
    }

    if (it->second.isValid()) {
      if (transform.position_dirty_) {
        it->second.setPosition(math::toGlm(transform.getPosition()));
      }
      if (transform.rotation_dirty_) {
        it->second.setRotation(math::toGlm(transform.getRotation()));
      }
      transform.position_dirty_ = false;
      transform.rotation_dirty_ = false;
    }
  });
}
void PhysicsSystem::applyBodyForces(world::World& world) {
  auto non_zero = [](const math::Vec3& value) {
    return std::abs(value.x) > 0.000001f ||
           std::abs(value.y) > 0.000001f ||
           std::abs(value.z) > 0.000001f;
  };

  world.forEach<components::PhysicsBodyForcesComponent>(
      [&](const world::Entity entity) {
    const uint64_t key = entityKey(entity);
    auto body_it = rigid_bodies_.find(key);
    if (body_it == rigid_bodies_.end() || !body_it->second.isValid()) {
      return;
    }

    auto& forces = world.get<components::PhysicsBodyForcesComponent>(entity);
    if (non_zero(forces.force)) {
      if (forces.force_at_position) {
        body_it->second.addForceAtPosition(math::toGlm(forces.force),
                                           math::toGlm(forces.force_position));
      } else {
        body_it->second.addForce(math::toGlm(forces.force));
      }
    }
    if (non_zero(forces.torque)) {
      body_it->second.addTorque(math::toGlm(forces.torque));
    }
    if (non_zero(forces.impulse)) {
      if (forces.impulse_at_position) {
        body_it->second.addImpulseAtPosition(math::toGlm(forces.impulse),
                                             math::toGlm(forces.impulse_position));
      } else {
        body_it->second.addImpulse(math::toGlm(forces.impulse));
      }
    }
    if (non_zero(forces.angular_impulse)) {
      body_it->second.addAngularImpulse(math::toGlm(forces.angular_impulse));
    }

    if (forces.clear_after_step) {
      forces.clearTransient();
    }
  });
}
void PhysicsSystem::syncDynamicBodies(world::World& world) {
  world.forEach<components::RigidbodyComponent, components::TransformComponent>(
      [&](const world::Entity entity) {
    if (!collisionEnabled(world, entity)) {
      return;
    }
    if (!hasPhysicsCollider(world, entity)) {
      return;
    }
    auto& body = world.get<components::RigidbodyComponent>(entity);
    if (toPhysicsMotion(body) != PhysicsMotionType::Dynamic) {
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
    const math::Vec3 body_pos = math::fromGlm(it->second.getPosition());
    transform.setPositionFromPhysics(body_pos);
    transform.setRotationFromPhysics(math::fromGlm(it->second.getRotation()));
    body.velocity = math::fromGlm(it->second.getVelocity());
    body.angular_velocity = math::fromGlm(it->second.getAngularVelocity());
  });
}

}  // namespace karma::physics
