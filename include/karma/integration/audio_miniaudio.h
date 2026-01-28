#pragma once

#include <string>

#include "karma/components/audio_source.h"
#include "karma/components/transform.h"

namespace karma::integration {

// Pseudocode sketch for a miniaudio-backed audio system.
class MiniaudioBackend {
 public:
  void initialize() {
    // Initialize miniaudio engine/device.
  }

  void setListener(const components::TransformComponent& listener) {
    // Update listener position and orientation.
  }

  void playClip(const std::string& clip_key, const components::TransformComponent& transform, float gain) {
    // Resolve clip handle from asset registry and play at transform.position.
  }
};

}  // namespace karma::integration
