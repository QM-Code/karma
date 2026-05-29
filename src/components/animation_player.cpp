#include "karma/components/animation_player.h"

#include <algorithm>

namespace karma::components {

bool setAnimationClip(AnimationPlayerComponent& player, size_t clip_index, bool reset_time) {
  if (clip_index >= player.clips.size()) {
    return false;
  }
  player.current_clip_index = clip_index;
  if (reset_time) {
    player.time_seconds = 0.0f;
  }
  return true;
}

bool setAnimationClip(AnimationPlayerComponent& player, std::string_view clip_name,
                      bool reset_time) {
  const auto it = std::find_if(player.clips.begin(),
                               player.clips.end(),
                               [&](const animation::AnimationClip& clip) {
                                 return clip.name == clip_name;
                               });
  if (it == player.clips.end()) {
    return false;
  }
  return setAnimationClip(player,
                          static_cast<size_t>(std::distance(player.clips.begin(), it)),
                          reset_time);
}

void playAnimation(AnimationPlayerComponent& player) {
  player.playing = true;
}

void pauseAnimation(AnimationPlayerComponent& player) {
  player.playing = false;
}

void stopAnimation(AnimationPlayerComponent& player) {
  player.playing = false;
  player.time_seconds = 0.0f;
}

}  // namespace karma::components
