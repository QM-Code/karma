#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "karma/simulation/animation/animation_clip.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"

namespace karma::components {

struct AnimationPlayerComponent : ecs::ComponentTag {
  std::vector<animation::AnimationClip> clips;
  std::vector<ecs::Entity> node_entities_by_index;
  size_t current_clip_index = 0;
  float time_seconds = 0.0f;
  float speed = 1.0f;
  bool loop = true;
  bool playing = false;
};

bool setAnimationClip(AnimationPlayerComponent& player, size_t clip_index, bool reset_time = true);
bool setAnimationClip(AnimationPlayerComponent& player, std::string_view clip_name,
                      bool reset_time = true);
void playAnimation(AnimationPlayerComponent& player);
void pauseAnimation(AnimationPlayerComponent& player);
void stopAnimation(AnimationPlayerComponent& player);

}  // namespace karma::components
