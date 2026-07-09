#include "karma/physics.h"
#include "physics_system_internal.h"

#include <optional>
#include <string>
#include <utility>

namespace karma::physics {

using namespace system_internal;

const MeshColliderGeometry* PhysicsSystem::resolveMeshColliderGeometry(std::string_view mesh_key) {
  if (mesh_key.empty() || !mesh_collider_geometry_provider_) {
    return nullptr;
  }

  std::string key(mesh_key);
  if (const auto cached = mesh_collider_geometry_cache_.find(key);
      cached != mesh_collider_geometry_cache_.end()) {
    return &cached->second;
  }

  std::optional<MeshColliderGeometry> resolved = mesh_collider_geometry_provider_(mesh_key);
  if (!resolved || resolved->vertices.empty() || resolved->indices.empty()) {
    return nullptr;
  }

  auto [inserted, _] = mesh_collider_geometry_cache_.emplace(std::move(key), std::move(*resolved));
  return &inserted->second;
}
void PhysicsSystem::update(world::World& world, float dt) {
  // Keep backend object lifecycle, input commands, simulation stepping,
  // state publication, and event publication as separate ownership phases.
  // Retire removed or disabled objects before the backend advances so they
  // cannot participate in one extra simulation step.
  cleanupStale(world);
  syncSimulationObjects(world);
  applySimulationInputs(world, dt);
  stepSimulation(dt);
  publishSimulationResults(world);
  publishSimulationEvents(world);
}
void PhysicsSystem::syncSimulationObjects(world::World& world) {
  syncRigidBodies(world);
  syncVehicles(world);
  syncConstraints(world);
  syncSoftBodies(world);
  syncCharacterControllerObject(world);
}
void PhysicsSystem::applySimulationInputs(world::World& world, float dt) {
  applyCharacterControllerInput(world, dt);
  applyBodyForces(world);
}
void PhysicsSystem::stepSimulation(float dt) {
  physics_.update(dt);
}
void PhysicsSystem::publishSimulationResults(world::World& world) {
  syncDynamicBodies(world);
  syncCharacterControllerTransform(world);
  syncVehicles(world);
  syncSoftBodies(world);
}
void PhysicsSystem::publishSimulationEvents(world::World& world) {
  syncContactEvents(world);
  syncGroundContacts(world);
}

}  // namespace karma::physics
