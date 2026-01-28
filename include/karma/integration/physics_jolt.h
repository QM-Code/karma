#pragma once

#include <unordered_map>

#include "karma/core/id.h"
#include "karma/components/collider.h"
#include "karma/components/rigidbody.h"
#include "karma/components/transform.h"

namespace karma::integration {

// Pseudocode sketch for a Jolt-backed physics world.
class JoltPhysicsBackend {
 public:
  void initialize() {
    // Create Jolt physics system + job system.
  }

  void syncNewBodies(/* ecs::World& world */) {
    // For each entity with Rigidbody + Collider but no body:
    // 1) Create Jolt body
    // 2) Store body id in entity_to_body_
  }

  void stepSimulation(float dt) {
    // Advance Jolt simulation.
  }

  void writeBackTransforms(/* ecs::World& world */) {
    // For each body, read position/rotation and write to TransformComponent.
  }

 private:
  std::unordered_map<uint64_t, uint32_t> entity_to_body_;
};

}  // namespace karma::integration
