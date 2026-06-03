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

math::Vec3 scaleVec3(const math::Vec3& v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

math::Vec3 cubicHermite(const math::Vec3& p0,
                        const math::Vec3& out_tangent0,
                        const math::Vec3& p1,
                        const math::Vec3& in_tangent1,
                        float t,
                        float span) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
  const float h10 = t3 - 2.0f * t2 + t;
  const float h01 = -2.0f * t3 + 3.0f * t2;
  const float h11 = t3 - t2;
  const math::Vec3 m0 = scaleVec3(out_tangent0, span);
  const math::Vec3 m1 = scaleVec3(in_tangent1, span);
  return {
      h00 * p0.x + h10 * m0.x + h01 * p1.x + h11 * m1.x,
      h00 * p0.y + h10 * m0.y + h01 * p1.y + h11 * m1.y,
      h00 * p0.z + h10 * m0.z + h01 * p1.z + h11 * m1.z,
  };
}

math::Quat cubicHermite(const math::Quat& p0,
                        const math::Quat& out_tangent0,
                        const math::Quat& p1,
                        const math::Quat& in_tangent1,
                        float t,
                        float span) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
  const float h10 = t3 - 2.0f * t2 + t;
  const float h01 = -2.0f * t3 + 3.0f * t2;
  const float h11 = t3 - t2;
  glm::quat q{
      h00 * p0.w + h10 * out_tangent0.w * span + h01 * p1.w + h11 * in_tangent1.w * span,
      h00 * p0.x + h10 * out_tangent0.x * span + h01 * p1.x + h11 * in_tangent1.x * span,
      h00 * p0.y + h10 * out_tangent0.y * span + h01 * p1.y + h11 * in_tangent1.y * span,
      h00 * p0.z + h10 * out_tangent0.z * span + h01 * p1.z + h11 * in_tangent1.z * span,
  };
  q = glm::normalize(q);
  return fromGlm(q);
}

float cubicHermite(float p0, float out_tangent0, float p1, float in_tangent1, float t, float span) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
  const float h10 = t3 - 2.0f * t2 + t;
  const float h01 = -2.0f * t3 + 3.0f * t2;
  const float h11 = t3 - t2;
  return h00 * p0 + h10 * out_tangent0 * span + h01 * p1 + h11 * in_tangent1 * span;
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
                                              float time_seconds,
                                              InterpolationMode interpolation) {
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
  if (interpolation == InterpolationMode::Step) {
    return lower->value;
  }
  if (interpolation == InterpolationMode::CubicSpline) {
    return cubicHermite(lower->value, lower->out_tangent, upper->value, upper->in_tangent, t, span);
  }
  return lerp(lower->value, upper->value, t);
}

std::optional<math::Quat> sampleQuatKeyframes(const std::vector<QuatKeyframe>& keys,
                                              float time_seconds,
                                              InterpolationMode interpolation) {
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
  if (interpolation == InterpolationMode::Step) {
    const glm::quat q = glm::normalize(toGlm(lower->value));
    return fromGlm(q);
  }
  if (interpolation == InterpolationMode::CubicSpline) {
    return cubicHermite(lower->value, lower->out_tangent, upper->value, upper->in_tangent, t, span);
  }
  const glm::quat q =
      glm::normalize(glm::slerp(toGlm(lower->value), toGlm(upper->value), t));
  return fromGlm(q);
}

std::optional<std::vector<float>> sampleMorphWeightKeyframes(
    const std::vector<MorphWeightKeyframe>& keys,
    float time_seconds,
    InterpolationMode interpolation) {
  if (keys.empty()) {
    return std::nullopt;
  }
  if (time_seconds <= keys.front().time_seconds || keys.size() == 1) {
    return keys.front().values;
  }
  if (time_seconds >= keys.back().time_seconds) {
    return keys.back().values;
  }

  const auto upper =
      std::upper_bound(keys.begin(),
                       keys.end(),
                       time_seconds,
                       [](float t, const MorphWeightKeyframe& key) {
                         return t < key.time_seconds;
                       });
  const auto lower = upper - 1;
  if (interpolation == InterpolationMode::Step) {
    return lower->values;
  }

  const float span = std::max(upper->time_seconds - lower->time_seconds, 0.000001f);
  const float t = std::clamp((time_seconds - lower->time_seconds) / span, 0.0f, 1.0f);
  const size_t count = std::min(lower->values.size(), upper->values.size());
  std::vector<float> out(count, 0.0f);
  for (size_t i = 0; i < count; ++i) {
    if (interpolation == InterpolationMode::CubicSpline &&
        i < lower->out_tangents.size() &&
        i < upper->in_tangents.size()) {
      out[i] = cubicHermite(lower->values[i],
                            lower->out_tangents[i],
                            upper->values[i],
                            upper->in_tangents[i],
                            t,
                            span);
    } else {
      out[i] = lower->values[i] + (upper->values[i] - lower->values[i]) * t;
    }
  }
  return out;
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
    transform.position = sampleVec3Keyframes(channel.position_keys,
                                             sample_time,
                                             channel.position_interpolation);
    transform.rotation = sampleQuatKeyframes(channel.rotation_keys,
                                             sample_time,
                                             channel.rotation_interpolation);
    transform.scale = sampleVec3Keyframes(channel.scale_keys,
                                          sample_time,
                                          channel.scale_interpolation);
    if (transform.position || transform.rotation || transform.scale) {
      on_sample(channel.target_node_index, transform);
    }
  }
}

}  // namespace karma::animation
