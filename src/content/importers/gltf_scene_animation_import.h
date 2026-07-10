#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "gltf_document.h"
#include "gltf_scene_import_internal.h"

struct aiScene;

namespace karma::world {

std::vector<world::AnimationClip> loadGltfAnimationClips(
    const GltfDocument& doc,
    const std::unordered_map<std::string, uint32_t>& node_indices_by_name,
    const GltfScenePrefab& prefab);

std::vector<world::AnimationClip> loadAnimationClips(
    const aiScene& scene,
    const std::unordered_map<std::string, uint32_t>& node_indices_by_name,
    const GltfScenePrefab* prefab = nullptr);

}  // namespace karma::world
