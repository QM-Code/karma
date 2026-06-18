#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

#include "karma/simulation/animation/animation_clip.h"

namespace karma::animation {

/// \ingroup karma_animation
/// One explicit source-joint to target-joint retarget mapping.
struct SkeletonMapEntry {
  uint32_t source_joint_index = kInvalidAnimationIndex;
  uint32_t target_joint_index = kInvalidAnimationIndex;
  glm::mat4 rest_pose_correction{1.0f};
};

/// \ingroup karma_animation
/// Explicit skeleton mapping used for clip retargeting.
struct SkeletonMap {
  std::vector<SkeletonMapEntry> joints;
  uint32_t source_root_joint_index = kInvalidAnimationIndex;
  uint32_t target_root_joint_index = kInvalidAnimationIndex;
};

/// \ingroup karma_animation
/// Root translation scale policy used while retargeting clips.
enum class RetargetRootScalePolicy : uint8_t {
  None,
  ExplicitScale,
};

/// \ingroup karma_animation
/// Retargeting options for explicit skeleton maps.
struct RetargetOptions {
  RetargetRootScalePolicy root_scale_policy = RetargetRootScalePolicy::None;
  float root_translation_scale = 1.0f;
  bool copy_unmapped_channels = false;
};

/// Validates that all mapped joints exist on their source and target skeletons.
bool validateSkeletonMap(const Skeleton& source,
                         const Skeleton& target,
                         const SkeletonMap& map,
                         std::string* diagnostic = nullptr);

/// Retargets a clip from `source_skeleton` to `target_skeleton`.
AnimationClip retargetClip(const AnimationClip& source_clip,
                           const Skeleton& source_skeleton,
                           const Skeleton& target_skeleton,
                           const SkeletonMap& map,
                           const RetargetOptions& options = {});

}  // namespace karma::animation
