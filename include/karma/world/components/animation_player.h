#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "karma/simulation/animation/animation_clip.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"

namespace karma::components {

/// \ingroup karma_components
/// Simple clip player for imported node animation.
///
/// `AnimationSystem` samples the current clip and writes local transforms for
/// `node_entities_by_index`. Use `AnimatorComponent` when a state machine,
/// blending, events, or root motion are needed.
struct AnimationPlayerComponent : ecs::ComponentTag {
  std::vector<animation::AnimationClip> clips;
  std::vector<ecs::Entity> node_entities_by_index;
  size_t current_clip_index = 0;
  float time_seconds = 0.0f;
  float speed = 1.0f;
  bool loop = true;
  bool playing = false;
};

/// Switches to a clip by index.
bool setAnimationClip(AnimationPlayerComponent& player, size_t clip_index, bool reset_time = true);
/// Switches to the first clip matching `clip_name`.
bool setAnimationClip(AnimationPlayerComponent& player, std::string_view clip_name,
                      bool reset_time = true);
/// Starts or resumes clip playback.
void playAnimation(AnimationPlayerComponent& player);
/// Pauses clip playback without resetting time.
void pauseAnimation(AnimationPlayerComponent& player);
/// Stops playback and rewinds to the start of the current clip.
void stopAnimation(AnimationPlayerComponent& player);

}  // namespace karma::components
