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
  std::vector<uint32_t> indices;
};

}  // namespace karma::renderer
