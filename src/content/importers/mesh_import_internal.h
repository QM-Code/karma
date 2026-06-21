#pragma once

#include <filesystem>
#include <vector>

#include "karma/world.h"

namespace karma::assets {

std::vector<world::MeshData> importMeshes(const std::filesystem::path& path);

}  // namespace karma::assets
