#include "karma/world.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/math.h"

namespace karma::world {

glm::mat4 poseTransformToMatrix(const PoseTransform& transform) {
  glm::mat4 matrix(1.0f);
  matrix = glm::translate(matrix, math::toGlm(transform.position));
  matrix *= glm::mat4_cast(math::toGlm(transform.rotation));
  matrix = glm::scale(matrix, math::toGlm(transform.scale));
  return matrix;
}

PoseTransform poseTransformFromSample(const SampledTransform& sample) {
  PoseTransform transform{};
  if (sample.position) {
    transform.position = *sample.position;
    transform.has_position = true;
  }
  if (sample.rotation) {
    transform.rotation = *sample.rotation;
    transform.has_rotation = true;
  }
  if (sample.scale) {
    transform.scale = *sample.scale;
    transform.has_scale = true;
  }
  return transform;
}

LocalPose makeRestLocalPose(const PoseHierarchy& hierarchy) {
  LocalPose pose{};
  pose.nodes = hierarchy.rest_local_transforms;
  for (PoseTransform& transform : pose.nodes) {
    transform.has_position = true;
    transform.has_rotation = true;
    transform.has_scale = true;
  }
  return pose;
}

void applySampleToLocalPose(LocalPose& pose,
                            uint32_t target_node_index,
                            const SampledTransform& sample) {
  if (target_node_index >= pose.nodes.size()) {
    pose.nodes.resize(static_cast<size_t>(target_node_index) + 1u);
  }
  PoseTransform& transform = pose.nodes[target_node_index];
  if (sample.position) {
    transform.position = *sample.position;
    transform.has_position = true;
  }
  if (sample.rotation) {
    transform.rotation = *sample.rotation;
    transform.has_rotation = true;
  }
  if (sample.scale) {
    transform.scale = *sample.scale;
    transform.has_scale = true;
  }
}

ModelPose composeModelPose(const PoseHierarchy& hierarchy, const LocalPose& local_pose) {
  const size_t node_count =
      std::max(hierarchy.parent_indices.size(), local_pose.nodes.size());
  ModelPose pose{};
  pose.node_matrices.assign(node_count, glm::mat4(1.0f));

  std::vector<uint8_t> composed(node_count, 0u);
  auto compose_node = [&](auto&& self, size_t node_index) -> glm::mat4 {
    if (node_index >= node_count) {
      return glm::mat4(1.0f);
    }
    if (composed[node_index] != 0u) {
      return pose.node_matrices[node_index];
    }

    PoseTransform local{};
    if (node_index < hierarchy.rest_local_transforms.size()) {
      local = hierarchy.rest_local_transforms[node_index];
    }
    if (node_index < local_pose.nodes.size()) {
      const PoseTransform& sampled = local_pose.nodes[node_index];
      if (sampled.has_position) {
        local.position = sampled.position;
      }
      if (sampled.has_rotation) {
        local.rotation = sampled.rotation;
      }
      if (sampled.has_scale) {
        local.scale = sampled.scale;
      }
    }

    glm::mat4 model = poseTransformToMatrix(local);
    if (node_index < hierarchy.parent_indices.size()) {
      const uint32_t parent = hierarchy.parent_indices[node_index];
      if (parent != kInvalidAnimationIndex && parent < node_count && parent != node_index) {
        model = self(self, parent) * model;
      }
    }
    pose.node_matrices[node_index] = model;
    composed[node_index] = 1u;
    return model;
  };

  for (size_t i = 0; i < node_count; ++i) {
    compose_node(compose_node, i);
  }
  return pose;
}

SkinningPalette buildSkinningPalette(const std::vector<uint32_t>& joint_node_indices,
                                     const std::vector<glm::mat4>& inverse_bind_matrices,
                                     const std::vector<glm::mat4>& node_model_matrices,
                                     const glm::mat4& render_space_world,
                                     uint32_t skin_index) {
  SkinningPalette palette{};
  palette.skin_index = skin_index;
  if (joint_node_indices.empty()) {
    palette.diagnostic = "Skin has no joints";
    return palette;
  }

  const glm::mat4 render_space_inverse = glm::inverse(render_space_world);
  palette.joint_matrices.reserve(joint_node_indices.size());
  for (size_t joint_index = 0; joint_index < joint_node_indices.size(); ++joint_index) {
    const uint32_t node_index = joint_node_indices[joint_index];
    if (node_index >= node_model_matrices.size()) {
      palette.diagnostic = "Skin joint node index is outside the model pose";
      palette.joint_matrices.clear();
      return palette;
    }
    const glm::mat4 inverse_bind =
        joint_index < inverse_bind_matrices.size() ? inverse_bind_matrices[joint_index]
                                                   : glm::mat4(1.0f);
    palette.joint_matrices.push_back(render_space_inverse *
                                     node_model_matrices[node_index] *
                                     inverse_bind);
  }

  palette.valid = true;
  return palette;
}

}  // namespace karma::world
