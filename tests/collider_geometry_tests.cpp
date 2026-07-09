#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

#include "karma/components.h"
#include "karma/math.h"
#include "karma/physics.h"
#include "karma/world.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool near(float actual, float expected, float epsilon = 1.0e-4f) {
  return std::abs(actual - expected) <= epsilon;
}

void requireVec3(const karma::math::Vec3& actual,
                 const karma::math::Vec3& expected) {
  assert(near(actual.x, expected.x));
  assert(near(actual.y, expected.y));
  assert(near(actual.z, expected.z));
}

karma::world::Entity addCollider(
    karma::world::World& world,
    const karma::components::TransformComponent& transform,
    karma::components::ColliderComponent collider) {
  const karma::world::Entity entity = world.createEntity();
  world.add(entity, transform);
  world.add(entity, std::move(collider));
  return entity;
}

void testRobustQuaternionMath() {
  const karma::math::Quat rotation =
      karma::math::fromYawPitch(kPi * 0.5f, 0.0f);
  const karma::math::Quat scaled{
      rotation.x * 3.0f,
      rotation.y * 3.0f,
      rotation.z * 3.0f,
      rotation.w * 3.0f,
  };

  requireVec3(karma::math::rotateVec(scaled, {1.0f, 0.0f, 0.0f}),
              {0.0f, 0.0f, -1.0f});
  const karma::math::Quat identity =
      karma::math::mul(scaled, karma::math::inverse(scaled));
  assert(near(identity.x, 0.0f));
  assert(near(identity.y, 0.0f));
  assert(near(identity.z, 0.0f));
  assert(near(identity.w, 1.0f));

  const karma::math::Quat halfway = karma::math::slerp(
      {0.0f, 0.0f, 0.0f, 2.0f}, scaled, 0.5f);
  assert(near(karma::math::length(halfway), 1.0f));
  assert(!karma::math::isFinite(karma::math::Vec3{
      std::numeric_limits<float>::infinity(), 0.0f, 0.0f}));
  requireVec3(karma::math::normalize(karma::math::Vec3{
                  std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f}),
              {});
  requireVec3(karma::math::normalize(karma::math::Vec3{
                  std::numeric_limits<float>::max(), 0.0f, 0.0f}),
              {1.0f, 0.0f, 0.0f});
  requireVec3(karma::math::normalize(karma::math::Vec3{
                  std::numeric_limits<float>::denorm_min(), 0.0f, 0.0f}),
              {});
  const karma::math::Quat huge_quaternion = karma::math::normalize({
      0.0f,
      std::numeric_limits<float>::max(),
      0.0f,
      std::numeric_limits<float>::max(),
  });
  assert(near(karma::math::length(huge_quaternion), 1.0f));
}

void testCapsuleUsesTotalHeight() {
  karma::world::World world;
  const auto capsule = addCollider(
      world,
      karma::components::TransformComponent{},
      karma::components::ColliderComponent::capsule({
          .radius = 0.5f,
          .height = 2.0f,
      }));

  assert(karma::world::queries::containsPoint(world, capsule,
                                               {0.0f, 0.99f, 0.0f}));
  assert(!karma::world::queries::containsPoint(world, capsule,
                                                {0.0f, 1.01f, 0.0f}));

  const auto separated_sphere = addCollider(
      world,
      karma::components::TransformComponent({0.0f, 1.25f, 0.0f}),
      karma::components::ColliderComponent::sphere({.radius = 0.2f}));
  assert(!karma::world::queries::overlaps(world, capsule, separated_sphere));

  world.get<karma::components::TransformComponent>(separated_sphere)
      .setPosition({0.0f, 1.15f, 0.0f});
  assert(karma::world::queries::overlaps(world, capsule, separated_sphere));

  const auto short_capsule = addCollider(
      world,
      karma::components::TransformComponent({3.0f, 0.0f, 0.0f}),
      karma::components::ColliderComponent::capsule({
          .radius = 1.0f,
          .height = 0.5f,
      }));
  assert(karma::world::queries::containsPoint(world, short_capsule,
                                               {3.0f, 0.99f, 0.0f}));
  assert(!karma::world::queries::containsPoint(world, short_capsule,
                                                {3.0f, 1.01f, 0.0f}));
}

void testScaledRotationsAndNegativeRadii() {
  karma::world::World world;
  const karma::math::Quat rotation =
      karma::math::fromYawPitch(kPi * 0.5f, 0.0f);
  const karma::math::Quat scaled_rotation{
      rotation.x * 4.0f,
      rotation.y * 4.0f,
      rotation.z * 4.0f,
      rotation.w * 4.0f,
  };
  const auto box = addCollider(
      world,
      karma::components::TransformComponent({}, scaled_rotation),
      karma::components::ColliderComponent::box({
          .half_extents = {2.0f, 0.5f, 0.5f},
      }));
  assert(karma::world::queries::containsPoint(world, box,
                                               {0.0f, 0.0f, -1.5f}));
  assert(!karma::world::queries::containsPoint(world, box,
                                                {1.0f, 0.0f, 0.0f}));
  assert(!karma::world::queries::containsPoint(world, box,
                                                {0.50002f, 0.0f, 0.0f}));

  const auto negative_radius = addCollider(
      world,
      karma::components::TransformComponent({5.0f, 0.0f, 0.0f}),
      karma::components::ColliderComponent::sphere({.radius = -1.0f}));
  const auto other = addCollider(
      world,
      karma::components::TransformComponent({6.1f, 0.0f, 0.0f}),
      karma::components::ColliderComponent::sphere({.radius = 0.25f}));
  assert(karma::world::queries::overlaps(world, negative_radius, other));
}

void testFiltersSelfOverlapAndInvalidInputs() {
  karma::world::World world;
  const auto sphere = addCollider(
      world,
      karma::components::TransformComponent{},
      karma::components::ColliderComponent::sphere());

  assert(karma::world::queries::findContainingCollider(world, {}).has_value());
  assert(!karma::world::queries::findContainingCollider(
      world,
      {},
      {.collision_layer_mask = 0u}).has_value());
  assert(karma::world::queries::findContainingColliders(
      world,
      {},
      {.collision_layer_mask = 0u}).empty());

  const auto self = karma::world::queries::findOverlappingCollider(
      world,
      sphere,
      {.skip_self = false});
  assert(karma::world::queries::overlaps(world, sphere, sphere));
  assert(self.has_value());
  assert(self->entity == sphere);
  assert(!karma::world::queries::findOverlappingCollider(world, sphere).has_value());
  assert(!karma::world::queries::findOverlappingCollider(
      world,
      sphere,
      {.collision_layer_mask = 0u, .skip_self = false}).has_value());

  const float nan = std::numeric_limits<float>::quiet_NaN();
  assert(!karma::world::queries::containsPoint(world, sphere, {nan, 0.0f, 0.0f}));
  const auto invalid = addCollider(
      world,
      karma::components::TransformComponent({nan, 0.0f, 0.0f}),
      karma::components::ColliderComponent::box());
  assert(!karma::world::queries::containsPoint(world, invalid, {}));
  assert(!karma::world::queries::overlaps(world, invalid, sphere));
}

void testPrimitiveMeshInputHardening() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const karma::world::MeshData plane = karma::world::createPlaneMesh(nan, 2.0f);
  for (const glm::vec3& vertex : plane.vertices) {
    assert(std::isfinite(vertex.x));
    assert(std::isfinite(vertex.y));
    assert(std::isfinite(vertex.z));
  }

  const karma::world::MeshData box = karma::world::createBoxMesh(
      {std::numeric_limits<float>::infinity(), -2.0f, 3.0f});
  for (const glm::vec3& vertex : box.vertices) {
    assert(std::isfinite(vertex.x));
    assert(std::isfinite(vertex.y));
    assert(std::isfinite(vertex.z));
  }

  const karma::world::MeshData huge_capsule = karma::world::createCapsuleMesh({
      .radius = std::numeric_limits<float>::max(),
      .cylinder_height = std::numeric_limits<float>::max(),
      .segments = 3u,
      .hemisphere_rings = 2u,
  });
  for (const glm::vec3& vertex : huge_capsule.vertices) {
    assert(std::isfinite(vertex.x));
    assert(std::isfinite(vertex.y));
    assert(std::isfinite(vertex.z));
  }

  bool threw = false;
  try {
    (void)karma::world::createSphereMesh({
        .segments = std::numeric_limits<uint32_t>::max(),
        .rings = 2u,
    });
  } catch (const std::length_error&) {
    threw = true;
  }
  assert(threw);

  threw = false;
  try {
    (void)karma::world::createCapsuleMesh({
        .cylinder_height = 0.0f,
        .segments = 3u,
        .hemisphere_rings = std::numeric_limits<uint32_t>::max(),
    });
  } catch (const std::length_error&) {
    threw = true;
  }
  assert(threw);
}

void testCollisionEventDeactivationTransitions() {
  karma::world::World world;
  const auto listener_entity = addCollider(
      world,
      karma::components::TransformComponent{},
      karma::components::ColliderComponent::sphere());
  world.add(listener_entity, karma::components::CollisionListenerComponent{
                                 .emit_stay = true,
                             });
  const auto other = addCollider(
      world,
      karma::components::TransformComponent{},
      karma::components::ColliderComponent::sphere());

  karma::physics::CollisionEventSystem system;
  system.update(world, 0.0f);
  auto& events =
      world.get<karma::components::CollisionEventsComponent>(listener_entity);
  assert(events.entered.size() == 1u);
  assert(events.entered.front().other == other);
  assert(events.active.size() == 1u);

  system.update(world, 0.0f);
  assert(events.entered.empty());
  assert(events.stayed.size() == 1u);
  assert(events.active.size() == 1u);

  auto& listener =
      world.get<karma::components::CollisionListenerComponent>(listener_entity);
  listener.enabled = false;
  system.update(world, 0.0f);
  assert(events.active.empty());
  assert(events.exited.size() == 1u);
  assert(events.exited.front().other == other);

  system.update(world, 0.0f);
  assert(events.exited.empty());
  listener.enabled = true;
  system.update(world, 0.0f);
  assert(events.entered.size() == 1u);

  world.remove<karma::components::ColliderComponent>(listener_entity);
  system.update(world, 0.0f);
  assert(events.active.empty());
  assert(events.exited.size() == 1u);
  assert(events.exited.front().other == other);
}

}  // namespace

int main() {
  testRobustQuaternionMath();
  testCapsuleUsesTotalHeight();
  testScaledRotationsAndNegativeRadii();
  testFiltersSelfOverlapAndInvalidInputs();
  testPrimitiveMeshInputHardening();
  testCollisionEventDeactivationTransitions();
  return 0;
}
