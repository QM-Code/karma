#include "karma/scene/transform_hierarchy.h"

#include "karma/components/transform.h"
#include "karma/math/quat.h"

namespace karma::scene {

namespace {

math::Vec3 multiplyVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x * b.x, a.y * b.y, a.z * b.z};
}

math::Vec3 addVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

components::TransformComponent toWorldTransform(
    const components::LocalTransformComponent& local) {
  return components::TransformComponent{local.position, local.rotation, local.scale};
}

components::TransformComponent composeTransform(
    const components::TransformComponent& parent,
    const components::LocalTransformComponent& local) {
  components::TransformComponent world_transform{};
  const math::Vec3 scaled_local = multiplyVec3(local.position, parent.getScale());
  const math::Vec3 rotated_local = math::rotateVec(parent.getRotation(), scaled_local);
  world_transform.setPosition(addVec3(parent.getPosition(), rotated_local));
  world_transform.setRotation(math::mul(parent.getRotation(), local.rotation));
  world_transform.setScale(multiplyVec3(parent.getScale(), local.scale));
  return world_transform;
}

void updateNode(ecs::World& world,
                const Scene& scene,
                NodeId node_id,
                const components::TransformComponent* parent_transform) {
  if (!scene.isAlive(node_id)) {
    return;
  }

  const Node& node = scene.get(node_id);
  const components::TransformComponent* current_transform = parent_transform;
  components::TransformComponent composed{};
  if (node.entity.isValid() &&
      world.isAlive(node.entity) &&
      world.has<components::LocalTransformComponent>(node.entity) &&
      world.has<components::TransformComponent>(node.entity)) {
    const auto& local = world.get<components::LocalTransformComponent>(node.entity);
    composed = parent_transform ? composeTransform(*parent_transform, local) : toWorldTransform(local);
    world.get<components::TransformComponent>(node.entity) = composed;
    current_transform = &world.get<components::TransformComponent>(node.entity);
  } else if (node.entity.isValid() &&
             world.isAlive(node.entity) &&
             world.has<components::TransformComponent>(node.entity)) {
    current_transform = &world.get<components::TransformComponent>(node.entity);
  }

  for (const NodeId child : node.children) {
    updateNode(world, scene, child, current_transform);
  }
}

}  // namespace

void updateWorldTransforms(ecs::World& world, const Scene& scene) {
  for (const Node& node : scene.nodes()) {
    if (!scene.isAlive(node.id) || scene.isAlive(node.parent)) {
      continue;
    }
    updateNode(world, scene, node.id, nullptr);
  }
}

}  // namespace karma::scene
