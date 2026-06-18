#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "gltf_document.h"
#include "karma/content/importers/gltf_scene_import.h"

struct aiScene;

namespace karma::scene {

std::vector<animation::AnimationClip> loadGltfAnimationClips(
    const GltfDocument& doc,
    const std::unordered_map<std::string, uint32_t>& node_indices_by_name,
    const GltfScenePrefab& prefab);

std::vector<animation::AnimationClip> loadAnimationClips(
    const aiScene& scene,
    const std::unordered_map<std::string, uint32_t>& node_indices_by_name);

}  // namespace karma::scene
