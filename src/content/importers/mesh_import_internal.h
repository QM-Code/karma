#pragma once

#include <filesystem>
#include <vector>

#include "karma/world/geometry/mesh_data.h"

namespace karma::content {

std::vector<geometry::MeshData> importMeshes(const std::filesystem::path& path);

}  // namespace karma::content
