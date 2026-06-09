#pragma once

#include <filesystem>
#include <vector>

#include "karma/world/geometry/mesh_data.h"

namespace karma::content {

/// \ingroup karma_content
/// Imports mesh geometry from a file into shared world-layer mesh data.
std::vector<geometry::MeshData> importMeshes(const std::filesystem::path& path);

}  // namespace karma::content
