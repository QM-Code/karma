#pragma once

#include <string>

#include "karma/world/components/transform.h"
#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Audio emitter bound to a clip key loaded by `AudioSystem`.
///
/// `play_on_start` is consumed once by the audio system. Calling `play()`
/// records a transient request that is consumed on the next audio update.
class AudioSourceComponent : public ecs::ComponentTag {
 public:
  std::string clip_key;
  float gain = 1.0f;
  float pitch = 1.0f;
  float min_distance = 1.0f;
  float max_distance = 20.0f;
  bool looping = false;
  bool play_on_start = false;
  bool spatialized = true;
  int max_instances = 5;

  /// Requests one playback instance on the next audio-system update.
  void play() { play_requested_ = true; }

  /// Returns and clears a pending playback request.
  bool consumePlayRequest() {
    if (!play_requested_) {
      return false;
    }
    play_requested_ = false;
    return true;
  }

 private:
  bool play_requested_ = false;
};

}  // namespace karma::components
