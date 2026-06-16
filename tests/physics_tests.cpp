#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

#include "karma/simulation/physics/physics_system.h"
#include "karma/simulation/physics/physics_world.hpp"
#include "karma/world/components/collider.h"
#include "karma/world/components/contact_events.h"
#include "karma/world/components/physics_collision_filter.h"
#include "karma/world/components/rigidbody.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"

namespace {

constexpr uint32_t kLayerA = 1u << 0u;
constexpr uint32_t kLayerB = 1u << 1u;
constexpr uint32_t kLayerC = 1u << 2u;

std::size_t overlappingContactCount(bool allow_contact) {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  karma::ecs::World world;
  karma::physics::World physics_world;
  physics_world.setGravity(0.0f);
  karma::physics::PhysicsSystem physics_system(physics_world);

  const karma::ecs::Entity dynamic_body = world.createEntity();
  world.add(dynamic_body, karma::components::TransformComponent{});
  world.add(dynamic_body, karma::components::BoxColliderComponent{
                              .half_extents = {1.0f, 1.0f, 1.0f},
                          });
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
  world.add(static_body, karma::components::BoxColliderComponent{
                             .half_extents = {1.0f, 1.0f, 1.0f},
                         });
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

}  // namespace

int main() {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
  assert(overlappingContactCount(true) > 0);
  assert(overlappingContactCount(false) == 0);
  queryApiFindsFilteredBodies();
  rigidBodyRuntimeControlsWork();
#endif
  return 0;
}
