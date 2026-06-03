#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "karma/core/math/types.h"

#include <glm/glm.hpp>

namespace karma::animation {

constexpr uint32_t kInvalidAnimationIndex = std::numeric_limits<uint32_t>::max();

enum class InterpolationMode : uint8_t {
  Step,
  Linear,
  CubicSpline,
};

enum class AnimationTargetPath : uint8_t {
  Translation,
  Rotation,
  Scale,
  MorphWeights,
};

struct Vec3Keyframe {
  float time_seconds = 0.0f;
  math::Vec3 value{};
  math::Vec3 in_tangent{};
  math::Vec3 out_tangent{};
};

struct QuatKeyframe {
  float time_seconds = 0.0f;
  math::Quat value{};
  math::Quat in_tangent{};
  math::Quat out_tangent{};
};

struct MorphWeightKeyframe {
  float time_seconds = 0.0f;
  std::vector<float> values;
  std::vector<float> in_tangents;
  std::vector<float> out_tangents;
};

struct AnimationTrack {
  uint32_t target_node_index = kInvalidAnimationIndex;
  uint32_t target_skin_index = kInvalidAnimationIndex;
  uint32_t target_joint_index = kInvalidAnimationIndex;
  uint32_t target_morph_target_index = kInvalidAnimationIndex;
  AnimationTargetPath path = AnimationTargetPath::Translation;
  InterpolationMode interpolation = InterpolationMode::Linear;
};

struct AnimationChannel {
  uint32_t target_node_index = 0;
  uint32_t target_skin_index = kInvalidAnimationIndex;
  uint32_t target_joint_index = kInvalidAnimationIndex;
  InterpolationMode position_interpolation = InterpolationMode::Linear;
  InterpolationMode rotation_interpolation = InterpolationMode::Linear;
  InterpolationMode scale_interpolation = InterpolationMode::Linear;
  std::vector<Vec3Keyframe> position_keys;
  std::vector<QuatKeyframe> rotation_keys;
  std::vector<Vec3Keyframe> scale_keys;
};

struct MorphTargetTrack {
  uint32_t target_node_index = kInvalidAnimationIndex;
  uint32_t target_mesh_index = kInvalidAnimationIndex;
  InterpolationMode interpolation = InterpolationMode::Linear;
  std::vector<MorphWeightKeyframe> weight_keys;
};

struct AnimationEvent {
  std::string name;
  float time_seconds = 0.0f;
  std::string payload;
};

struct RootMotionTrack {
  uint32_t target_node_index = kInvalidAnimationIndex;
  InterpolationMode position_interpolation = InterpolationMode::Linear;
  InterpolationMode rotation_interpolation = InterpolationMode::Linear;
  std::vector<Vec3Keyframe> position_keys;
  std::vector<QuatKeyframe> rotation_keys;
};

struct Joint {
  std::string name;
  uint32_t parent_joint_index = kInvalidAnimationIndex;
  uint32_t node_index = kInvalidAnimationIndex;
  glm::mat4 inverse_bind_matrix{1.0f};
};

struct Skeleton {
  std::string name;
  std::vector<Joint> joints;
  std::vector<uint32_t> root_joint_indices;
};

struct Skin {
  std::string name;
  uint32_t skeleton_index = kInvalidAnimationIndex;
  std::vector<uint32_t> joint_node_indices;
  std::vector<glm::mat4> inverse_bind_matrices;
};

struct AnimationClip {
  std::string name;
  float duration_seconds = 0.0f;
  float ticks_per_second = 1.0f;
  uint32_t source_index = kInvalidAnimationIndex;
  std::vector<AnimationChannel> channels;
  std::vector<MorphTargetTrack> morph_target_tracks;
  std::vector<AnimationEvent> events;
  std::optional<RootMotionTrack> root_motion;
};

struct SampledTransform {
  std::optional<math::Vec3> position;
  std::optional<math::Quat> rotation;
  std::optional<math::Vec3> scale;
};

float normalizeAnimationTime(const AnimationClip& clip, float time_seconds, bool loop);
std::optional<math::Vec3> sampleVec3Keyframes(const std::vector<Vec3Keyframe>& keys,
                                              float time_seconds,
                                              InterpolationMode interpolation =
                                                  InterpolationMode::Linear);
std::optional<math::Quat> sampleQuatKeyframes(const std::vector<QuatKeyframe>& keys,
                                              float time_seconds,
                                              InterpolationMode interpolation =
                                                  InterpolationMode::Linear);
std::optional<std::vector<float>> sampleMorphWeightKeyframes(
    const std::vector<MorphWeightKeyframe>& keys,
    float time_seconds,
    InterpolationMode interpolation = InterpolationMode::Linear);
void sampleAnimationClip(
    const AnimationClip& clip,
    float time_seconds,
    bool loop,
    const std::function<void(uint32_t target_node_index, const SampledTransform& transform)>&
        on_sample);

}  // namespace karma::animation
