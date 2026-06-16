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

/// \ingroup karma_animation
/// Sentinel index used for absent animation nodes, clips, skins, and joints.
constexpr uint32_t kInvalidAnimationIndex = std::numeric_limits<uint32_t>::max();

/// \ingroup karma_animation
/// glTF-compatible keyframe interpolation mode.
enum class InterpolationMode : uint8_t {
  Step,
  Linear,
  CubicSpline,
};

/// \ingroup karma_animation
/// Animation target channel path.
enum class AnimationTargetPath : uint8_t {
  Translation,
  Rotation,
  Scale,
  MorphWeights,
};

/// \ingroup karma_animation
/// Vec3 animation keyframe, including optional cubic-spline tangents.
struct Vec3Keyframe {
  float time_seconds = 0.0f;
  math::Vec3 value{};
  math::Vec3 in_tangent{};
  math::Vec3 out_tangent{};
};

/// \ingroup karma_animation
/// Quaternion animation keyframe, including optional cubic-spline tangents.
struct QuatKeyframe {
  float time_seconds = 0.0f;
  math::Quat value{};
  math::Quat in_tangent{};
  math::Quat out_tangent{};
};

/// \ingroup karma_animation
/// Morph target weight keyframe.
struct MorphWeightKeyframe {
  float time_seconds = 0.0f;
  std::vector<float> values;
  std::vector<float> in_tangents;
  std::vector<float> out_tangents;
};

/// \ingroup karma_animation
/// Generic target description for imported animation data.
struct AnimationTrack {
  uint32_t target_node_index = kInvalidAnimationIndex;
  uint32_t target_skin_index = kInvalidAnimationIndex;
  uint32_t target_joint_index = kInvalidAnimationIndex;
  uint32_t target_morph_target_index = kInvalidAnimationIndex;
  AnimationTargetPath path = AnimationTargetPath::Translation;
  InterpolationMode interpolation = InterpolationMode::Linear;
};

/// \ingroup karma_animation
/// Node transform animation channel.
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

/// \ingroup karma_animation
/// Morph weight animation track.
struct MorphTargetTrack {
  uint32_t target_node_index = kInvalidAnimationIndex;
  uint32_t target_mesh_index = kInvalidAnimationIndex;
  InterpolationMode interpolation = InterpolationMode::Linear;
  std::vector<MorphWeightKeyframe> weight_keys;
};

/// \ingroup karma_animation
/// Authored event emitted while sampling a clip.
struct AnimationEvent {
  std::string name;
  float time_seconds = 0.0f;
  std::string payload;
};

/// \ingroup karma_animation
/// Optional root-motion track extracted from animation data.
struct RootMotionTrack {
  uint32_t target_node_index = kInvalidAnimationIndex;
  InterpolationMode position_interpolation = InterpolationMode::Linear;
  InterpolationMode rotation_interpolation = InterpolationMode::Linear;
  std::vector<Vec3Keyframe> position_keys;
  std::vector<QuatKeyframe> rotation_keys;
};

/// \ingroup karma_animation
/// One skeleton joint mapped back to an imported scene node.
struct Joint {
  std::string name;
  uint32_t parent_joint_index = kInvalidAnimationIndex;
  uint32_t node_index = kInvalidAnimationIndex;
  glm::mat4 inverse_bind_matrix{1.0f};
};

/// \ingroup karma_animation
/// Imported skeleton topology.
struct Skeleton {
  std::string name;
  std::vector<Joint> joints;
  std::vector<uint32_t> root_joint_indices;
};

/// \ingroup karma_animation
/// Imported skin data used by skinned meshes.
struct Skin {
  std::string name;
  uint32_t skeleton_index = kInvalidAnimationIndex;
  std::vector<uint32_t> joint_node_indices;
  std::vector<glm::mat4> inverse_bind_matrices;
};

/// \ingroup karma_animation
/// Imported animation clip.
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

/// \ingroup karma_animation
/// Optional transform sample for one target node.
struct SampledTransform {
  std::optional<math::Vec3> position;
  std::optional<math::Quat> rotation;
  std::optional<math::Vec3> scale;
};

/// Normalizes `time_seconds` into clip duration, respecting loop mode.
float normalizeAnimationTime(const AnimationClip& clip, float time_seconds, bool loop);
/// Samples Vec3 keyframes at a time.
std::optional<math::Vec3> sampleVec3Keyframes(const std::vector<Vec3Keyframe>& keys,
                                              float time_seconds,
                                              InterpolationMode interpolation =
                                                  InterpolationMode::Linear);
/// Samples quaternion keyframes at a time.
std::optional<math::Quat> sampleQuatKeyframes(const std::vector<QuatKeyframe>& keys,
                                              float time_seconds,
                                              InterpolationMode interpolation =
                                                  InterpolationMode::Linear);
/// Samples morph target weights at a time.
std::optional<std::vector<float>> sampleMorphWeightKeyframes(
    const std::vector<MorphWeightKeyframe>& keys,
    float time_seconds,
    InterpolationMode interpolation = InterpolationMode::Linear);
/// Samples a clip and invokes `on_sample` for each transform target node.
void sampleAnimationClip(
    const AnimationClip& clip,
    float time_seconds,
    bool loop,
    const std::function<void(uint32_t target_node_index, const SampledTransform& transform)>&
        on_sample);
/// Samples a clip and invokes callbacks for transform and morph-weight targets.
///
/// `on_sample` receives node transform channels. `on_morph_weights` receives
/// glTF `weights` channels keyed by the target node index.
void sampleAnimationClip(
    const AnimationClip& clip,
    float time_seconds,
    bool loop,
    const std::function<void(uint32_t target_node_index, const SampledTransform& transform)>&
        on_sample,
    const std::function<void(uint32_t target_node_index, const std::vector<float>& weights)>&
        on_morph_weights);

}  // namespace karma::animation
