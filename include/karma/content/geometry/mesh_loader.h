#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace karma::geometry {

/// \ingroup karma_content
/// Legacy GLB mesh data payload.
struct MeshData {
    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
};

/// Loads meshes from a GLB file into legacy geometry payloads.
std::vector<MeshData> loadGLB(const std::string& filename);

} // namespace karma::geometry
