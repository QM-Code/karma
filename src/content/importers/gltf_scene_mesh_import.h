#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "gltf_document.h"
#include "gltf_scene_import_internal.h"

namespace karma::scene {

void populateGltfMeshData(const GltfDocument& doc,
                          const std::unordered_map<std::string, uint32_t>& node_indices_by_name,
                          GltfScenePrefab& prefab);

}  // namespace karma::scene
