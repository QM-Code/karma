#include "karma/world.h"

#include "karma/math.h"
#include "karma/components.h"
#include "karma/math.h"

namespace karma::world {

namespace {

struct ComposedTransform {
  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
};

ComposedTransform composeTransform(const components::TransformComponent& parent,
                                   const components::TransformComponent& transform) {
  const math::Vec3 scaled_local = math::multiply(transform.localPosition(), parent.worldScale());
  const math::Vec3 rotated_local = math::rotateVec(parent.worldRotation(), scaled_local);
  return {
      .position = math::add(parent.worldPosition(), rotated_local),
      .rotation = math::mul(parent.worldRotation(), transform.localRotation()),
      .scale = math::multiply(parent.worldScale(), transform.localScale()),
  };
}

}  // namespace

void updateWorldTransforms(world::World& world, const Scene& scene) {
  auto update_node = [&](auto&& self,
                         NodeId node_id,
                         const components::TransformComponent* parent_transform,
                         bool parent_reset_history) -> void {
    if (!scene.isAlive(node_id)) {
      return;
    }

    const Node& node = scene.get(node_id);
    const components::TransformComponent* current_transform = parent_transform;
    bool reset_children_history = parent_reset_history;
    if (node.entity.isValid() && world.isAlive(node.entity) &&
        world.has<components::TransformComponent>(node.entity)) {
      auto& transform = world.get<components::TransformComponent>(node.entity);
      const bool reset_history =
          parent_reset_history ||
          !transform.hierarchy_initialized_ ||
          transform.position_dirty_ ||
          transform.rotation_dirty_;
      if (parent_transform != nullptr) {
        const ComposedTransform composed = composeTransform(*parent_transform, transform);
        transform.setWorldFromHierarchy(composed.position,
                                        composed.rotation,
                                        composed.scale,
                                        reset_history);
      } else {
        transform.setWorldFromHierarchy(transform.localPosition(),
                                        transform.localRotation(),
                                        transform.localScale(),
                                        reset_history);
      }
      current_transform = &transform;
      reset_children_history = reset_history;
    }

    for (const NodeId child : node.children) {
      self(self, child, current_transform, reset_children_history);
    }
  };

  for (const Node& node : scene.nodes()) {
    if (!scene.isAlive(node.id) || scene.isAlive(node.parent)) {
      continue;
    }
    update_node(update_node, node.id, nullptr, false);
  }
}

}  // namespace karma::world
