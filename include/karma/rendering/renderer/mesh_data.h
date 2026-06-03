#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace karma::renderer {

struct MeshData {
  std::vector<glm::vec3> vertices;
  std::vector<glm::vec3> normals;
  std::vector<glm::vec2> uvs;
  std::vector<glm::vec4> tangents;
  std::vector<glm::uvec4> joint_indices;
  std::vector<glm::vec4> joint_weights;
  std::vector<uint32_t> indices;

  struct MorphTarget {
    std::vector<glm::vec3> position_deltas;
    std::vector<glm::vec3> normal_deltas;
    std::vector<glm::vec3> tangent_deltas;
  };
  std::vector<MorphTarget> morph_targets;
};

}  // namespace karma::renderer
