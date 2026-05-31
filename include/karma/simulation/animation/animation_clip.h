#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "karma/core/math/types.h"

namespace karma::animation {

struct Vec3Keyframe {
  float time_seconds = 0.0f;
  math::Vec3 value{};
};

struct QuatKeyframe {
  float time_seconds = 0.0f;
  math::Quat value{};
};

struct AnimationChannel {
  uint32_t target_node_index = 0;
  std::vector<Vec3Keyframe> position_keys;
  std::vector<QuatKeyframe> rotation_keys;
  std::vector<Vec3Keyframe> scale_keys;
};

struct AnimationClip {
  std::string name;
  float duration_seconds = 0.0f;
  float ticks_per_second = 1.0f;
  std::vector<AnimationChannel> channels;
};

struct SampledTransform {
  std::optional<math::Vec3> position;
  std::optional<math::Quat> rotation;
  std::optional<math::Vec3> scale;
};

float normalizeAnimationTime(const AnimationClip& clip, float time_seconds, bool loop);
std::optional<math::Vec3> sampleVec3Keyframes(const std::vector<Vec3Keyframe>& keys,
                                              float time_seconds);
std::optional<math::Quat> sampleQuatKeyframes(const std::vector<QuatKeyframe>& keys,
                                              float time_seconds);
void sampleAnimationClip(
    const AnimationClip& clip,
    float time_seconds,
    bool loop,
    const std::function<void(uint32_t target_node_index, const SampledTransform& transform)>&
        on_sample);

}  // namespace karma::animation
