#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "karma/content/assets/asset_package.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/world.h"
#include "karma/world/scene/node.h"
#include "karma/world/scene/scene.h"

namespace karma::prefabs {

/// \ingroup karma_prefabs
/// Serialized prefab node.
struct PrefabNode {
  uint32_t id = 0;
  std::string name;
  std::optional<size_t> parent;
  nlohmann::json components = nlohmann::json::object();
};

/// \ingroup karma_prefabs
/// Serialized prefab document.
struct PrefabDocument {
  uint32_t version = 1;
  size_t root = 0;
  std::vector<PrefabNode> nodes;
};

/// \ingroup karma_prefabs
/// Options controlling prefab save traversal.
struct PrefabSaveOptions {
  bool include_children = true;
};

/// \ingroup karma_prefabs
/// Instantiation-time root transform and naming overrides.
struct PrefabInstantiateDesc {
  components::TransformComponent root_transform{};
  std::string name_override;
  content::AssetRegistry* assets = nullptr;
};

/// \ingroup karma_prefabs
/// ECS entities created by prefab instantiation.
struct PrefabInstance {
  ecs::Entity root{};
  scene::NodeId root_scene_node = scene::Node::kInvalidId;
  std::vector<ecs::Entity> entities;
  std::unordered_map<std::string, ecs::Entity> named_entities;
  std::unordered_map<uint32_t, ecs::Entity> entities_by_id;
  std::optional<content::AssetPackageHandle> asset_package;
  content::AssetRegistry* asset_registry = nullptr;

  /// Returns true when a root entity was created.
  bool valid() const { return root.isValid(); }

  /// Finds an instantiated entity by saved name.
  ecs::Entity find(std::string_view name) const {
    const auto it = named_entities.find(std::string(name));
    if (it == named_entities.end()) {
      return {};
    }
    return it->second;
  }

  /// Finds an instantiated entity by saved node id.
  ecs::Entity find(uint32_t saved_node_id) const {
    const auto it = entities_by_id.find(saved_node_id);
    if (it == entities_by_id.end()) {
      return {};
    }
    return it->second;
  }
};

/// Saves an entity subtree to a JSON prefab file.
bool savePrefab(const ecs::World& world,
                const scene::Scene& scene,
                ecs::Entity root,
                const std::filesystem::path& path,
                const PrefabSaveOptions& options = {});

/// Instantiates a JSON prefab file into world and scene data.
std::optional<PrefabInstance> instantiatePrefab(
    ecs::World& world,
    scene::Scene& scene,
    const std::filesystem::path& path,
    const PrefabInstantiateDesc& desc = {});

/// Destroys a prefab instance rooted at `root`.
bool destroyPrefab(ecs::World& world, scene::Scene& scene, ecs::Entity root);

/// Binds the default registry used when `PrefabInstantiateDesc::assets` is null.
void bindPrefabAssetRegistry(content::AssetRegistry* assets);

/// Releases all globally cached prefab asset packages.
void clearPrefabAssetPackages();

}  // namespace karma::prefabs
