#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "karma/world/components/collider.h"
#include "karma/world/components/contact_events.h"
#include "karma/world/components/ground_contact.h"
#include "karma/world/components/character_controller.h"
#include "karma/world/components/physics_collision_filter.h"
#include "karma/world/components/physics_constraint.h"
#include "karma/world/components/physics_material.h"
#include "karma/world/components/physics_soft_body.h"
#include "karma/world/components/physics_vehicle.h"
#include "karma/world/components/rigidbody.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/collider_queries.h"
#include "karma/world/ecs/world.h"
#include "karma/simulation/physics/character_controller.hpp"
#include "karma/simulation/physics/constraint.hpp"
#include "karma/simulation/physics/physics_world.hpp"
#include "karma/simulation/physics/soft_body.hpp"
#include "karma/simulation/physics/vehicle.hpp"
#include "karma/world/systems/system.h"

namespace karma::physics {

/// \ingroup karma_physics
/// Local-space geometry used when a mesh collider refers to external mesh content.
struct MeshColliderGeometry {
  std::vector<math::Vec3> vertices;
  std::vector<uint32_t> indices;
};

using MeshColliderGeometryProvider =
    std::function<std::optional<MeshColliderGeometry>(std::string_view mesh_key)>;

/// \ingroup karma_physics
/// Syncs ECS physics components with the configured physics backend.
///
/// The system creates/destroys backend bodies for ECS rigid bodies, applies
/// character-controller intent, steps physics, writes transforms, and emits
/// contact/ground-state components.
class PhysicsSystem : public systems::ISystem {
 public:
  explicit PhysicsSystem(World& physics) : physics_(physics) {}

  void setMeshColliderGeometryProvider(MeshColliderGeometryProvider provider) {
    mesh_collider_geometry_provider_ = std::move(provider);
    mesh_collider_geometry_cache_.clear();
  }

  void update(ecs::World& world, float dt) override;
  std::string_view name() const override { return "PhysicsSystem"; }

 private:
  static uint64_t entityKey(ecs::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  void syncSimulationObjects(ecs::World& world);
  void applySimulationInputs(ecs::World& world, float dt);
  void stepSimulation(float dt);
  void publishSimulationResults(ecs::World& world);
  void publishSimulationEvents(ecs::World& world);
  void syncRigidBodies(ecs::World& world);
  void syncVehicles(ecs::World& world);
  void syncSoftBodies(ecs::World& world);
  void syncConstraints(ecs::World& world);
  void applyBodyForces(ecs::World& world);
  void syncDynamicBodies(ecs::World& world);
  void syncCharacterControllerObject(ecs::World& world);
  void applyCharacterControllerInput(ecs::World& world, float dt);
  void syncCharacterControllerTransform(ecs::World& world);
  void syncContactEvents(ecs::World& world);
  void syncGroundContacts(ecs::World& world);
  void cleanupStale(ecs::World& world);
  const MeshColliderGeometry* resolveMeshColliderGeometry(std::string_view mesh_key);

  struct TrackedContact {
    ecs::Entity other{};
    components::ColliderShapeType other_shape = components::ColliderShapeType::Box;
    math::Vec3 point{};
    math::Vec3 normal{0.0f, 1.0f, 0.0f};
  };

  using ContactMap = std::unordered_map<uint64_t, TrackedContact>;

  World& physics_;
  std::unordered_map<uint64_t, RigidBody> rigid_bodies_;
  struct BodyState {
    std::size_t signature = 0;
  };
  std::unordered_map<uint64_t, BodyState> body_state_;
  std::unordered_map<uint64_t, RigidBody> static_bodies_;
  std::unordered_map<uint64_t, Constraint> constraints_;
  std::unordered_map<uint64_t, std::size_t> constraint_signatures_;
  std::unordered_map<uint64_t, Vehicle> vehicles_;
  std::unordered_map<uint64_t, std::size_t> vehicle_signatures_;
  std::unordered_map<uint64_t, SoftBody> soft_bodies_;
  std::unordered_map<uint64_t, std::size_t> soft_body_signatures_;
  struct CharacterControllerState {
    CharacterController controller{};
    math::Vec3 half_extents{-1.0f, -1.0f, -1.0f};
    math::Vec3 center{};
    std::uintptr_t native_handle = 0;
    int shape_kind = -1;
  };
  std::unordered_map<uint64_t, CharacterControllerState> character_controllers_;
  std::unordered_map<std::uintptr_t, ecs::Entity> physics_entities_by_handle_;
  std::unordered_map<uint64_t, ContactMap> previous_contacts_;
  MeshColliderGeometryProvider mesh_collider_geometry_provider_;
  std::unordered_map<std::string, MeshColliderGeometry> mesh_collider_geometry_cache_;
};

}  // namespace karma::physics
