#include "karma/prefabs/effect_prefab_system.h"

#include <algorithm>
#include <vector>

#include "karma/components/effect_prefab.h"
#include "karma/math/quat.h"

namespace karma::prefabs {

namespace {

math::Vec3 multiplyVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x * b.x, a.y * b.y, a.z * b.z};
}

math::Vec3 addVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

components::TransformComponent composeTransform(const components::TransformComponent& root,
                                                const components::TransformComponent& local) {
  components::TransformComponent world_transform{};
  const math::Vec3 scaled_local = multiplyVec3(local.getPosition(), root.getScale());
  const math::Vec3 rotated_local = math::rotateVec(root.getRotation(), scaled_local);
  world_transform.setPosition(addVec3(root.getPosition(), rotated_local));
  world_transform.setRotation(math::mul(root.getRotation(), local.getRotation()));
  world_transform.setScale(multiplyVec3(root.getScale(), local.getScale()));
  return world_transform;
}

}  // namespace

void EffectPrefabSystem::update(ecs::World& world, float dt, float interpolation_alpha) {
  (void)dt;
  (void)interpolation_alpha;

  const std::vector<ecs::Entity> members =
      world.view<components::EffectPrefabMemberComponent, components::TransformComponent>();
  std::vector<ecs::Entity> stale_members;
  stale_members.reserve(members.size());

  for (const ecs::Entity member_entity : members) {
    auto& member = world.get<components::EffectPrefabMemberComponent>(member_entity);
    if (!world.isAlive(member.root) || !world.has<components::TransformComponent>(member.root)) {
      stale_members.push_back(member_entity);
      continue;
    }

    const auto& root_transform = world.get<components::TransformComponent>(member.root);
    world.get<components::TransformComponent>(member_entity) =
        composeTransform(root_transform, member.local_transform);
  }

  for (const ecs::Entity stale : stale_members) {
    world.destroyEntity(stale);
  }

  const std::vector<ecs::Entity> roots = world.view<components::EffectPrefabInstanceComponent>();
  for (const ecs::Entity root : roots) {
    auto& instance = world.get<components::EffectPrefabInstanceComponent>(root);
    instance.members.erase(
        std::remove_if(instance.members.begin(),
                       instance.members.end(),
                       [&](const ecs::Entity member) { return !world.isAlive(member); }),
        instance.members.end());
  }
}

}  // namespace karma::prefabs
