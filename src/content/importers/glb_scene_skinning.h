#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "gltf_document.h"
#include "karma/content/importers/glb_scene_import.h"

struct aiScene;

namespace karma::scene {

void populateGltfSkins(const GltfDocument& doc,
                       const std::unordered_map<std::string, uint32_t>& node_indices_by_name,
                       GlbScenePrefab& prefab);

void populatePrimitiveSkinning(
    const aiScene& scene,
    const std::unordered_map<std::string, uint32_t>& node_indices_by_name,
    GlbScenePrefab& prefab);

}  // namespace karma::scene
