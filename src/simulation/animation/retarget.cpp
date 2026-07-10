#include "karma/world.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/math.h"

namespace karma::world {

namespace {

math::Quat toQuat(const glm::quat& q) {
  return {q.x, q.y, q.z, q.w};
}

math::Quat correctionRotation(const glm::mat4& matrix) {
  return math::normalize(toQuat(glm::quat_cast(glm::mat3(matrix))));
}

uint32_t jointForNode(const Skeleton& skeleton, uint32_t node_index) {
  for (uint32_t joint_index = 0; joint_index < skeleton.joints.size(); ++joint_index) {
    if (skeleton.joints[joint_index].node_index == node_index) {
      return joint_index;
    }
  }
  return kInvalidAnimationIndex;
}

uint32_t jointForChannel(const Skeleton& skeleton, const AnimationChannel& channel) {
  if (channel.target_node_index != kInvalidAnimationIndex) {
    return jointForNode(skeleton, channel.target_node_index);
  }
  if (channel.target_joint_index < skeleton.joints.size()) {
    return channel.target_joint_index;
  }
  return kInvalidAnimationIndex;
}

const SkeletonMapEntry* entryForSourceJoint(const SkeletonMap& map,
                                            uint32_t source_joint_index) {
  const auto it = std::find_if(map.joints.begin(),
                               map.joints.end(),
                               [&](const SkeletonMapEntry& entry) {
                                 return entry.source_joint_index == source_joint_index;
                               });
  return it == map.joints.end() ? nullptr : &*it;
}

float rootScale(const RetargetOptions& options) {
  return options.root_scale_policy == RetargetRootScalePolicy::ExplicitScale
             ? options.root_translation_scale
             : 1.0f;
}

void applyRotationCorrection(AnimationChannel& channel, const glm::mat4& correction_matrix) {
  const math::Quat correction = correctionRotation(correction_matrix);
  for (QuatKeyframe& key : channel.rotation_keys) {
    key.value = math::normalize(math::mul(correction, key.value));
    key.in_tangent = math::mul(correction, key.in_tangent);
    key.out_tangent = math::mul(correction, key.out_tangent);
  }
}

glm::mat4 restRotationCorrection(const Joint& source, const Joint& target) {
  const math::Quat correction =
      math::normalize(math::mul(target.rest_local_rotation,
                                math::inverse(source.rest_local_rotation)));
  return glm::mat4_cast(math::toGlm(correction));
}

void retargetPositionKeys(AnimationChannel& channel,
                          const math::Vec3& source_rest,
                          const math::Vec3& target_rest,
                          float scale,
                          const math::Quat& delta_rotation = {}) {
  if (channel.position_keys.empty()) {
    return;
  }
  for (Vec3Keyframe& key : channel.position_keys) {
    const math::Vec3 delta = math::rotateVec(
        delta_rotation,
        math::subtract(key.value, source_rest));
    key.value = math::add(target_rest, math::scale(delta, scale));
    key.in_tangent = math::scale(
        math::rotateVec(delta_rotation, key.in_tangent),
        scale);
    key.out_tangent = math::scale(
        math::rotateVec(delta_rotation, key.out_tangent),
        scale);
  }
}

void retargetScaleKeys(AnimationChannel& channel,
                       const math::Vec3& source_rest,
                       const math::Vec3& target_rest) {
  if (channel.scale_keys.empty()) {
    return;
  }

  constexpr float kMinRestScale = 0.000001f;
  const auto value_component = [](float value, float source, float target) {
    if (std::abs(source) <= kMinRestScale) {
      return target + (value - source);
    }
    return value * (target / source);
  };
  const auto tangent_factor = [](float source, float target) {
    return std::abs(source) <= kMinRestScale ? 1.0f : target / source;
  };
  const math::Vec3 tangent_scale{
      tangent_factor(source_rest.x, target_rest.x),
      tangent_factor(source_rest.y, target_rest.y),
      tangent_factor(source_rest.z, target_rest.z),
  };

  for (Vec3Keyframe& key : channel.scale_keys) {
    key.value = {
        value_component(key.value.x, source_rest.x, target_rest.x),
        value_component(key.value.y, source_rest.y, target_rest.y),
        value_component(key.value.z, source_rest.z, target_rest.z),
    };
    key.in_tangent = {
        key.in_tangent.x * tangent_scale.x,
        key.in_tangent.y * tangent_scale.y,
        key.in_tangent.z * tangent_scale.z,
    };
    key.out_tangent = {
        key.out_tangent.x * tangent_scale.x,
        key.out_tangent.y * tangent_scale.y,
        key.out_tangent.z * tangent_scale.z,
    };
  }
}

const char* humanoidBoneName(HumanoidBone bone) {
  switch (bone) {
    case HumanoidBone::Root: return "Root";
    case HumanoidBone::Hips: return "Hips";
    case HumanoidBone::Spine: return "Spine";
    case HumanoidBone::Chest: return "Chest";
    case HumanoidBone::UpperChest: return "UpperChest";
    case HumanoidBone::Neck: return "Neck";
    case HumanoidBone::Head: return "Head";
    case HumanoidBone::LeftShoulder: return "LeftShoulder";
    case HumanoidBone::LeftUpperArm: return "LeftUpperArm";
    case HumanoidBone::LeftLowerArm: return "LeftLowerArm";
    case HumanoidBone::LeftHand: return "LeftHand";
    case HumanoidBone::RightShoulder: return "RightShoulder";
    case HumanoidBone::RightUpperArm: return "RightUpperArm";
    case HumanoidBone::RightLowerArm: return "RightLowerArm";
    case HumanoidBone::RightHand: return "RightHand";
    case HumanoidBone::LeftUpperLeg: return "LeftUpperLeg";
    case HumanoidBone::LeftLowerLeg: return "LeftLowerLeg";
    case HumanoidBone::LeftFoot: return "LeftFoot";
    case HumanoidBone::LeftToe: return "LeftToe";
    case HumanoidBone::RightUpperLeg: return "RightUpperLeg";
    case HumanoidBone::RightLowerLeg: return "RightLowerLeg";
    case HumanoidBone::RightFoot: return "RightFoot";
    case HumanoidBone::RightToe: return "RightToe";
    case HumanoidBone::LeftThumbProximal: return "LeftThumbProximal";
    case HumanoidBone::LeftThumbIntermediate: return "LeftThumbIntermediate";
    case HumanoidBone::LeftThumbDistal: return "LeftThumbDistal";
    case HumanoidBone::LeftIndexProximal: return "LeftIndexProximal";
    case HumanoidBone::LeftIndexIntermediate: return "LeftIndexIntermediate";
    case HumanoidBone::LeftIndexDistal: return "LeftIndexDistal";
    case HumanoidBone::LeftMiddleProximal: return "LeftMiddleProximal";
    case HumanoidBone::LeftMiddleIntermediate: return "LeftMiddleIntermediate";
    case HumanoidBone::LeftMiddleDistal: return "LeftMiddleDistal";
    case HumanoidBone::LeftRingProximal: return "LeftRingProximal";
    case HumanoidBone::LeftRingIntermediate: return "LeftRingIntermediate";
    case HumanoidBone::LeftRingDistal: return "LeftRingDistal";
    case HumanoidBone::LeftLittleProximal: return "LeftLittleProximal";
    case HumanoidBone::LeftLittleIntermediate: return "LeftLittleIntermediate";
    case HumanoidBone::LeftLittleDistal: return "LeftLittleDistal";
    case HumanoidBone::RightThumbProximal: return "RightThumbProximal";
    case HumanoidBone::RightThumbIntermediate: return "RightThumbIntermediate";
    case HumanoidBone::RightThumbDistal: return "RightThumbDistal";
    case HumanoidBone::RightIndexProximal: return "RightIndexProximal";
    case HumanoidBone::RightIndexIntermediate: return "RightIndexIntermediate";
    case HumanoidBone::RightIndexDistal: return "RightIndexDistal";
    case HumanoidBone::RightMiddleProximal: return "RightMiddleProximal";
    case HumanoidBone::RightMiddleIntermediate: return "RightMiddleIntermediate";
    case HumanoidBone::RightMiddleDistal: return "RightMiddleDistal";
    case HumanoidBone::RightRingProximal: return "RightRingProximal";
    case HumanoidBone::RightRingIntermediate: return "RightRingIntermediate";
    case HumanoidBone::RightRingDistal: return "RightRingDistal";
    case HumanoidBone::RightLittleProximal: return "RightLittleProximal";
    case HumanoidBone::RightLittleIntermediate: return "RightLittleIntermediate";
    case HumanoidBone::RightLittleDistal: return "RightLittleDistal";
  }
  return "Unknown";
}

void appendAlias(HumanoidProfileBone& bone, std::string value) {
  if (!value.empty()) {
    bone.aliases.push_back(std::move(value));
  }
}

HumanoidProfileBone profileBone(HumanoidBone bone,
                                bool required,
                                std::initializer_list<std::string_view> aliases) {
  HumanoidProfileBone out{.bone = bone, .required = required};
  out.aliases.reserve(aliases.size() + 3u);
  for (std::string_view alias : aliases) {
    appendAlias(out, std::string(alias));
  }
  appendAlias(out, humanoidBoneName(bone));
  return out;
}

const HumanoidBoneBinding* bindingForBone(const HumanoidRig& rig, HumanoidBone bone) {
  const auto it = std::find_if(rig.bindings.begin(),
                               rig.bindings.end(),
                               [&](const HumanoidBoneBinding& binding) {
                                 return binding.bone == bone;
                               });
  return it == rig.bindings.end() ? nullptr : &*it;
}

const HumanoidProfileBone* profileBoneForSemantic(const HumanoidProfile& profile,
                                                  HumanoidBone bone) {
  const auto it = std::find_if(profile.bones.begin(),
                               profile.bones.end(),
                               [&](const HumanoidProfileBone& profile_bone) {
                                 return profile_bone.bone == bone;
                               });
  return it == profile.bones.end() ? nullptr : &*it;
}

void addDiagnostic(HumanoidRetargetDiagnostic* diagnostic, std::string message) {
  if (diagnostic != nullptr) {
    diagnostic->messages.push_back(std::move(message));
  }
}

template <typename T>
void appendUnique(std::vector<T>& destination, const std::vector<T>& source) {
  for (const T& value : source) {
    if (std::find(destination.begin(), destination.end(), value) == destination.end()) {
      destination.push_back(value);
    }
  }
}

void mergeDiagnostic(HumanoidRetargetDiagnostic& destination,
                     const HumanoidRetargetDiagnostic& source) {
  appendUnique(destination.missing_required_bones, source.missing_required_bones);
  appendUnique(destination.optional_unmapped_bones, source.optional_unmapped_bones);
  appendUnique(destination.duplicate_bindings, source.duplicate_bindings);
  appendUnique(destination.channels_skipped, source.channels_skipped);
  appendUnique(destination.messages, source.messages);
}

std::vector<glm::mat4> composeRestModelMatrices(const Skeleton& skeleton,
                                                bool& valid) {
  std::vector<glm::mat4> model_matrices(skeleton.joints.size(), glm::mat4(1.0f));
  std::vector<uint8_t> state(skeleton.joints.size(), 0u);
  valid = true;

  auto compose = [&](auto&& self, uint32_t joint_index) -> glm::mat4 {
    if (joint_index >= skeleton.joints.size()) {
      valid = false;
      return glm::mat4(1.0f);
    }
    if (state[joint_index] == 2u) {
      return model_matrices[joint_index];
    }
    if (state[joint_index] == 1u) {
      valid = false;
      return glm::mat4(1.0f);
    }

    state[joint_index] = 1u;
    const Joint& joint = skeleton.joints[joint_index];
    const float rest_values[] = {
        joint.rest_local_position.x,
        joint.rest_local_position.y,
        joint.rest_local_position.z,
        joint.rest_local_rotation.x,
        joint.rest_local_rotation.y,
        joint.rest_local_rotation.z,
        joint.rest_local_rotation.w,
        joint.rest_local_scale.x,
        joint.rest_local_scale.y,
        joint.rest_local_scale.z,
    };
    if (std::any_of(std::begin(rest_values),
                    std::end(rest_values),
                    [](float value) { return !std::isfinite(value); })) {
      valid = false;
      state[joint_index] = 2u;
      return glm::mat4(1.0f);
    }
    const float rotation_length_squared =
        joint.rest_local_rotation.x * joint.rest_local_rotation.x +
        joint.rest_local_rotation.y * joint.rest_local_rotation.y +
        joint.rest_local_rotation.z * joint.rest_local_rotation.z +
        joint.rest_local_rotation.w * joint.rest_local_rotation.w;
    if (rotation_length_squared <= 0.00000001f) {
      valid = false;
      state[joint_index] = 2u;
      return glm::mat4(1.0f);
    }
    glm::mat4 local(1.0f);
    local = glm::translate(local, math::toGlm(joint.rest_local_position));
    local *= glm::mat4_cast(math::toGlm(joint.rest_local_rotation));
    local = glm::scale(local, math::toGlm(joint.rest_local_scale));

    glm::mat4 model = local;
    if (joint.parent_joint_index != kInvalidAnimationIndex) {
      if (joint.parent_joint_index >= skeleton.joints.size() ||
          joint.parent_joint_index == joint_index) {
        valid = false;
      } else {
        model = self(self, joint.parent_joint_index) * local;
      }
    }
    model_matrices[joint_index] = model;
    state[joint_index] = 2u;
    return model;
  };

  for (uint32_t joint_index = 0u; joint_index < skeleton.joints.size(); ++joint_index) {
    compose(compose, joint_index);
  }
  return model_matrices;
}

float humanoidHeight(const Skeleton& skeleton,
                     const std::vector<HumanoidBoneBinding>& bindings) {
  if (skeleton.joints.empty() || bindings.empty()) {
    return 0.0f;
  }

  bool hierarchy_valid = false;
  const std::vector<glm::mat4> model_matrices =
      composeRestModelMatrices(skeleton, hierarchy_valid);
  if (!hierarchy_valid) {
    return 0.0f;
  }

  auto model_position = [&](uint32_t joint_index) {
    return glm::vec3(model_matrices[joint_index][3]);
  };
  glm::vec3 up_axis(0.0f, 1.0f, 0.0f);
  const HumanoidBoneBinding* hips = nullptr;
  const HumanoidBoneBinding* head = nullptr;
  for (const HumanoidBoneBinding& binding : bindings) {
    if (binding.bone == HumanoidBone::Hips) {
      hips = &binding;
    } else if (binding.bone == HumanoidBone::Head) {
      head = &binding;
    }
  }
  if (hips != nullptr && head != nullptr &&
      hips->joint_index < model_matrices.size() &&
      head->joint_index < model_matrices.size()) {
    const glm::vec3 humanoid_up =
        model_position(head->joint_index) - model_position(hips->joint_index);
    const float length_squared = glm::dot(humanoid_up, humanoid_up);
    if (length_squared > 0.000001f) {
      up_axis = humanoid_up / std::sqrt(length_squared);
    }
  }

  float min_height = std::numeric_limits<float>::max();
  float max_height = std::numeric_limits<float>::lowest();
  std::unordered_set<uint32_t> measured_joints;
  for (const HumanoidBoneBinding& binding : bindings) {
    if (binding.joint_index >= model_matrices.size() ||
        !measured_joints.insert(binding.joint_index).second) {
      continue;
    }
    const float projected = glm::dot(model_position(binding.joint_index), up_axis);
    if (!std::isfinite(projected)) {
      return 0.0f;
    }
    min_height = std::min(min_height, projected);
    max_height = std::max(max_height, projected);
  }
  if (measured_joints.size() < 2u) {
    return 0.0f;
  }
  const float height = max_height - min_height;
  return std::isfinite(height) && height > 0.0f ? height : 0.0f;
}

std::unordered_map<std::string, uint32_t> buildJointNameMap(const Skeleton& skeleton) {
  std::unordered_map<std::string, uint32_t> map;
  map.reserve(skeleton.joints.size());
  for (uint32_t joint_index = 0u; joint_index < skeleton.joints.size(); ++joint_index) {
    const std::string& name = skeleton.joints[joint_index].name;
    if (name.empty()) {
      continue;
    }
    map.try_emplace(name, joint_index);
  }
  return map;
}

uint32_t findJointForAliases(const std::unordered_map<std::string, uint32_t>& joints,
                             const HumanoidProfileBone& profile_bone) {
  for (const std::string& alias : profile_bone.aliases) {
    if (auto it = joints.find(alias); it != joints.end()) {
      return it->second;
    }
  }
  return kInvalidAnimationIndex;
}

void recordChannelSkips(const AnimationClip& source_clip,
                        const Skeleton& source_skeleton,
                        const SkeletonMap& map,
                        HumanoidRetargetDiagnostic* diagnostic) {
  if (diagnostic == nullptr) {
    return;
  }
  for (uint32_t channel_index = 0u; channel_index < source_clip.channels.size(); ++channel_index) {
    const uint32_t source_joint = jointForChannel(source_skeleton, source_clip.channels[channel_index]);
    if (source_joint == kInvalidAnimationIndex ||
        entryForSourceJoint(map, source_joint) == nullptr) {
      diagnostic->channels_skipped.push_back(channel_index);
    }
  }
}

bool clipTranslatesJoint(const AnimationClip& clip,
                         const Skeleton& skeleton,
                         uint32_t joint_index) {
  if (joint_index >= skeleton.joints.size()) {
    return false;
  }
  constexpr float kMotionEpsilonSquared = 0.00000001f;
  const auto has_delta = [](const math::Vec3& value, const math::Vec3& reference) {
    const math::Vec3 delta = math::subtract(value, reference);
    return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z >
           kMotionEpsilonSquared;
  };
  const auto has_tangent = [&](const math::Vec3& tangent) {
    return has_delta(tangent, {});
  };
  const math::Vec3 rest = skeleton.joints[joint_index].rest_local_position;
  for (const AnimationChannel& channel : clip.channels) {
    if (jointForChannel(skeleton, channel) != joint_index) {
      continue;
    }
    for (const Vec3Keyframe& key : channel.position_keys) {
      if (has_delta(key.value, rest) || has_tangent(key.in_tangent) ||
          has_tangent(key.out_tangent)) {
        return true;
      }
    }
  }
  if (!clip.root_motion.has_value() ||
      clip.root_motion->target_node_index != skeleton.joints[joint_index].node_index) {
    return false;
  }
  if (clip.root_motion->position_keys.empty()) {
    return false;
  }
  const math::Vec3 root_motion_origin =
      clip.root_motion->position_keys.front().value;
  for (const Vec3Keyframe& key : clip.root_motion->position_keys) {
    if (has_delta(key.value, root_motion_origin) ||
        has_tangent(key.in_tangent) ||
        has_tangent(key.out_tangent)) {
      return true;
    }
  }
  return false;
}

AnimationClip emptyRetargetedClip(const AnimationClip& source_clip) {
  AnimationClip empty = source_clip;
  empty.channels.clear();
  empty.morph_target_tracks.clear();
  empty.root_motion.reset();
  return empty;
}

}  // namespace

bool validateSkeletonMap(const Skeleton& source,
                         const Skeleton& target,
                         const SkeletonMap& map,
                         std::string* diagnostic) {
  std::unordered_set<uint32_t> source_joints;
  std::unordered_set<uint32_t> target_joints;
  for (const SkeletonMapEntry& entry : map.joints) {
    if (entry.source_joint_index >= source.joints.size()) {
      if (diagnostic != nullptr) {
        *diagnostic = "source joint index out of range";
      }
      return false;
    }
    if (entry.target_joint_index >= target.joints.size()) {
      if (diagnostic != nullptr) {
        *diagnostic = "target joint index out of range";
      }
      return false;
    }
    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        if (!std::isfinite(entry.rest_pose_correction[column][row])) {
          if (diagnostic != nullptr) {
            *diagnostic = "rest-pose correction contains a non-finite value";
          }
          return false;
        }
      }
    }
    if (!source_joints.insert(entry.source_joint_index).second) {
      if (diagnostic != nullptr) {
        *diagnostic = "source joint is mapped more than once";
      }
      return false;
    }
    if (!target_joints.insert(entry.target_joint_index).second) {
      if (diagnostic != nullptr) {
        *diagnostic = "target joint is mapped more than once";
      }
      return false;
    }
  }
  if (map.source_root_joint_index != kInvalidAnimationIndex &&
      map.source_root_joint_index >= source.joints.size()) {
    if (diagnostic != nullptr) {
      *diagnostic = "source root joint index out of range";
    }
    return false;
  }
  if (map.target_root_joint_index != kInvalidAnimationIndex &&
      map.target_root_joint_index >= target.joints.size()) {
    if (diagnostic != nullptr) {
      *diagnostic = "target root joint index out of range";
    }
    return false;
  }
  if ((map.source_root_joint_index == kInvalidAnimationIndex) !=
      (map.target_root_joint_index == kInvalidAnimationIndex)) {
    if (diagnostic != nullptr) {
      *diagnostic = "source and target roots must be specified together";
    }
    return false;
  }
  if (map.source_root_joint_index != kInvalidAnimationIndex) {
    const SkeletonMapEntry* root_entry =
        entryForSourceJoint(map, map.source_root_joint_index);
    if (root_entry == nullptr ||
        root_entry->target_joint_index != map.target_root_joint_index) {
      if (diagnostic != nullptr) {
        *diagnostic = "root joints must be paired by the joint map";
      }
      return false;
    }
  }
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  return true;
}

AnimationClip retargetClip(const AnimationClip& source_clip,
                           const Skeleton& source_skeleton,
                           const Skeleton& target_skeleton,
                           const SkeletonMap& map,
                           const RetargetOptions& options) {
  AnimationClip out = source_clip;
  out.channels.clear();
  out.morph_target_tracks.clear();
  out.root_motion.reset();

  if (!validateSkeletonMap(source_skeleton, target_skeleton, map)) {
    return out;
  }
  if (options.translation_scale_source_joint_index != kInvalidAnimationIndex &&
      (options.translation_scale_source_joint_index >= source_skeleton.joints.size() ||
       entryForSourceJoint(map,
                           options.translation_scale_source_joint_index) == nullptr)) {
    return out;
  }
  if ((options.root_scale_policy != RetargetRootScalePolicy::None &&
       options.root_scale_policy != RetargetRootScalePolicy::ExplicitScale) ||
      (options.root_scale_policy == RetargetRootScalePolicy::ExplicitScale &&
       (!std::isfinite(options.root_translation_scale) ||
        options.root_translation_scale <= 0.0f))) {
    return out;
  }

  const float translation_scale = rootScale(options);
  const uint32_t translation_scale_joint =
      options.translation_scale_source_joint_index != kInvalidAnimationIndex
          ? options.translation_scale_source_joint_index
          : map.source_root_joint_index;
  for (const AnimationChannel& source_channel : source_clip.channels) {
    const uint32_t source_joint = jointForChannel(source_skeleton, source_channel);
    if (source_joint == kInvalidAnimationIndex) {
      if (options.copy_unmapped_channels) {
        out.channels.push_back(source_channel);
      }
      continue;
    }

    const SkeletonMapEntry* entry = entryForSourceJoint(map, source_joint);
    if (entry == nullptr || entry->target_joint_index >= target_skeleton.joints.size()) {
      if (options.copy_unmapped_channels) {
        out.channels.push_back(source_channel);
      }
      continue;
    }

    AnimationChannel retargeted = source_channel;
    retargeted.target_node_index = target_skeleton.joints[entry->target_joint_index].node_index;
    retargeted.target_joint_index = entry->target_joint_index;
    retargeted.target_skin_index = kInvalidAnimationIndex;
    applyRotationCorrection(retargeted, entry->rest_pose_correction);
    const float position_scale =
        source_joint == translation_scale_joint ? translation_scale : 1.0f;
    const math::Quat position_correction =
        source_joint == translation_scale_joint ||
                source_joint == map.source_root_joint_index
            ? correctionRotation(entry->rest_pose_correction)
            : math::Quat{};
    retargetPositionKeys(retargeted,
                         source_skeleton.joints[source_joint].rest_local_position,
                         target_skeleton.joints[entry->target_joint_index].rest_local_position,
                         position_scale,
                         position_correction);
    retargetScaleKeys(retargeted,
                      source_skeleton.joints[source_joint].rest_local_scale,
                      target_skeleton.joints[entry->target_joint_index].rest_local_scale);
    out.channels.push_back(std::move(retargeted));
  }

  if (source_clip.root_motion) {
    const uint32_t source_root_node =
        map.source_root_joint_index < source_skeleton.joints.size()
            ? source_skeleton.joints[map.source_root_joint_index].node_index
            : kInvalidAnimationIndex;
    const uint32_t target_root_node =
        map.target_root_joint_index < target_skeleton.joints.size()
            ? target_skeleton.joints[map.target_root_joint_index].node_index
            : kInvalidAnimationIndex;
    if (source_clip.root_motion->target_node_index == source_root_node &&
        target_root_node != kInvalidAnimationIndex) {
      out.root_motion = source_clip.root_motion;
      out.root_motion->target_node_index = target_root_node;
      if (const SkeletonMapEntry* root_entry =
              entryForSourceJoint(map, map.source_root_joint_index);
          root_entry != nullptr) {
        AnimationChannel root_channel{};
        root_channel.position_interpolation = out.root_motion->position_interpolation;
        root_channel.rotation_interpolation = out.root_motion->rotation_interpolation;
        root_channel.position_keys = std::move(out.root_motion->position_keys);
        root_channel.rotation_keys = std::move(out.root_motion->rotation_keys);
        applyRotationCorrection(root_channel, root_entry->rest_pose_correction);
        retargetPositionKeys(
            root_channel,
            source_skeleton.joints[root_entry->source_joint_index].rest_local_position,
            target_skeleton.joints[root_entry->target_joint_index].rest_local_position,
            translation_scale,
            correctionRotation(root_entry->rest_pose_correction));
        out.root_motion->position_keys = std::move(root_channel.position_keys);
        out.root_motion->rotation_keys = std::move(root_channel.rotation_keys);
      }
    } else {
      out.root_motion.reset();
    }
  }

  return out;
}

HumanoidProfile builtinHumanoidProfile(HumanoidProfileKind kind) {
  HumanoidProfile profile{};
  profile.kind = kind;
  profile.name = "Mixamo";
  profile.bones = {
      profileBone(HumanoidBone::Root, false, {"Armature", "RootNode"}),
      profileBone(HumanoidBone::Hips, true, {"mixamorig:Hips"}),
      profileBone(HumanoidBone::Spine, true, {"mixamorig:Spine"}),
      profileBone(HumanoidBone::Chest, true, {"mixamorig:Spine1", "Spine1"}),
      profileBone(HumanoidBone::UpperChest, false, {"mixamorig:Spine2", "Spine2"}),
      profileBone(HumanoidBone::Neck, true, {"mixamorig:Neck"}),
      profileBone(HumanoidBone::Head, true, {"mixamorig:Head"}),
      profileBone(HumanoidBone::LeftShoulder, true, {"mixamorig:LeftShoulder"}),
      profileBone(HumanoidBone::LeftUpperArm, true, {"mixamorig:LeftArm"}),
      profileBone(HumanoidBone::LeftLowerArm, true, {"mixamorig:LeftForeArm", "LeftForeArm"}),
      profileBone(HumanoidBone::LeftHand, true, {"mixamorig:LeftHand"}),
      profileBone(HumanoidBone::RightShoulder, true, {"mixamorig:RightShoulder"}),
      profileBone(HumanoidBone::RightUpperArm, true, {"mixamorig:RightArm"}),
      profileBone(HumanoidBone::RightLowerArm, true, {"mixamorig:RightForeArm", "RightForeArm"}),
      profileBone(HumanoidBone::RightHand, true, {"mixamorig:RightHand"}),
      profileBone(HumanoidBone::LeftUpperLeg, true, {"mixamorig:LeftUpLeg", "LeftUpLeg"}),
      profileBone(HumanoidBone::LeftLowerLeg, true, {"mixamorig:LeftLeg"}),
      profileBone(HumanoidBone::LeftFoot, true, {"mixamorig:LeftFoot"}),
      profileBone(HumanoidBone::LeftToe, true, {"mixamorig:LeftToeBase", "LeftToeBase", "LeftToes"}),
      profileBone(HumanoidBone::RightUpperLeg, true, {"mixamorig:RightUpLeg", "RightUpLeg"}),
      profileBone(HumanoidBone::RightLowerLeg, true, {"mixamorig:RightLeg"}),
      profileBone(HumanoidBone::RightFoot, true, {"mixamorig:RightFoot"}),
      profileBone(HumanoidBone::RightToe, true, {"mixamorig:RightToeBase", "RightToeBase", "RightToes"}),
      profileBone(HumanoidBone::LeftThumbProximal, false, {"mixamorig:LeftHandThumb1", "LeftHandThumb1"}),
      profileBone(HumanoidBone::LeftThumbIntermediate, false, {"mixamorig:LeftHandThumb2", "LeftHandThumb2"}),
      profileBone(HumanoidBone::LeftThumbDistal, false, {"mixamorig:LeftHandThumb3", "LeftHandThumb3"}),
      profileBone(HumanoidBone::LeftIndexProximal, false, {"mixamorig:LeftHandIndex1", "LeftHandIndex1"}),
      profileBone(HumanoidBone::LeftIndexIntermediate, false, {"mixamorig:LeftHandIndex2", "LeftHandIndex2"}),
      profileBone(HumanoidBone::LeftIndexDistal, false, {"mixamorig:LeftHandIndex3", "LeftHandIndex3"}),
      profileBone(HumanoidBone::LeftMiddleProximal, false, {"mixamorig:LeftHandMiddle1", "LeftHandMiddle1"}),
      profileBone(HumanoidBone::LeftMiddleIntermediate, false, {"mixamorig:LeftHandMiddle2", "LeftHandMiddle2"}),
      profileBone(HumanoidBone::LeftMiddleDistal, false, {"mixamorig:LeftHandMiddle3", "LeftHandMiddle3"}),
      profileBone(HumanoidBone::LeftRingProximal, false, {"mixamorig:LeftHandRing1", "LeftHandRing1"}),
      profileBone(HumanoidBone::LeftRingIntermediate, false, {"mixamorig:LeftHandRing2", "LeftHandRing2"}),
      profileBone(HumanoidBone::LeftRingDistal, false, {"mixamorig:LeftHandRing3", "LeftHandRing3"}),
      profileBone(HumanoidBone::LeftLittleProximal, false, {"mixamorig:LeftHandPinky1", "LeftHandPinky1", "LeftHandLittle1"}),
      profileBone(HumanoidBone::LeftLittleIntermediate, false, {"mixamorig:LeftHandPinky2", "LeftHandPinky2", "LeftHandLittle2"}),
      profileBone(HumanoidBone::LeftLittleDistal, false, {"mixamorig:LeftHandPinky3", "LeftHandPinky3", "LeftHandLittle3"}),
      profileBone(HumanoidBone::RightThumbProximal, false, {"mixamorig:RightHandThumb1", "RightHandThumb1"}),
      profileBone(HumanoidBone::RightThumbIntermediate, false, {"mixamorig:RightHandThumb2", "RightHandThumb2"}),
      profileBone(HumanoidBone::RightThumbDistal, false, {"mixamorig:RightHandThumb3", "RightHandThumb3"}),
      profileBone(HumanoidBone::RightIndexProximal, false, {"mixamorig:RightHandIndex1", "RightHandIndex1"}),
      profileBone(HumanoidBone::RightIndexIntermediate, false, {"mixamorig:RightHandIndex2", "RightHandIndex2"}),
      profileBone(HumanoidBone::RightIndexDistal, false, {"mixamorig:RightHandIndex3", "RightHandIndex3"}),
      profileBone(HumanoidBone::RightMiddleProximal, false, {"mixamorig:RightHandMiddle1", "RightHandMiddle1"}),
      profileBone(HumanoidBone::RightMiddleIntermediate, false, {"mixamorig:RightHandMiddle2", "RightHandMiddle2"}),
      profileBone(HumanoidBone::RightMiddleDistal, false, {"mixamorig:RightHandMiddle3", "RightHandMiddle3"}),
      profileBone(HumanoidBone::RightRingProximal, false, {"mixamorig:RightHandRing1", "RightHandRing1"}),
      profileBone(HumanoidBone::RightRingIntermediate, false, {"mixamorig:RightHandRing2", "RightHandRing2"}),
      profileBone(HumanoidBone::RightRingDistal, false, {"mixamorig:RightHandRing3", "RightHandRing3"}),
      profileBone(HumanoidBone::RightLittleProximal, false, {"mixamorig:RightHandPinky1", "RightHandPinky1", "RightHandLittle1"}),
      profileBone(HumanoidBone::RightLittleIntermediate, false, {"mixamorig:RightHandPinky2", "RightHandPinky2", "RightHandLittle2"}),
      profileBone(HumanoidBone::RightLittleDistal, false, {"mixamorig:RightHandPinky3", "RightHandPinky3", "RightHandLittle3"}),
  };
  return profile;
}

HumanoidRig bindHumanoidRig(const Skeleton& skeleton,
                            const HumanoidProfile& profile,
                            uint32_t skeleton_index,
                            std::string_view skeleton_key,
                            HumanoidRetargetDiagnostic* diagnostic) {
  if (diagnostic != nullptr) {
    *diagnostic = {};
  }

  HumanoidRig rig{};
  rig.name = skeleton.name.empty() ? profile.name + " Humanoid Rig"
                                   : skeleton.name + " Humanoid Rig";
  rig.skeleton_index = skeleton_index;
  rig.skeleton_key = std::string(skeleton_key);
  rig.skeleton = skeleton;
  rig.profile = profile;

  const std::unordered_map<std::string, uint32_t> joints = buildJointNameMap(skeleton);
  rig.bindings.reserve(profile.bones.size());
  for (const HumanoidProfileBone& profile_bone : profile.bones) {
    const uint32_t joint_index = findJointForAliases(joints, profile_bone);
    if (joint_index == kInvalidAnimationIndex) {
      continue;
    }
    rig.bindings.push_back(HumanoidBoneBinding{
        .bone = profile_bone.bone,
        .joint_index = joint_index,
        .joint_name = skeleton.joints[joint_index].name,
    });
  }
  validateHumanoidRig(rig, skeleton, profile, diagnostic);
  return rig;
}

float humanoidRigHeight(const HumanoidRig& rig) {
  return humanoidHeight(rig.skeleton, rig.bindings);
}

bool validateHumanoidRig(const HumanoidRig& rig,
                         const Skeleton& skeleton,
                         const HumanoidProfile& profile,
                         HumanoidRetargetDiagnostic* diagnostic) {
  HumanoidRetargetDiagnostic local;
  HumanoidRetargetDiagnostic* out = diagnostic != nullptr ? diagnostic : &local;
  if (diagnostic != nullptr) {
    *diagnostic = {};
  }

  bool contract_valid = true;
  if (skeleton.joints.empty()) {
    contract_valid = false;
    addDiagnostic(out, "humanoid skeleton is empty");
  }
  if (profile.bones.empty()) {
    contract_valid = false;
    addDiagnostic(out, "humanoid profile has no semantic bones");
  }
  if (rig.bindings.empty()) {
    contract_valid = false;
    addDiagnostic(out, "humanoid rig has no resolved bindings");
  }
  if (bindingForBone(rig, HumanoidBone::Hips) == nullptr) {
    contract_valid = false;
    addDiagnostic(out, "humanoid rig must bind Hips");
  }

  auto record_duplicate = [&](HumanoidBone bone) {
    if (std::find(out->duplicate_bindings.begin(),
                  out->duplicate_bindings.end(),
                  bone) == out->duplicate_bindings.end()) {
      out->duplicate_bindings.push_back(bone);
    }
  };

  bool indices_valid = true;
  std::unordered_set<uint32_t> seen_bones;
  std::unordered_map<uint32_t, HumanoidBone> seen_joints;
  for (const HumanoidBoneBinding& binding : rig.bindings) {
    if (profileBoneForSemantic(profile, binding.bone) == nullptr) {
      contract_valid = false;
      addDiagnostic(out,
                    std::string("humanoid binding is not declared by the profile: ") +
                        humanoidBoneName(binding.bone));
    }
    const uint32_t bone_key = static_cast<uint32_t>(binding.bone);
    if (!seen_bones.insert(bone_key).second) {
      record_duplicate(binding.bone);
    }
    if (binding.joint_index >= skeleton.joints.size()) {
      indices_valid = false;
      addDiagnostic(out,
                    std::string("humanoid binding joint out of range: ") +
                        humanoidBoneName(binding.bone));
      continue;
    }
    const auto [joint_it, inserted] =
        seen_joints.emplace(binding.joint_index, binding.bone);
    if (!inserted) {
      record_duplicate(joint_it->second);
      record_duplicate(binding.bone);
      addDiagnostic(out,
                    std::string("humanoid joint is bound to multiple semantics: ") +
                        humanoidBoneName(joint_it->second) + " and " +
                        humanoidBoneName(binding.bone));
    }
  }

  for (const HumanoidProfileBone& profile_bone : profile.bones) {
    if (bindingForBone(rig, profile_bone.bone) != nullptr) {
      continue;
    }
    if (profile_bone.required &&
        std::find(out->missing_required_bones.begin(),
                  out->missing_required_bones.end(),
                  profile_bone.bone) == out->missing_required_bones.end()) {
      out->missing_required_bones.push_back(profile_bone.bone);
    } else if (!profile_bone.required &&
               std::find(out->optional_unmapped_bones.begin(),
                         out->optional_unmapped_bones.end(),
                         profile_bone.bone) == out->optional_unmapped_bones.end()) {
      out->optional_unmapped_bones.push_back(profile_bone.bone);
    }
  }

  bool hierarchy_valid = false;
  (void)composeRestModelMatrices(skeleton, hierarchy_valid);
  if (!hierarchy_valid) {
    addDiagnostic(out, "humanoid skeleton hierarchy is invalid or cyclic");
  }

  bool connected = hierarchy_valid;
  uint32_t humanoid_root = kInvalidAnimationIndex;
  if (hierarchy_valid) {
    for (const HumanoidBoneBinding& binding : rig.bindings) {
      const HumanoidProfileBone* profile_bone =
          profileBoneForSemantic(profile, binding.bone);
      if (profile_bone == nullptr || !profile_bone->required ||
          binding.joint_index >= skeleton.joints.size()) {
        continue;
      }
      uint32_t root = binding.joint_index;
      while (skeleton.joints[root].parent_joint_index != kInvalidAnimationIndex) {
        root = skeleton.joints[root].parent_joint_index;
      }
      if (humanoid_root == kInvalidAnimationIndex) {
        humanoid_root = root;
      } else if (root != humanoid_root) {
        connected = false;
        addDiagnostic(out,
                      "required humanoid bones do not share one skeleton hierarchy");
        break;
      }
    }
  }

  return contract_valid && indices_valid && hierarchy_valid && connected &&
         out->missing_required_bones.empty() &&
         out->duplicate_bindings.empty();
}

SkeletonMap buildHumanoidSkeletonMap(const Skeleton& source_skeleton,
                                     const HumanoidRig& source_rig,
                                     const Skeleton& target_skeleton,
                                     const HumanoidRig& target_rig,
                                     HumanoidRetargetDiagnostic* diagnostic) {
  if (diagnostic != nullptr) {
    *diagnostic = {};
  }

  SkeletonMap map{};
  std::unordered_set<uint32_t> mapped_source_joints;
  std::unordered_set<uint32_t> mapped_target_joints;
  for (const HumanoidBoneBinding& source_binding : source_rig.bindings) {
    const HumanoidBoneBinding* target_binding =
        bindingForBone(target_rig, source_binding.bone);
    if (target_binding == nullptr) {
      if (diagnostic != nullptr) {
        const HumanoidProfileBone* source_profile_bone =
            profileBoneForSemantic(source_rig.profile, source_binding.bone);
        if (source_profile_bone != nullptr && source_profile_bone->required) {
          diagnostic->missing_required_bones.push_back(source_binding.bone);
        } else {
          diagnostic->optional_unmapped_bones.push_back(source_binding.bone);
        }
      }
      continue;
    }
    if (source_binding.joint_index >= source_skeleton.joints.size() ||
        target_binding->joint_index >= target_skeleton.joints.size()) {
      addDiagnostic(diagnostic,
                    std::string("humanoid map binding out of range: ") +
                        humanoidBoneName(source_binding.bone));
      continue;
    }
    if (mapped_source_joints.contains(source_binding.joint_index) ||
        mapped_target_joints.contains(target_binding->joint_index)) {
      if (diagnostic != nullptr &&
          std::find(diagnostic->duplicate_bindings.begin(),
                    diagnostic->duplicate_bindings.end(),
                    source_binding.bone) == diagnostic->duplicate_bindings.end()) {
        diagnostic->duplicate_bindings.push_back(source_binding.bone);
      }
      addDiagnostic(diagnostic,
                    std::string("humanoid retarget map reuses a joint: ") +
                        humanoidBoneName(source_binding.bone));
      continue;
    }
    mapped_source_joints.insert(source_binding.joint_index);
    mapped_target_joints.insert(target_binding->joint_index);
    map.joints.push_back(SkeletonMapEntry{
        .source_joint_index = source_binding.joint_index,
        .target_joint_index = target_binding->joint_index,
        .rest_pose_correction =
            restRotationCorrection(source_skeleton.joints[source_binding.joint_index],
                                   target_skeleton.joints[target_binding->joint_index]),
    });
  }

  const HumanoidBoneBinding* source_root = bindingForBone(source_rig, HumanoidBone::Root);
  const HumanoidBoneBinding* target_root = bindingForBone(target_rig, HumanoidBone::Root);
  if (source_root == nullptr || target_root == nullptr) {
    source_root = bindingForBone(source_rig, HumanoidBone::Hips);
    target_root = bindingForBone(target_rig, HumanoidBone::Hips);
  }
  if (source_root != nullptr) {
    map.source_root_joint_index = source_root->joint_index;
  }
  if (target_root != nullptr) {
    map.target_root_joint_index = target_root->joint_index;
  }
  return map;
}

SkeletonMap buildHumanoidSkeletonMap(const HumanoidRig& source_rig,
                                     const HumanoidRig& target_rig,
                                     HumanoidRetargetDiagnostic* diagnostic) {
  return buildHumanoidSkeletonMap(source_rig.skeleton,
                                  source_rig,
                                  target_rig.skeleton,
                                  target_rig,
                                  diagnostic);
}

AnimationClip retargetHumanoidClip(
    const AnimationClip& source_clip,
    const Skeleton& source_skeleton,
    const HumanoidRig& source_rig,
    const Skeleton& target_skeleton,
    const HumanoidRig& target_rig,
    const HumanoidRetargetOptions& options,
    HumanoidRetargetDiagnostic* diagnostic) {
  if (diagnostic != nullptr) {
    *diagnostic = {};
  }

  HumanoidRetargetDiagnostic source_diagnostic;
  HumanoidRetargetDiagnostic target_diagnostic;
  const bool source_valid =
      validateHumanoidRig(source_rig,
                          source_skeleton,
                          source_rig.profile,
                          &source_diagnostic);
  const bool target_valid =
      validateHumanoidRig(target_rig,
                          target_skeleton,
                          target_rig.profile,
                          &target_diagnostic);
  if (diagnostic != nullptr) {
    mergeDiagnostic(*diagnostic, source_diagnostic);
    mergeDiagnostic(*diagnostic, target_diagnostic);
  }
  if (!source_valid || !target_valid) {
    return emptyRetargetedClip(source_clip);
  }

  HumanoidRetargetDiagnostic map_diagnostic;
  SkeletonMap map = buildHumanoidSkeletonMap(source_skeleton,
                                             source_rig,
                                             target_skeleton,
                                             target_rig,
                                             &map_diagnostic);
  if (diagnostic != nullptr) {
    mergeDiagnostic(*diagnostic, map_diagnostic);
  }
  if (!map_diagnostic.valid()) {
    return emptyRetargetedClip(source_clip);
  }

  const HumanoidBoneBinding* source_hips =
      bindingForBone(source_rig, HumanoidBone::Hips);
  const HumanoidBoneBinding* target_hips =
      bindingForBone(target_rig, HumanoidBone::Hips);
  uint32_t translation_scale_joint = map.source_root_joint_index;
  if (!clipTranslatesJoint(source_clip,
                           source_skeleton,
                           map.source_root_joint_index) &&
      source_hips != nullptr && target_hips != nullptr) {
    translation_scale_joint = source_hips->joint_index;
  }
  if (source_clip.root_motion.has_value() && source_hips != nullptr &&
      target_hips != nullptr &&
      source_hips->joint_index < source_skeleton.joints.size() &&
      source_clip.root_motion->target_node_index ==
          source_skeleton.joints[source_hips->joint_index].node_index) {
    map.source_root_joint_index = source_hips->joint_index;
    map.target_root_joint_index = target_hips->joint_index;
  }

  RetargetOptions retarget_options{};
  retarget_options.root_scale_policy = options.root_scale_policy;
  retarget_options.root_translation_scale = options.root_translation_scale;
  retarget_options.translation_scale_source_joint_index = translation_scale_joint;
  retarget_options.copy_unmapped_channels = options.copy_unmapped_channels;
  const float source_height = humanoidHeight(source_skeleton, source_rig.bindings);
  const float target_height = humanoidHeight(target_skeleton, target_rig.bindings);
  if (options.root_scale_policy == RetargetRootScalePolicy::None &&
      options.derive_root_translation_scale_from_height &&
      source_height > 0.0001f &&
      target_height > 0.0001f) {
    retarget_options.root_scale_policy = RetargetRootScalePolicy::ExplicitScale;
    retarget_options.root_translation_scale = target_height / source_height;
  }

  if (!options.copy_unmapped_channels) {
    recordChannelSkips(source_clip, source_skeleton, map, diagnostic);
  }
  return retargetClip(source_clip,
                      source_skeleton,
                      target_skeleton,
                      map,
                      retarget_options);
}

AnimationClip retargetHumanoidClip(
    const AnimationClip& source_clip,
    const HumanoidRig& source_rig,
    const HumanoidRig& target_rig,
    const HumanoidRetargetOptions& options,
    HumanoidRetargetDiagnostic* diagnostic) {
  return retargetHumanoidClip(source_clip,
                              source_rig.skeleton,
                              source_rig,
                              target_rig.skeleton,
                              target_rig,
                              options,
                              diagnostic);
}

}  // namespace karma::world
