#pragma once

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/world.h"



#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>


namespace karma::prefabs {

/// \ingroup karma_prefabs
/// Serialization hooks for one ECS component type.
struct ComponentSerializer {
  std::string type_name;
  std::function<bool(const world::World&, world::Entity)> has;
  std::function<nlohmann::json(const world::World&, world::Entity)> serialize;
  std::function<bool(world::World&, world::Entity, const nlohmann::json&)> deserialize;
};

/// \ingroup karma_prefabs
/// Registry mapping component type names to JSON serializers.
///
/// The current global registry is process-wide. Prefer scoped ownership if
/// future tools need multiple independent engine instances in one process.
class ComponentSerializerRegistry {
 public:
  /// Registers a serializer by type name.
  bool registerSerializer(ComponentSerializer serializer);
  /// Clears all registered serializers.
  void clear();

  /// Finds a serializer by component type name.
  const ComponentSerializer* find(std::string_view type_name) const;
  /// Registered serializers in insertion order.
  const std::vector<ComponentSerializer>& serializers() const { return serializers_; }

 private:
  std::vector<ComponentSerializer> serializers_;
  std::unordered_map<std::string, size_t> indices_;
};

/// Process-global component serializer registry.
ComponentSerializerRegistry& componentSerializerRegistry();
/// Registers built-in component serializers into `registry`.
void registerBuiltinComponentSerializers(ComponentSerializerRegistry& registry);
/// Ensures built-in serializers are present in the global registry.
void ensureBuiltinComponentSerializers();

}  // namespace karma::prefabs


#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>


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
  uint32_t version = 2;
  size_t root = 0;
  nlohmann::json variables = nlohmann::json::object();
  std::vector<PrefabNode> nodes;
};

/// \ingroup karma_prefabs
/// Options controlling prefab save traversal.
struct PrefabSaveOptions {
  bool include_children = true;
};

/// \ingroup karma_prefabs
/// Instantiation-time root transform, naming, asset, and variable overrides.
struct PrefabInstantiateDesc {
  components::TransformComponent root_transform{};
  std::string name_override;
  assets::AssetRegistry* assets = nullptr;
  std::unordered_map<std::string, nlohmann::json> variables;
};

/// \ingroup karma_prefabs
/// ECS entities created by prefab instantiation.
struct PrefabInstance {
  world::Entity root{};
  world::NodeId root_scene_node = world::Node::kInvalidId;
  std::vector<world::Entity> entities;
  std::unordered_map<std::string, world::Entity> named_entities;
  std::unordered_map<uint32_t, world::Entity> entities_by_id;
  std::optional<assets::AssetPackageHandle> asset_package;
  assets::AssetRegistry* asset_registry = nullptr;

  /// Returns true when a root entity was created.
  bool valid() const { return root.isValid(); }

  /// Finds an instantiated entity by saved name.
  world::Entity find(std::string_view name) const {
    const auto it = named_entities.find(std::string(name));
    if (it == named_entities.end()) {
      return {};
    }
    return it->second;
  }

  /// Finds an instantiated entity by saved node id.
  world::Entity find(uint32_t saved_node_id) const {
    const auto it = entities_by_id.find(saved_node_id);
    if (it == entities_by_id.end()) {
      return {};
    }
    return it->second;
  }
};

/// Saves an entity subtree to a JSON prefab file.
bool savePrefab(const world::World& world,
                const world::Scene& scene,
                world::Entity root,
                const std::filesystem::path& path,
                const PrefabSaveOptions& options = {});

/// Instantiates a JSON prefab file into world and scene data.
std::optional<PrefabInstance> instantiatePrefab(
    world::World& world,
    world::Scene& scene,
    const std::filesystem::path& path,
    const PrefabInstantiateDesc& desc = {});

/// Destroys a prefab instance rooted at `root`.
bool destroyPrefab(world::World& world, world::Scene& scene, world::Entity root);

/// Binds the default registry used when `PrefabInstantiateDesc::assets` is null.
void bindPrefabAssetRegistry(assets::AssetRegistry* assets);

/// Releases all globally cached prefab asset packages.
void clearPrefabAssetPackages();

}  // namespace karma::prefabs
