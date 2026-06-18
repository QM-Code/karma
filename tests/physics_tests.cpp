#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

#include "karma/features/visual/terrain/terrain_system.h"
#include "karma/simulation/physics/physics_system.h"
#include "karma/simulation/physics/physics_world.hpp"
#include "karma/world/components/character_controller.h"
#include "karma/world/components/collider.h"
#include "karma/world/components/contact_events.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/physics_collision_filter.h"
#include "karma/world/components/rigidbody.h"
#include "karma/world/components/terrain.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"
#include "karma/world/scene/scene.h"
#include "karma/world/scene/transform_hierarchy.h"

namespace {

constexpr uint32_t kLayerA = 1u << 0u;
constexpr uint32_t kLayerB = 1u << 1u;
constexpr uint32_t kLayerC = 1u << 2u;

template <typename Fn>
bool throwsRuntimeError(Fn&& fn) {
  try {
    fn();
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

void componentRequirementsAreStrict() {
  karma::ecs::World world;

  assert(throwsRuntimeError([&] {
    world.add(karma::ecs::Entity{}, karma::components::TransformComponent{});
  }));

  const karma::ecs::Entity dead = world.createEntity();
  world.destroyEntity(dead);
  assert(throwsRuntimeError([&] {
    world.add(dead, karma::components::TransformComponent{});
  }));

  const karma::ecs::Entity collider_without_transform = world.createEntity();
  assert(throwsRuntimeError([&] {
    world.add(collider_without_transform, karma::components::ColliderComponent::box());
  }));

  const karma::ecs::Entity rigidbody_without_collider = world.createEntity();
  world.add(rigidbody_without_collider, karma::components::TransformComponent{});
  assert(throwsRuntimeError([&] {
    world.add(rigidbody_without_collider, karma::components::RigidbodyComponent{});
  }));

  const karma::ecs::Entity controller_without_collider = world.createEntity();
  world.add(controller_without_collider, karma::components::TransformComponent{});
  assert(throwsRuntimeError([&] {
    world.add(controller_without_collider, karma::components::CharacterControllerComponent{});
  }));

  const karma::ecs::Entity controller_with_mesh = world.createEntity();
  world.add(controller_with_mesh, karma::components::TransformComponent{});
  world.add(controller_with_mesh, karma::components::ColliderComponent::mesh());
  assert(throwsRuntimeError([&] {
    world.add(controller_with_mesh, karma::components::CharacterControllerComponent{});
  }));

  const karma::ecs::Entity controller_with_mismatched_shape = world.createEntity();
  world.add(controller_with_mismatched_shape, karma::components::TransformComponent{});
  auto mismatched = karma::components::ColliderComponent::box();
  mismatched.type = karma::components::ColliderShapeType::Sphere;
  world.add(controller_with_mismatched_shape, mismatched);
  assert(throwsRuntimeError([&] {
    world.add(controller_with_mismatched_shape,
              karma::components::CharacterControllerComponent{});
  }));

  const karma::ecs::Entity valid_controller = world.createEntity();
  world.add(valid_controller, karma::components::TransformComponent{});
  world.add(valid_controller, karma::components::ColliderComponent::box());
  world.add(valid_controller, karma::components::CharacterControllerComponent{});
  assert(world.has<karma::components::CharacterControllerComponent>(valid_controller));

  const karma::ecs::Entity valid_rigidbody = world.createEntity();
  world.add(valid_rigidbody, karma::components::TransformComponent{});
  world.add(valid_rigidbody, karma::components::ColliderComponent::box());
  world.add(valid_rigidbody, karma::components::RigidbodyComponent{});
  assert(world.has<karma::components::RigidbodyComponent>(valid_rigidbody));
}

std::size_t overlappingContactCount(bool allow_contact) {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  karma::ecs::World world;
  karma::physics::World physics_world;
  physics_world.setGravity(0.0f);
  karma::physics::PhysicsSystem physics_system(physics_world);

  const karma::ecs::Entity dynamic_body = world.createEntity();
  world.add(dynamic_body, karma::components::TransformComponent{});
  world.add(dynamic_body,
            karma::components::ColliderComponent::box(
                karma::components::BoxColliderShape{
                    .center = {0.0f, 1.0f, 0.0f},
                    .half_extents = {1.0f, 1.0f, 1.0f},
                }));
  world.add(dynamic_body, karma::components::RigidbodyComponent{
                              .mass = 1.0f,
                              .allow_sleeping = false,
                          });
  world.add(dynamic_body, karma::components::PhysicsCollisionFilterComponent{
                              .layers = kLayerA,
                              .collides_with = allow_contact ? kLayerB : kLayerC,
                          });
  world.add(dynamic_body, karma::components::ContactListenerComponent{.emit_stay = true});

  const karma::ecs::Entity static_body = world.createEntity();
  world.add(static_body, karma::components::TransformComponent{{0.25f, 0.0f, 0.0f}});
  world.add(static_body,
            karma::components::ColliderComponent::box(
                karma::components::BoxColliderShape{
                    .center = {0.0f, 1.0f, 0.0f},
                    .half_extents = {1.0f, 1.0f, 1.0f},
                }));
  world.add(static_body, karma::components::PhysicsCollisionFilterComponent{
                             .layers = kLayerB,
                             .collides_with = kLayerA,
                         });

  for (int i = 0; i < 4; ++i) {
    physics_system.update(world, 1.0f / 60.0f);
  }

  assert(world.has<karma::components::ContactEventsComponent>(dynamic_body));
  return world.get<karma::components::ContactEventsComponent>(dynamic_body).active.size();
#else
  (void)allow_contact;
  return 0;
#endif
}

void queryApiFindsFilteredBodies() {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  karma::physics::World physics_world;
  physics_world.setGravity(0.0f);

  karma::physics::PhysicsBodyDesc body_desc;
  body_desc.motion = karma::physics::PhysicsMotionType::Static;
  body_desc.shape.type = karma::physics::PhysicsShapeType::Box;
  body_desc.shape.half_extents = {1.0f, 1.0f, 1.0f};
  body_desc.collision_filter.layers = kLayerA;
  body_desc.collision_filter.collides_with = 0xFFFFFFFFu;
  karma::physics::RigidBody body = physics_world.createBody(body_desc);
  assert(body.isValid());
  const std::uintptr_t body_handle = body.nativeHandle();

  karma::physics::PhysicsRaycastDesc ray;
  ray.from = {-3.0f, 0.0f, 0.0f};
  ray.to = {3.0f, 0.0f, 0.0f};
  ray.filter.collision_mask = kLayerA;
  karma::physics::PhysicsQueryHit hit{};
  assert(physics_world.castRay(ray, hit));
  assert(hit.body == body_handle);

  ray.filter.collision_mask = kLayerB;
  assert(!physics_world.castRay(ray, hit));

  std::vector<karma::physics::PhysicsQueryHit> hits;
  physics_world.collidePoint({0.0f, 0.0f, 0.0f},
                             karma::physics::PhysicsQueryFilter{.collision_mask = kLayerA},
                             hits);
  assert(!hits.empty());
  assert(hits.front().body == body_handle);

  hits.clear();
  physics_world.collidePoint({0.0f, 0.0f, 0.0f},
                             karma::physics::PhysicsQueryFilter{.collision_mask = kLayerB},
                             hits);
  assert(hits.empty());

  karma::physics::PhysicsShapeQueryDesc overlap;
  overlap.shape.type = karma::physics::PhysicsShapeType::Sphere;
  overlap.shape.radius = 0.5f;
  overlap.filter.collision_mask = kLayerA;
  physics_world.collideShape(overlap, hits);
  assert(!hits.empty());
  assert(hits.front().body == body_handle);

  hits.clear();
  karma::physics::PhysicsShapeCastDesc cast;
  cast.shape.type = karma::physics::PhysicsShapeType::Sphere;
  cast.shape.radius = 0.25f;
  cast.from = {-4.0f, 0.0f, 0.0f};
  cast.translation = {8.0f, 0.0f, 0.0f};
  cast.filter.collision_mask = kLayerA;
  physics_world.castShape(cast, hits);
  assert(!hits.empty());
  assert(hits.front().body == body_handle);
#endif
}

void meshColliderGeometryProviderBuildsExternalMeshCollider() {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  karma::ecs::World world;
  karma::physics::World physics_world;
  physics_world.setGravity(0.0f);
  karma::physics::PhysicsSystem physics_system(physics_world);

  int resolve_count = 0;
  physics_system.setMeshColliderGeometryProvider(
      [&](std::string_view mesh_key) -> std::optional<karma::physics::MeshColliderGeometry> {
        assert(mesh_key == "runtime/quad");
        ++resolve_count;
        return karma::physics::MeshColliderGeometry{
            .vertices = {
                {-2.0f, 0.0f, -2.0f},
                {2.0f, 0.0f, -2.0f},
                {2.0f, 0.0f, 2.0f},
                {-2.0f, 0.0f, 2.0f},
            },
            .indices = {0, 2, 1, 0, 3, 2},
        };
      });

  const karma::ecs::Entity terrain = world.createEntity();
  world.add(terrain, karma::components::TransformComponent{});
  world.add(terrain, karma::components::MeshComponent{.mesh_key = "runtime/quad"});
  world.add(terrain, karma::components::ColliderComponent::mesh());

  physics_system.update(world, 1.0f / 60.0f);
  assert(resolve_count == 1);

  karma::physics::PhysicsRaycastDesc ray;
  ray.from = {0.0f, 2.0f, 0.0f};
  ray.to = {0.0f, -2.0f, 0.0f};
  karma::physics::PhysicsQueryHit hit{};
  assert(physics_world.castRay(ray, hit));

  physics_system.update(world, 1.0f / 60.0f);
  assert(resolve_count == 1);
#endif
}

void characterControllerStaysStableOnMeshColliderGround() {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  karma::ecs::World world;
  karma::physics::World physics_world;
  physics_world.setGravity(-9.8f);
  karma::physics::PhysicsSystem physics_system(physics_world);

  physics_system.setMeshColliderGeometryProvider(
      [](std::string_view mesh_key) -> std::optional<karma::physics::MeshColliderGeometry> {
        assert(mesh_key == "runtime/ground");
        return karma::physics::MeshColliderGeometry{
            .vertices = {
                {-20.0f, 0.0f, -20.0f},
                {20.0f, 0.0f, -20.0f},
                {20.0f, 0.0f, 20.0f},
                {-20.0f, 0.0f, 20.0f},
            },
            .indices = {0, 2, 1, 0, 3, 2},
        };
      });

  const karma::ecs::Entity ground = world.createEntity();
  world.add(ground, karma::components::TransformComponent{});
  world.add(ground, karma::components::MeshComponent{.mesh_key = "runtime/ground"});
  world.add(ground, karma::components::ColliderComponent::mesh());

  const karma::ecs::Entity player = world.createEntity();
  world.add(player, karma::components::TransformComponent{});
  world.add(player,
            karma::components::ColliderComponent::box(
                karma::components::BoxColliderShape{
                    .center = {0.0f, 1.0f, 0.0f},
                    .half_extents = {1.0f, 1.0f, 1.0f},
                }));
  world.add(player, karma::components::CharacterControllerComponent{});

  for (int i = 0; i < 60; ++i) {
    physics_system.update(world, 1.0f / 60.0f);
  }

  const auto& transform = world.get<karma::components::TransformComponent>(player);
  const float y = transform.getPosition().y;
  assert(y > -0.05f);
  assert(y < 0.25f);
  assert(world.get<karma::components::CharacterControllerComponent>(player).grounded);
#endif
}

void multipleCharacterControllersMoveIndependently() {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  karma::ecs::World world;
  karma::physics::World physics_world;
  physics_world.setGravity(0.0f);
  karma::physics::PhysicsSystem physics_system(physics_world);

  auto make_controller = [&](const karma::math::Vec3& start,
                             const karma::math::Vec3& velocity) {
    const karma::ecs::Entity entity = world.createEntity();
    world.add(entity, karma::components::TransformComponent{start});
    world.add(entity,
              karma::components::ColliderComponent::box(
                  karma::components::BoxColliderShape{
                      .center = {0.0f, 1.0f, 0.0f},
                      .half_extents = {0.5f, 1.0f, 0.5f},
                  }));
    karma::components::CharacterControllerComponent controller{};
    controller.setDesiredVelocity(velocity);
    world.add(entity, controller);
    return entity;
  };

  const karma::ecs::Entity a = make_controller({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  const karma::ecs::Entity b = make_controller({0.0f, 0.0f, 4.0f}, {0.0f, 0.0f, -1.0f});

  for (int i = 0; i < 30; ++i) {
    physics_system.update(world, 1.0f / 60.0f);
  }

  const auto& transform_a = world.get<karma::components::TransformComponent>(a);
  const auto& transform_b = world.get<karma::components::TransformComponent>(b);
  assert(transform_a.getPosition().x > 0.25f);
  assert(std::abs(transform_a.getPosition().z) < 0.05f);
  assert(transform_b.getPosition().z < 3.75f);
  assert(std::abs(transform_b.getPosition().x) < 0.05f);
  assert(world.get<karma::components::CharacterControllerComponent>(a).velocity.x > 0.5f);
  assert(world.get<karma::components::CharacterControllerComponent>(b).velocity.z < -0.5f);
#endif
}

void sceneHierarchyPreservesPhysicsInterpolationSpan() {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  karma::ecs::World world;
  karma::scene::Scene scene;
  karma::physics::World physics_world;
  physics_world.setGravity(0.0f);
  karma::physics::PhysicsSystem physics_system(physics_world);

  const karma::ecs::Entity body = world.createEntity();
  world.add(body, karma::components::TransformComponent{});
  scene.createNode(body);
  world.add(body,
            karma::components::ColliderComponent::box(
                karma::components::BoxColliderShape{
                    .half_extents = {0.5f, 0.5f, 0.5f},
                }));
  world.add(body, karma::components::RigidbodyComponent{
                      .mass = 1.0f,
                      .velocity = {6.0f, 0.0f, 0.0f},
                      .use_gravity = false,
                      .allow_sleeping = false,
                  });

  physics_system.update(world, 1.0f / 60.0f);

  const auto& before_hierarchy = world.get<karma::components::TransformComponent>(body);
  const float previous_x = before_hierarchy.getInterpolatedPosition(0.0f).x;
  const float mid_x = before_hierarchy.getInterpolatedPosition(0.5f).x;
  const float current_x = before_hierarchy.getInterpolatedPosition(1.0f).x;
  assert(previous_x < mid_x);
  assert(mid_x < current_x);

  karma::scene::updateWorldTransforms(world, scene);

  const auto& after_hierarchy = world.get<karma::components::TransformComponent>(body);
  assert(std::abs(after_hierarchy.getInterpolatedPosition(0.0f).x - previous_x) < 0.0001f);
  assert(std::abs(after_hierarchy.getInterpolatedPosition(0.5f).x - mid_x) < 0.0001f);
  assert(std::abs(after_hierarchy.getInterpolatedPosition(1.0f).x - current_x) < 0.0001f);
#endif
}

void rigidBodyRuntimeControlsWork() {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  karma::physics::World physics_world;
  physics_world.setGravity(0.0f);

  karma::physics::PhysicsBodyDesc desc;
  desc.motion = karma::physics::PhysicsMotionType::Dynamic;
  desc.mass = 1.0f;
  desc.allow_sleeping = true;
  desc.shape.type = karma::physics::PhysicsShapeType::Box;
  desc.shape.half_extents = {0.5f, 0.5f, 0.5f};
  karma::physics::RigidBody body = physics_world.createBody(desc);
  assert(body.isValid());

  body.setFriction(0.75f);
  assert(std::abs(body.getFriction() - 0.75f) < 0.0001f);
  body.setRestitution(0.35f);
  assert(std::abs(body.getRestitution() - 0.35f) < 0.0001f);
  body.setGravityFactor(0.25f);
  assert(std::abs(body.getGravityFactor() - 0.25f) < 0.0001f);
  body.setUserData(0x1234u);
  assert(body.getUserData() == 0x1234u);

  body.setUseManifoldReduction(false);
  assert(!body.getUseManifoldReduction());
  body.setUseManifoldReduction(true);
  assert(body.getUseManifoldReduction());

  body.setLinearAndAngularVelocity({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  assert(std::abs(body.getVelocity().x - 1.0f) < 0.0001f);
  assert(std::abs(body.getAngularVelocity().y - 1.0f) < 0.0001f);
  body.addLinearVelocity({1.0f, 0.0f, 0.0f});
  assert(std::abs(body.getVelocity().x - 2.0f) < 0.0001f);
  assert(body.getPointVelocity({0.0f, 0.0f, 1.0f}).x != 0.0f);

  body.deactivate();
  assert(!body.isActive());
  body.activate();
  assert(body.isActive());
  body.resetSleepTimer();

  karma::physics::PhysicsShapeDesc sphere;
  sphere.type = karma::physics::PhysicsShapeType::Sphere;
  sphere.radius = 0.5f;
  assert(body.setShape(sphere));

  karma::physics::PhysicsBuoyancyDesc buoyancy;
  buoyancy.surface_position = {0.0f, 0.0f, 0.0f};
  buoyancy.surface_normal = {0.0f, 1.0f, 0.0f};
  buoyancy.buoyancy = 1.0f;
  buoyancy.delta_time = 1.0f / 60.0f;
  (void)body.applyBuoyancyImpulse(buoyancy);
#endif
}

void vehicleConstraintCreatesAndReportsState() {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  karma::physics::World physics_world;
  physics_world.setGravity(-9.8f);

  karma::physics::PhysicsBodyDesc body_desc;
  body_desc.motion = karma::physics::PhysicsMotionType::Dynamic;
  body_desc.shape.type = karma::physics::PhysicsShapeType::Box;
  body_desc.shape.half_extents = {1.0f, 0.25f, 2.0f};
  body_desc.mass = 1200.0f;
  body_desc.position = {0.0f, 2.0f, 0.0f};
  body_desc.allow_sleeping = false;
  karma::physics::RigidBody chassis = physics_world.createBody(body_desc);
  assert(chassis.isValid());

  karma::physics::PhysicsVehicleDesc desc;
  desc.controller = karma::physics::PhysicsVehicleControllerType::Wheeled;
  desc.wheels.resize(4);
  const std::array<glm::vec3, 4> positions{
      glm::vec3{-0.9f, -0.35f, 1.3f},
      glm::vec3{0.9f, -0.35f, 1.3f},
      glm::vec3{-0.9f, -0.35f, -1.3f},
      glm::vec3{0.9f, -0.35f, -1.3f},
  };
  for (size_t i = 0; i < desc.wheels.size(); ++i) {
    desc.wheels[i].position = positions[i];
    desc.wheels[i].suspension_force_point = positions[i];
    desc.wheels[i].radius = 0.35f;
    desc.wheels[i].width = 0.25f;
    desc.wheels[i].suspension_min_length = 0.2f;
    desc.wheels[i].suspension_max_length = 0.6f;
    desc.wheels[i].max_steer_angle = i < 2 ? 0.6f : 0.0f;
  }
  desc.differentials.push_back({
      .left_wheel = 2,
      .right_wheel = 3,
      .differential_ratio = 3.42f,
      .left_right_split = 0.5f,
      .limited_slip_ratio = 1.4f,
      .engine_torque_ratio = 1.0f,
  });
  desc.anti_roll_bars.push_back({0, 1, 1500.0f});
  desc.anti_roll_bars.push_back({2, 3, 1500.0f});

  karma::physics::Vehicle vehicle = physics_world.createVehicle(desc, chassis.nativeHandle());
  assert(vehicle.isValid());
  vehicle.setInput({.forward = 0.5f, .right = 0.2f});
  physics_world.update(1.0f / 60.0f);

  const karma::physics::PhysicsVehicleState state = vehicle.getState();
  assert(state.valid);
  assert(state.wheels.size() == 4);
  assert(state.handle == vehicle.nativeHandle());
#endif
}

void softBodyCreatesAndReportsState() {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  karma::physics::World physics_world;
  physics_world.setGravity(-9.8f);

  karma::physics::PhysicsSoftBodyDesc desc;
  desc.preset = karma::physics::PhysicsSoftBodyPreset::Cloth;
  desc.position = {0.0f, 3.0f, 0.0f};
  desc.grid_size_x = 4;
  desc.grid_size_y = 4;
  desc.grid_spacing = 0.25f;
  desc.pin_cloth_corners = true;
  desc.vertex_radius = 0.01f;
  desc.solver_iterations = 3;

  karma::physics::SoftBody soft_body = physics_world.createSoftBody(desc);
  assert(soft_body.isValid());
  soft_body.setPressure(0.25f);
  soft_body.setUpdatePosition(true);
  physics_world.update(1.0f / 60.0f);

  karma::physics::PhysicsSoftBodyState state = soft_body.getState();
  assert(state.valid);
  assert(!state.vertices.empty());
  assert(!state.indices.empty());
  assert(state.handle == soft_body.nativeHandle());
  soft_body.setVertexPosition(0, state.vertices.front().position, true);
#endif
}

void terrainColliderMarkerCreatesPhysicsHeightfield() {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  karma::ecs::World world;
  karma::physics::World physics_world;
  physics_world.setGravity(0.0f);
  karma::physics::PhysicsSystem physics_system(physics_world);
  karma::terrain::TerrainSystem terrain_system(nullptr);

  const karma::ecs::Entity terrain = world.createEntity();
  world.add(terrain, karma::components::TransformComponent{});
  world.add(terrain, karma::components::ColliderComponent{});
  world.add(terrain, karma::components::TerrainComponent{
                         .source = karma::components::TerrainSourceType::Procedural,
                         .tile_size = 16.0f,
                         .tile_resolution = 17u,
                         .height_scale = 2.0f,
                         .height_offset = 0.0f,
                     });

  terrain_system.syncTerrainColliders(world);
  assert(world.has<karma::components::ColliderComponent>(terrain));
  const auto& collider = world.get<karma::components::ColliderComponent>(terrain);
  assert(collider.type == karma::components::ColliderShapeType::HeightField);
  assert(std::get_if<karma::components::HeightFieldColliderShape>(&collider.shape) != nullptr);

  physics_system.update(world, 1.0f / 60.0f);

  karma::physics::PhysicsQueryHit hit{};
  karma::physics::PhysicsRaycastDesc ray{};
  ray.from = {8.0f, 20.0f, 8.0f};
  ray.to = {8.0f, -20.0f, 8.0f};
  assert(physics_world.castRay(ray, hit));
  assert(hit.body != 0u);
#endif
}

}  // namespace

int main() {
  componentRequirementsAreStrict();
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  assert(overlappingContactCount(true) > 0);
  assert(overlappingContactCount(false) == 0);
  queryApiFindsFilteredBodies();
  meshColliderGeometryProviderBuildsExternalMeshCollider();
  characterControllerStaysStableOnMeshColliderGround();
  multipleCharacterControllersMoveIndependently();
  sceneHierarchyPreservesPhysicsInterpolationSpan();
  rigidBodyRuntimeControlsWork();
  vehicleConstraintCreatesAndReportsState();
  softBodyCreatesAndReportsState();
  terrainColliderMarkerCreatesPhysicsHeightfield();
#endif
  return 0;
}
