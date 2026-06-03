#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "karma/core/math/types.h"
#include "karma/simulation/animation/animation_clip.h"

namespace karma::animation {

/// \ingroup karma_animation
/// Transform sample with per-channel presence flags.
struct PoseTransform {
  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  bool has_position = false;
  bool has_rotation = false;
  bool has_scale = false;
};

/// \ingroup karma_animation
/// Local pose indexed by imported node index.
struct LocalPose {
  std::vector<PoseTransform> nodes;
};

/// \ingroup karma_animation
/// Parent links and rest pose used for model-pose composition.
struct PoseHierarchy {
  std::vector<uint32_t> parent_indices;
  std::vector<PoseTransform> rest_local_transforms;
};

/// \ingroup karma_animation
/// Model-space matrix palette indexed by imported node index.
struct ModelPose {
  std::vector<glm::mat4> node_matrices;
};

/// \ingroup karma_animation
/// Final joint matrix palette for one skin.
struct SkinningPalette {
  uint32_t skin_index = kInvalidAnimationIndex;
  std::vector<glm::mat4> joint_matrices;
  bool valid = false;
  std::string diagnostic;
};

/// Converts a pose transform to a matrix.
glm::mat4 poseTransformToMatrix(const PoseTransform& transform);
/// Converts an optional sampled transform to a pose transform.
PoseTransform poseTransformFromSample(const SampledTransform& sample);

/// Creates a local pose from rest transforms.
LocalPose makeRestLocalPose(const PoseHierarchy& hierarchy);
/// Applies one sampled node transform to a local pose.
void applySampleToLocalPose(LocalPose& pose,
                            uint32_t target_node_index,
                            const SampledTransform& sample);
/// Composes local pose transforms through the hierarchy.
ModelPose composeModelPose(const PoseHierarchy& hierarchy, const LocalPose& local_pose);

/// Builds final skinning matrices from node model matrices and inverse binds.
SkinningPalette buildSkinningPalette(const std::vector<uint32_t>& joint_node_indices,
                                     const std::vector<glm::mat4>& inverse_bind_matrices,
                                     const std::vector<glm::mat4>& node_model_matrices,
                                     const glm::mat4& render_space_world,
                                     uint32_t skin_index = kInvalidAnimationIndex);

}  // namespace karma::animation
