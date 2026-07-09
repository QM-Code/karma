#pragma once

#include "karma/audio.h"

#include <algorithm>
#include <cmath>

#include <glm/gtx/quaternion.hpp>

namespace karma::audio::backend {

struct ListenerState {
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct StereoGains {
  float left = 0.0f;
  float right = 0.0f;
};

inline StereoGains stereoGains(const AudioPlaybackOptions& options,
                               const ListenerState& listener) {
  if (!options.valid()) {
    return {};
  }
  if (!options.spatialized) {
    return {options.gain, options.gain};
  }

  const glm::vec3 offset = options.position - listener.position;
  const float distance = std::hypot(offset.x, offset.y, offset.z);
  float attenuation = 0.0f;
  if (distance <= options.min_distance) {
    attenuation = 1.0f;
  } else if (distance < options.max_distance &&
             options.max_distance > options.min_distance) {
    attenuation = 1.0f -
                  (distance - options.min_distance) /
                      (options.max_distance - options.min_distance);
  }

  float pan = 0.0f;
  if (distance > 1.0e-6f && std::isfinite(distance)) {
    const glm::vec3 right = listener.rotation * glm::vec3{1.0f, 0.0f, 0.0f};
    const float right_length = std::hypot(right.x, right.y, right.z);
    if (right_length > 1.0e-6f && std::isfinite(right_length)) {
      pan = std::clamp(glm::dot(offset / distance, right / right_length),
                       -1.0f,
                       1.0f);
    }
  }

  const float base_gain = options.gain * attenuation;
  return {
      base_gain * (pan > 0.0f ? 1.0f - pan : 1.0f),
      base_gain * (pan < 0.0f ? 1.0f + pan : 1.0f),
  };
}

}  // namespace karma::audio::backend
