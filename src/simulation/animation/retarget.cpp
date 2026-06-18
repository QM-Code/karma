#include "karma/simulation/animation/retarget.h"

#include <algorithm>
#include <utility>

#include <glm/gtc/quaternion.hpp>

#include "karma/core/math/quat.h"
#include "karma/core/math/vec3.h"

namespace karma::animation {

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

void scalePositionKeys(AnimationChannel& channel, float scale) {
  if (scale == 1.0f) {
    return;
  }
  for (Vec3Keyframe& key : channel.position_keys) {
    key.value = math::scale(key.value, scale);
    key.in_tangent = math::scale(key.in_tangent, scale);
    key.out_tangent = math::scale(key.out_tangent, scale);
  }
}

void scaleRootMotion(RootMotionTrack& root_motion, float scale) {
  if (scale == 1.0f) {
    return;
  }
  for (Vec3Keyframe& key : root_motion.position_keys) {
    key.value = math::scale(key.value, scale);
    key.in_tangent = math::scale(key.in_tangent, scale);
    key.out_tangent = math::scale(key.out_tangent, scale);
  }
}

}  // namespace

bool validateSkeletonMap(const Skeleton& source,
                         const Skeleton& target,
                         const SkeletonMap& map,
                         std::string* diagnostic) {
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

  if (!validateSkeletonMap(source_skeleton, target_skeleton, map)) {
    return out;
  }

  const float translation_scale = rootScale(options);
  for (const AnimationChannel& source_channel : source_clip.channels) {
    const uint32_t source_joint =
        jointForNode(source_skeleton, source_channel.target_node_index);
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
    if (source_joint == map.source_root_joint_index) {
      scalePositionKeys(retargeted, translation_scale);
    }
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
      scaleRootMotion(*out.root_motion, translation_scale);
    } else {
      out.root_motion.reset();
    }
  }

  return out;
}

}  // namespace karma::animation
