#include "karma/physics.h"
#include "physics_system_internal.h"

namespace karma::physics {

using namespace system_internal;

void PhysicsSystem::syncSoftBodies(world::World& world) {
  world.forEach<components::PhysicsSoftBodyComponent>([&](const world::Entity entity) {
    auto& component = world.get<components::PhysicsSoftBodyComponent>(entity);
    const uint64_t key = entityKey(entity);
    auto* transform = world.has<components::TransformComponent>(entity)
        ? &world.get<components::TransformComponent>(entity)
        : nullptr;

    if (!component.enabled) {
      auto it = soft_bodies_.find(key);
      if (it != soft_bodies_.end()) {
        physics_entities_by_handle_.erase(it->second.nativeHandle());
        it->second.destroy();
        soft_bodies_.erase(it);
      }
      soft_body_signatures_.erase(key);
      return;
    }

    PhysicsSoftBodyDesc desc = buildSoftBodyDesc(component, transform);
    const std::size_t signature = softBodySignature(desc);
    auto it = soft_bodies_.find(key);
    auto signature_it = soft_body_signatures_.find(key);
    const bool transform_dirty = transform != nullptr &&
                                 (transform->position_dirty_ || transform->rotation_dirty_);
    const bool needs_recreate =
        component.recreate ||
        transform_dirty ||
        it == soft_bodies_.end() ||
        signature_it == soft_body_signatures_.end() ||
        signature_it->second != signature;

    if (needs_recreate) {
      if (it != soft_bodies_.end()) {
        physics_entities_by_handle_.erase(it->second.nativeHandle());
        it->second.destroy();
      }
      SoftBody soft_body = physics_.createSoftBody(desc);
      it = soft_bodies_.insert_or_assign(key, std::move(soft_body)).first;
      soft_body_signatures_[key] = signature;
      component.recreate = false;
      if (it->second.isValid()) {
        physics_entities_by_handle_[it->second.nativeHandle()] = entity;
      }
      if (transform != nullptr) {
        transform->position_dirty_ = false;
        transform->rotation_dirty_ = false;
      }
    }

    if (it->second.isValid()) {
      it->second.setPressure(component.pressure);
      it->second.setUpdatePosition(component.update_position);
      const PhysicsSoftBodyState state = it->second.getState();
      if (transform != nullptr && state.valid) {
        transform->setPositionFromPhysics(math::fromGlm(state.position));
        transform->setRotationFromPhysics(math::fromGlm(state.rotation));
      }
    }
  });
}

}  // namespace karma::physics
