# Engine integration overview (pseudocode)

This document sketches how the engine layer would bind to external libraries
(Diligent, Jolt, etc.) without hard-coding those libraries into gameplay code.

## Pattern: API + backend + factory

1. Public engine API (header-only or thin wrapper)
2. Backend interface (pure virtual)
3. Backend implementation (library-specific)
4. Factory returns chosen backend based on build flags

## Rendering flow (Diligent example)

- Engine systems submit renderable data (transform + mesh/material keys)
- Renderer resolves assets to GPU resources
- Backend translates to Diligent calls

Pseudocode:

```cpp
// engine/renderer/render_system.hpp
void RenderSystem::update(World& world, float dt) {
  for (auto entity : view<TransformComponent, MeshComponent>()) {
    const auto& transform = world.get<TransformComponent>(entity);
    const auto& mesh = world.get<MeshComponent>(entity);

    if (!mesh.visible) {
      backend_->setVisible(entity, false);
      continue;
    }

    backend_->setVisible(entity, true);
    backend_->setTransform(entity, transform);
    backend_->setMesh(entity, mesh.mesh_key);
    backend_->setMaterial(entity, mesh.material_key);
  }
  backend_->present();
}
```

## Physics flow (Jolt example)

- PhysicsSystem creates bodies for entities with Rigidbody/Collider
- Backend owns Jolt world and maps entity IDs -> Jolt body IDs
- Simulation writes back positions to TransformComponent

Pseudocode:

```cpp
void PhysicsSystem::update(World& world, float dt) {
  if (!backend_) return;

  backend_->syncNewEntities(world);  // create missing bodies
  backend_->stepSimulation(dt);
  backend_->writeBackTransforms(world);  // Jolt -> ECS
}
```

## Audio flow (miniaudio/SDL example)

- AudioSystem updates listener from active camera
- Entities with AudioSourceComponent trigger play/stop

Pseudocode:

```cpp
void AudioSystem::update(World& world, float dt) {
  backend_->setListener(camera_position, camera_rotation);
  for (auto entity : view<TransformComponent, AudioSourceComponent>()) {
    if (shouldPlay(entity)) {
      backend_->playClip(source.clip_key, transform.position, source.gain);
    }
  }
}
```
