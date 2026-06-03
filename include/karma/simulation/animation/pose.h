#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "karma/core/math/types.h"
#include "karma/simulation/animation/animation_clip.h"

namespace karma::animation {

struct PoseTransform {
  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  bool has_position = false;
  bool has_rotation = false;
  bool has_scale = false;
};

struct LocalPose {
  std::vector<PoseTransform> nodes;
};

struct PoseHierarchy {
  std::vector<uint32_t> parent_indices;
  std::vector<PoseTransform> rest_local_transforms;
};

struct ModelPose {
  std::vector<glm::mat4> node_matrices;
};

struct SkinningPalette {
  uint32_t skin_index = kInvalidAnimationIndex;
  std::vector<glm::mat4> joint_matrices;
  bool valid = false;
  std::string diagnostic;
};

glm::mat4 poseTransformToMatrix(const PoseTransform& transform);
PoseTransform poseTransformFromSample(const SampledTransform& sample);

LocalPose makeRestLocalPose(const PoseHierarchy& hierarchy);
void applySampleToLocalPose(LocalPose& pose,
                            uint32_t target_node_index,
                            const SampledTransform& sample);
ModelPose composeModelPose(const PoseHierarchy& hierarchy, const LocalPose& local_pose);

SkinningPalette buildSkinningPalette(const std::vector<uint32_t>& joint_node_indices,
                                     const std::vector<glm::mat4>& inverse_bind_matrices,
                                     const std::vector<glm::mat4>& node_model_matrices,
                                     const glm::mat4& render_space_world,
                                     uint32_t skin_index = kInvalidAnimationIndex);

}  // namespace karma::animation
