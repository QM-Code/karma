#include "karma/simulation/animation/animation_clip.h"

#include <algorithm>
#include <cmath>

#include <glm/gtx/quaternion.hpp>

namespace karma::animation {

namespace {

glm::quat toGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

math::Quat fromGlm(const glm::quat& q) {
  return {q.x, q.y, q.z, q.w};
}

math::Vec3 lerp(const math::Vec3& a, const math::Vec3& b, float t) {
  return {
      a.x + (b.x - a.x) * t,
      a.y + (b.y - a.y) * t,
      a.z + (b.z - a.z) * t,
  };
}

}  // namespace

float normalizeAnimationTime(const AnimationClip& clip, float time_seconds, bool loop) {
  if (clip.duration_seconds <= 0.0f) {
    return 0.0f;
  }
  if (!loop) {
    return std::clamp(time_seconds, 0.0f, clip.duration_seconds);
  }
  float wrapped = std::fmod(time_seconds, clip.duration_seconds);
  if (wrapped < 0.0f) {
    wrapped += clip.duration_seconds;
  }
  return wrapped;
}

std::optional<math::Vec3> sampleVec3Keyframes(const std::vector<Vec3Keyframe>& keys,
                                              float time_seconds) {
  if (keys.empty()) {
    return std::nullopt;
  }
  if (time_seconds <= keys.front().time_seconds || keys.size() == 1) {
    return keys.front().value;
  }
  if (time_seconds >= keys.back().time_seconds) {
    return keys.back().value;
  }

  const auto upper =
      std::upper_bound(keys.begin(),
                       keys.end(),
                       time_seconds,
                       [](float t, const Vec3Keyframe& key) { return t < key.time_seconds; });
  const auto lower = upper - 1;
  const float span = std::max(upper->time_seconds - lower->time_seconds, 0.000001f);
  const float t = std::clamp((time_seconds - lower->time_seconds) / span, 0.0f, 1.0f);
  return lerp(lower->value, upper->value, t);
}

std::optional<math::Quat> sampleQuatKeyframes(const std::vector<QuatKeyframe>& keys,
                                              float time_seconds) {
  if (keys.empty()) {
    return std::nullopt;
  }
  if (time_seconds <= keys.front().time_seconds || keys.size() == 1) {
    const glm::quat q = glm::normalize(toGlm(keys.front().value));
    return fromGlm(q);
  }
  if (time_seconds >= keys.back().time_seconds) {
    const glm::quat q = glm::normalize(toGlm(keys.back().value));
    return fromGlm(q);
  }

  const auto upper =
      std::upper_bound(keys.begin(),
                       keys.end(),
                       time_seconds,
                       [](float t, const QuatKeyframe& key) { return t < key.time_seconds; });
  const auto lower = upper - 1;
  const float span = std::max(upper->time_seconds - lower->time_seconds, 0.000001f);
  const float t = std::clamp((time_seconds - lower->time_seconds) / span, 0.0f, 1.0f);
  const glm::quat q =
      glm::normalize(glm::slerp(toGlm(lower->value), toGlm(upper->value), t));
  return fromGlm(q);
}

void sampleAnimationClip(
    const AnimationClip& clip,
    float time_seconds,
    bool loop,
    const std::function<void(uint32_t target_node_index, const SampledTransform& transform)>&
        on_sample) {
  if (!on_sample) {
    return;
  }

  const float sample_time = normalizeAnimationTime(clip, time_seconds, loop);
  for (const AnimationChannel& channel : clip.channels) {
    SampledTransform transform{};
    transform.position = sampleVec3Keyframes(channel.position_keys, sample_time);
    transform.rotation = sampleQuatKeyframes(channel.rotation_keys, sample_time);
    transform.scale = sampleVec3Keyframes(channel.scale_keys, sample_time);
    if (transform.position || transform.rotation || transform.scale) {
      on_sample(channel.target_node_index, transform);
    }
  }
}

}  // namespace karma::animation
