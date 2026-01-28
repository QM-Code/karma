#pragma once

#include <string>

#include "karma/components/transform.h"
#include "karma/ecs/component.h"

namespace karma::components {

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

  void play() { play_requested_ = true; }

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
