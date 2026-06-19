#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "karma/world/ecs/entity.h"
#include "karma/world/scene/node.h"

namespace karma::content {
class AssetRegistry;
struct GltfSceneAsset;
}  // namespace karma::content

namespace karma::ecs {
class World;
}  // namespace karma::ecs

namespace karma::scene {
class Scene;

/// \ingroup karma_content
/// Sentinel node id for imported glTF scene data.
constexpr uint32_t kInvalidGltfSceneNode = Node::kInvalidId;

/// \ingroup karma_content
/// Controls how a registered glTF scene asset is instantiated into ECS/scene data.
struct GltfSceneInstantiateOptions {
  bool create_synthetic_root = false;
  bool autoplay_animations = true;
  /// Retained for older call sites. Registered glTF scene assets already carry
  /// deterministic mesh/material keys, so this is ignored for asset instantiation.
  std::string asset_key_prefix;
};

/// \ingroup karma_content
/// ECS/scene entities created from a registered glTF scene asset.
struct GltfSceneImportResult {
  ecs::Entity root_entity{};
  scene::NodeId root_node = scene::Node::kInvalidId;
  std::vector<ecs::Entity> entities;
  std::vector<ecs::Entity> node_entities_by_index;
  /// Renderable morph primitive entities keyed by imported glTF node index.
  std::vector<std::vector<ecs::Entity>> morph_entities_by_node_index;

  /// Returns true when a root ECS entity and scene node were created.
  bool valid() const {
    return root_entity.isValid() && root_node != scene::Node::kInvalidId;
  }
};

/// Instantiates cached/registered glTF scene metadata into world and scene data.
GltfSceneImportResult instantiateGltfSceneAsset(
    ecs::World& world,
    scene::Scene& scene,
    content::AssetRegistry& assets,
    const content::GltfSceneAsset& asset,
    const GltfSceneInstantiateOptions& options = {});

}  // namespace karma::scene
