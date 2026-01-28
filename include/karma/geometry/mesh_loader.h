#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace karma::geometry {

struct MeshData {
    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
};

std::vector<MeshData> loadGLB(const std::string& filename);

} // namespace karma::geometry
