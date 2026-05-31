#include "karma/simulation/animation/animation_system.h"

#include "karma/world/components/animation_player.h"
#include "karma/world/components/transform.h"

namespace karma::animation {

void AnimationSystem::update(ecs::World& world, scene::Scene& scene, float dt) {
  (void)scene;

  const std::vector<ecs::Entity> players = world.view<components::AnimationPlayerComponent>();
  for (const ecs::Entity entity : players) {
    auto& player = world.get<components::AnimationPlayerComponent>(entity);
    if (player.clips.empty() || player.current_clip_index >= player.clips.size()) {
      continue;
    }

    if (player.playing) {
      player.time_seconds += dt * player.speed;
    }

    const AnimationClip& clip = player.clips[player.current_clip_index];
    if (player.playing) {
      player.time_seconds = normalizeAnimationTime(clip, player.time_seconds, player.loop);
    }

    sampleAnimationClip(
        clip,
        player.time_seconds,
        player.loop,
        [&](uint32_t target_node_index, const SampledTransform& sampled) {
          if (target_node_index >= player.node_entities_by_index.size()) {
            return;
          }
          const ecs::Entity target = player.node_entities_by_index[target_node_index];
          if (!world.isAlive(target) || !world.has<components::LocalTransformComponent>(target)) {
            return;
          }
          auto& local = world.get<components::LocalTransformComponent>(target);
          if (sampled.position) {
            local.position = *sampled.position;
          }
          if (sampled.rotation) {
            local.rotation = *sampled.rotation;
          }
          if (sampled.scale) {
            local.scale = *sampled.scale;
          }
        });
  }
}

}  // namespace karma::animation
