#include "karma/prefabs.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

#include "karma/assets.h"
#include "karma/prefabs.h"
#include "karma/math.h"
#include "karma/math.h"
#include "karma/components.h"
#include "karma/world.h"

namespace karma::prefabs {

namespace {

using Json = nlohmann::json;

components::TransformComponent composeTransform(
    const components::TransformComponent& parent,
    const components::TransformComponent& local) {
  components::TransformComponent transform{};
  const math::Vec3 scaled_local = math::multiply(local.localPosition(), parent.worldScale());
  const math::Vec3 rotated_local = math::rotateVec(parent.worldRotation(), scaled_local);
  transform.setLocalPosition(math::add(parent.worldPosition(), rotated_local));
  transform.setLocalRotation(math::mul(parent.worldRotation(), local.localRotation()));
  transform.setLocalScale(math::multiply(parent.worldScale(), local.localScale()));
  return transform;
}

std::filesystem::path resolvePrefabPath(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    return path / "prefab.json";
  }
  if (path.extension().empty()) {
    return path / "prefab.json";
  }
  return path;
}

bool readRequiredUint32(const Json& object,
                        std::string_view key,
                        uint32_t& out_value,
                        const std::filesystem::path& path) {
  const auto it = object.find(key);
  if (it == object.end() || (!it->is_number_unsigned() && !it->is_number_integer())) {
    spdlog::error("Prefab '{}' is missing numeric '{}' field", path.string(), key);
    return false;
  }
  const int64_t value = it->get<int64_t>();
  if (value < 0 || value > static_cast<int64_t>(UINT32_MAX)) {
    spdlog::error("Prefab '{}' has out-of-range '{}' field", path.string(), key);
    return false;
  }
  out_value = static_cast<uint32_t>(value);
  return true;
}

bool readRequiredSize(const Json& object,
                      std::string_view key,
                      size_t& out_value,
                      const std::filesystem::path& path) {
  const auto it = object.find(key);
  if (it == object.end() || (!it->is_number_unsigned() && !it->is_number_integer())) {
    spdlog::error("Prefab '{}' is missing numeric '{}' field", path.string(), key);
    return false;
  }
  const int64_t value = it->get<int64_t>();
  if (value < 0) {
    spdlog::error("Prefab '{}' has negative '{}' field", path.string(), key);
    return false;
  }
  out_value = static_cast<size_t>(value);
  return true;
}

bool readRequiredString(const Json& object,
                        std::string_view key,
                        std::string& out_value,
                        const std::filesystem::path& path) {
  const auto it = object.find(key);
  if (it == object.end() || !it->is_string()) {
    spdlog::error("Prefab '{}' is missing string '{}' field", path.string(), key);
    return false;
  }
  out_value = it->get<std::string>();
  return true;
}

bool validateParents(const PrefabDocument& document,
                     const std::filesystem::path& path) {
  if (document.nodes.empty()) {
    spdlog::error("Prefab '{}' contains no nodes", path.string());
    return false;
  }
  if (document.root >= document.nodes.size()) {
    spdlog::error("Prefab '{}' root index is out of range", path.string());
    return false;
  }
  if (document.nodes[document.root].parent.has_value()) {
    spdlog::error("Prefab '{}' root node must not have a parent", path.string());
    return false;
  }

  for (size_t index = 0; index < document.nodes.size(); ++index) {
    const auto parent = document.nodes[index].parent;
    if (parent.has_value() && *parent >= document.nodes.size()) {
      spdlog::error("Prefab '{}' node {} parent index is out of range",
                    path.string(),
                    index);
      return false;
    }

    std::vector<bool> visited(document.nodes.size(), false);
    size_t cursor = index;
    while (document.nodes[cursor].parent.has_value()) {
      if (visited[cursor]) {
        spdlog::error("Prefab '{}' contains a parent cycle at node {}",
                      path.string(),
                      index);
        return false;
      }
      visited[cursor] = true;
      cursor = *document.nodes[cursor].parent;
    }
  }

  return true;
}

std::optional<PrefabDocument> parseDocument(const Json& json,
                                            const std::filesystem::path& path) {
  if (!json.is_object()) {
    spdlog::error("Prefab '{}' root JSON value must be an object", path.string());
    return std::nullopt;
  }

  PrefabDocument document{};
  if (!readRequiredUint32(json, "version", document.version, path)) {
    return std::nullopt;
  }
  if (document.version != 1u) {
    spdlog::error("Prefab '{}' has unsupported version {}", path.string(), document.version);
    return std::nullopt;
  }
  if (!readRequiredSize(json, "root", document.root, path)) {
    return std::nullopt;
  }

  const auto nodes_it = json.find("nodes");
  if (nodes_it == json.end() || !nodes_it->is_array()) {
    spdlog::error("Prefab '{}' is missing array 'nodes' field", path.string());
    return std::nullopt;
  }

  std::unordered_set<uint32_t> ids;
  document.nodes.reserve(nodes_it->size());
  for (size_t index = 0; index < nodes_it->size(); ++index) {
    const Json& node_json = (*nodes_it)[index];
    if (!node_json.is_object()) {
      spdlog::error("Prefab '{}' node {} must be an object", path.string(), index);
      return std::nullopt;
    }

    PrefabNode node{};
    if (!readRequiredUint32(node_json, "id", node.id, path) ||
        !readRequiredString(node_json, "name", node.name, path)) {
      return std::nullopt;
    }
    if (!ids.insert(node.id).second) {
      spdlog::error("Prefab '{}' contains duplicate node id {}", path.string(), node.id);
      return std::nullopt;
    }

    const auto parent_it = node_json.find("parent");
    if (parent_it == node_json.end()) {
      spdlog::error("Prefab '{}' node {} is missing 'parent' field", path.string(), index);
      return std::nullopt;
    }
    if (parent_it->is_null()) {
      node.parent.reset();
    } else if (parent_it->is_number_unsigned() || parent_it->is_number_integer()) {
      const int64_t parent = parent_it->get<int64_t>();
      if (parent < 0) {
        spdlog::error("Prefab '{}' node {} has negative parent index",
                      path.string(),
                      index);
        return std::nullopt;
      }
      node.parent = static_cast<size_t>(parent);
    } else {
      spdlog::error("Prefab '{}' node {} parent must be null or numeric",
                    path.string(),
                    index);
      return std::nullopt;
    }

    const auto components_it = node_json.find("components");
    if (components_it == node_json.end() || !components_it->is_object()) {
      spdlog::error("Prefab '{}' node {} is missing object 'components' field",
                    path.string(),
                    index);
      return std::nullopt;
    }
    node.components = *components_it;
    document.nodes.push_back(std::move(node));
  }

  if (!validateParents(document, path)) {
    return std::nullopt;
  }
  return document;
}

std::optional<PrefabDocument> loadDocument(const std::filesystem::path& input_path) {
  const std::filesystem::path path = resolvePrefabPath(input_path);
  std::ifstream stream(path);
  if (!stream) {
    spdlog::error("Failed to open prefab '{}'", path.string());
    return std::nullopt;
  }

  Json json;
  try {
    stream >> json;
  } catch (const std::exception& e) {
    spdlog::error("Failed to parse prefab '{}': {}", path.string(), e.what());
    return std::nullopt;
  }

  return parseDocument(json, path);
}

Json toJson(const PrefabDocument& document) {
  Json nodes = Json::array();
  for (const PrefabNode& node : document.nodes) {
    nodes.push_back(Json{
        {"id", node.id},
        {"name", node.name},
        {"parent", node.parent.has_value() ? Json(*node.parent) : Json(nullptr)},
        {"components", node.components},
    });
  }
  return Json{
      {"version", document.version},
      {"root", document.root},
      {"nodes", std::move(nodes)},
  };
}

uint64_t entityKey(world::Entity entity) {
  return (static_cast<uint64_t>(entity.index) << 32u) |
         static_cast<uint64_t>(entity.generation);
}

struct CachedPrefabPackage {
  assets::AssetRegistry* assets = nullptr;
  assets::AssetPackageHandle handle;
  uint32_t ref_count = 0u;
};

std::unordered_map<std::string, CachedPrefabPackage> g_cached_prefab_packages;
std::unordered_map<uint64_t, std::string> g_package_by_root_entity;
assets::AssetRegistry* g_default_prefab_assets = nullptr;

std::string packageCacheKey(assets::AssetRegistry* assets,
                            const std::filesystem::path& manifest_path) {
  std::error_code ec;
  std::filesystem::path absolute = std::filesystem::absolute(manifest_path, ec);
  if (ec) {
    absolute = manifest_path;
  }
  return std::to_string(reinterpret_cast<std::uintptr_t>(assets)) + "|" +
         absolute.lexically_normal().string();
}

struct PackageAcquireResult {
  bool success = true;
  std::string cache_key;
  std::optional<assets::AssetPackageHandle> handle;
};

PackageAcquireResult acquirePrefabPackage(assets::AssetRegistry* assets,
                                          const std::filesystem::path& prefab_path) {
  PackageAcquireResult result{};
  if (assets == nullptr) {
    assets = g_default_prefab_assets;
  }
  const std::filesystem::path manifest_path =
      assets::resolveAssetPackagePath(prefab_path.parent_path());
  std::error_code ec;
  if (!std::filesystem::exists(manifest_path, ec)) {
    return result;
  }
  if (ec) {
    spdlog::error("Failed to inspect asset package '{}': {}",
                  manifest_path.string(),
                  ec.message());
    result.success = false;
    return result;
  }
  if (assets == nullptr) {
    spdlog::error("Prefab '{}' has an asset package but no AssetRegistry was supplied",
                  prefab_path.string());
    result.success = false;
    return result;
  }

  result.cache_key = packageCacheKey(assets, manifest_path);
  auto cached_it = g_cached_prefab_packages.find(result.cache_key);
  if (cached_it != g_cached_prefab_packages.end()) {
    cached_it->second.ref_count += 1u;
    result.handle = cached_it->second.handle;
    return result;
  }

  std::string diagnostic;
  std::optional<assets::AssetPackageHandle> package =
      assets::importAssetPackage(*assets, manifest_path, &diagnostic);
  if (!package.has_value()) {
    spdlog::error("Failed to import prefab asset package '{}': {}",
                  manifest_path.string(),
                  diagnostic);
    result.success = false;
    return result;
  }

  CachedPrefabPackage cached{};
  cached.assets = assets;
  cached.handle = *package;
  cached.ref_count = 1u;
  g_cached_prefab_packages[result.cache_key] = cached;
  result.handle = std::move(package);
  return result;
}

void releasePrefabPackageByKey(const std::string& cache_key) {
  if (cache_key.empty()) {
    return;
  }
  auto cached_it = g_cached_prefab_packages.find(cache_key);
  if (cached_it == g_cached_prefab_packages.end()) {
    return;
  }
  if (cached_it->second.ref_count > 0u) {
    cached_it->second.ref_count -= 1u;
  }
  if (cached_it->second.ref_count == 0u) {
    if (cached_it->second.assets != nullptr) {
      assets::unloadAssetPackage(*cached_it->second.assets, cached_it->second.handle);
    }
    g_cached_prefab_packages.erase(cached_it);
  }
}

std::string entityName(const world::World& world, world::Entity entity) {
  if (!world.isAlive(entity) || !world.has<components::TagComponent>(entity)) {
    return {};
  }
  return world.get<components::TagComponent>(entity).name;
}

void collectSubtree(const world::World& world,
                    const world::Scene& scene,
                    world::NodeId node_id,
                    std::vector<world::NodeId>& out_nodes) {
  if (!scene.isAlive(node_id)) {
    return;
  }
  const world::Node& node = scene.get(node_id);
  if (!node.entity.isValid() || !world.isAlive(node.entity)) {
    return;
  }
  out_nodes.push_back(node_id);
  for (const world::NodeId child : node.children) {
    collectSubtree(world, scene, child, out_nodes);
  }
}

PrefabDocument buildDocument(const world::World& world,
                             const world::Scene& scene,
                             world::Entity root,
                             const PrefabSaveOptions& options) {
  ensureBuiltinComponentSerializers();
  const ComponentSerializerRegistry& registry = componentSerializerRegistry();

  std::vector<world::NodeId> scene_nodes;
  const world::NodeId root_node = scene.findNode(root);
  if (options.include_children && scene.isAlive(root_node)) {
    collectSubtree(world, scene, root_node, scene_nodes);
  } else if (scene.isAlive(root_node)) {
    scene_nodes.push_back(root_node);
  }

  PrefabDocument document{};
  document.root = 0;

  std::unordered_map<world::NodeId, size_t> index_by_node;
  if (!scene_nodes.empty()) {
    document.nodes.reserve(scene_nodes.size());
    for (size_t index = 0; index < scene_nodes.size(); ++index) {
      index_by_node[scene_nodes[index]] = index;
    }
    for (size_t index = 0; index < scene_nodes.size(); ++index) {
      const world::Node& scene_node = scene.get(scene_nodes[index]);
      PrefabNode prefab_node{};
      prefab_node.id = static_cast<uint32_t>(index);
      prefab_node.name = entityName(world, scene_node.entity);
      if (scene.isAlive(scene_node.parent)) {
        const auto parent_it = index_by_node.find(scene_node.parent);
        if (parent_it != index_by_node.end()) {
          prefab_node.parent = parent_it->second;
        }
      }

      prefab_node.components = Json::object();
      for (const ComponentSerializer& serializer : registry.serializers()) {
        if (!serializer.has(world, scene_node.entity)) {
          continue;
        }
        prefab_node.components[serializer.type_name] =
            serializer.serialize(world, scene_node.entity);
      }
      document.nodes.push_back(std::move(prefab_node));
    }
    return document;
  }

  PrefabNode prefab_node{};
  prefab_node.id = 0u;
  prefab_node.name = entityName(world, root);
  prefab_node.components = Json::object();
  for (const ComponentSerializer& serializer : registry.serializers()) {
    if (!serializer.has(world, root)) {
      continue;
    }
    prefab_node.components[serializer.type_name] = serializer.serialize(world, root);
  }
  document.nodes.push_back(std::move(prefab_node));
  return document;
}

void destroyCreated(world::World& world,
                    world::Scene& scene,
                    const std::vector<world::Entity>& entities,
                    const std::vector<world::NodeId>& nodes) {
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    if (scene.isAlive(*it)) {
      scene.destroyNode(*it);
    }
  }
  for (const world::Entity entity : entities) {
    if (world.isAlive(entity)) {
      world.destroyEntity(entity);
    }
  }
}

bool deserializeComponents(world::World& world,
                           world::Entity entity,
                           const PrefabNode& node,
                           const std::filesystem::path& path) {
  ComponentSerializerRegistry& registry = componentSerializerRegistry();
  std::unordered_set<std::string> consumed;
  consumed.reserve(node.components.size());

  for (const ComponentSerializer& serializer : registry.serializers()) {
    const auto component_it = node.components.find(serializer.type_name);
    if (component_it == node.components.end()) {
      continue;
    }
    try {
      if (!serializer.deserialize(world, entity, *component_it)) {
        spdlog::error("Prefab '{}' node '{}' has invalid '{}' component payload",
                      path.string(),
                      node.name,
                      serializer.type_name);
        return false;
      }
    } catch (const std::exception& e) {
      spdlog::error("Prefab '{}' node '{}' failed to add '{}' component: {}",
                    path.string(),
                    node.name,
                    serializer.type_name,
                    e.what());
      return false;
    }
    consumed.insert(serializer.type_name);
  }

  for (auto it = node.components.begin(); it != node.components.end(); ++it) {
    const std::string type_name = it.key();
    if (consumed.find(type_name) == consumed.end()) {
      spdlog::error("Prefab '{}' node '{}' has unknown component '{}'",
                   path.string(),
                   node.name,
                   type_name);
      return false;
    }
  }
  return true;
}

void applyRootTransform(world::World& world,
                        world::Entity root,
                        const components::TransformComponent& root_transform) {
  components::TransformComponent saved{};
  if (world.has<components::TransformComponent>(root)) {
    saved = world.get<components::TransformComponent>(root);
  }

  const components::TransformComponent final_transform =
      composeTransform(root_transform, saved);
  world.add(root, final_transform);
}

void ensureTransformsForHierarchy(world::World& world, world::Entity entity) {
  if (!world.has<components::TransformComponent>(entity)) {
    world.add(entity, components::TransformComponent{});
  }
}

void collectSubtreeNodes(const world::Scene& scene,
                         world::NodeId root,
                         std::vector<world::NodeId>& out_nodes) {
  if (!scene.isAlive(root)) {
    return;
  }
  out_nodes.push_back(root);
  const world::Node& node = scene.get(root);
  for (const world::NodeId child : node.children) {
    collectSubtreeNodes(scene, child, out_nodes);
  }
}

}  // namespace

bool savePrefab(const world::World& world,
                const world::Scene& scene,
                world::Entity root,
                const std::filesystem::path& input_path,
                const PrefabSaveOptions& options) {
  if (!world.isAlive(root)) {
    spdlog::error("Cannot save prefab: root entity is not alive");
    return false;
  }

  const PrefabDocument document = buildDocument(world, scene, root, options);
  if (document.nodes.empty()) {
    spdlog::error("Cannot save prefab: no serializable nodes found");
    return false;
  }

  const std::filesystem::path path = resolvePrefabPath(input_path);
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      spdlog::error("Failed to create prefab directory '{}': {}",
                    path.parent_path().string(),
                    ec.message());
      return false;
    }
  }

  std::ofstream stream(path);
  if (!stream) {
    spdlog::error("Failed to open prefab '{}' for writing", path.string());
    return false;
  }
  stream << toJson(document).dump(2) << '\n';
  return static_cast<bool>(stream);
}

std::optional<PrefabInstance> instantiatePrefab(
    world::World& world,
    world::Scene& scene,
    const std::filesystem::path& input_path,
    const PrefabInstantiateDesc& desc) {
  ensureBuiltinComponentSerializers();
  const std::filesystem::path path = resolvePrefabPath(input_path);
  std::optional<PrefabDocument> document = loadDocument(path);
  if (!document.has_value()) {
    return std::nullopt;
  }
  PackageAcquireResult package = acquirePrefabPackage(desc.assets, path);
  if (!package.success) {
    return std::nullopt;
  }

  PrefabInstance instance{};
  std::vector<world::Entity> created_entities;
  std::vector<world::NodeId> created_nodes;
  created_entities.reserve(document->nodes.size());
  created_nodes.reserve(document->nodes.size());

  for (size_t index = 0; index < document->nodes.size(); ++index) {
    world::Entity entity = world.createEntity();
    world::NodeId scene_node = scene.createNode(entity);
    created_entities.push_back(entity);
    created_nodes.push_back(scene_node);
    instance.entities.push_back(entity);
    instance.entities_by_id[document->nodes[index].id] = entity;
    if (!document->nodes[index].name.empty()) {
      instance.named_entities[document->nodes[index].name] = entity;
    }
  }

  for (size_t index = 0; index < document->nodes.size(); ++index) {
    if (!deserializeComponents(world, created_entities[index], document->nodes[index], path)) {
      destroyCreated(world, scene, created_entities, created_nodes);
      releasePrefabPackageByKey(package.cache_key);
      return std::nullopt;
    }
    ensureTransformsForHierarchy(world, created_entities[index]);
  }

  for (size_t index = 0; index < document->nodes.size(); ++index) {
    const PrefabNode& node = document->nodes[index];
    const bool is_root = index == document->root;
    const std::string final_name =
        is_root && !desc.name_override.empty() ? desc.name_override : node.name;
    if (!final_name.empty()) {
      world.setName(created_entities[index], final_name);
      if (is_root && final_name != node.name) {
        instance.named_entities[final_name] = created_entities[index];
      }
    }
  }

  for (size_t index = 0; index < document->nodes.size(); ++index) {
    const auto parent = document->nodes[index].parent;
    if (!parent.has_value()) {
      continue;
    }
    scene.reparent(created_nodes[index], created_nodes[*parent]);
  }

  instance.root = created_entities[document->root];
  instance.root_scene_node = created_nodes[document->root];
  instance.asset_registry = desc.assets;
  instance.asset_package = package.handle;
  if (!package.cache_key.empty()) {
    g_package_by_root_entity[entityKey(instance.root)] = package.cache_key;
  }
  applyRootTransform(world, instance.root, desc.root_transform);
  world::updateWorldTransforms(world, scene);
  return instance;
}

bool destroyPrefab(world::World& world, world::Scene& scene, world::Entity root) {
  if (!world.isAlive(root)) {
    return false;
  }

  const uint64_t root_key = entityKey(root);
  std::string package_key;
  if (const auto package_it = g_package_by_root_entity.find(root_key);
      package_it != g_package_by_root_entity.end()) {
    package_key = package_it->second;
    g_package_by_root_entity.erase(package_it);
  }

  const world::NodeId root_node = scene.findNode(root);
  if (!scene.isAlive(root_node)) {
    world.destroyEntity(root);
    releasePrefabPackageByKey(package_key);
    return true;
  }

  std::vector<world::NodeId> nodes;
  collectSubtreeNodes(scene, root_node, nodes);

  std::vector<world::Entity> entities;
  entities.reserve(nodes.size());
  for (const world::NodeId node_id : nodes) {
    const world::Node& node = scene.get(node_id);
    if (node.entity.isValid()) {
      entities.push_back(node.entity);
    }
  }

  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    if (scene.isAlive(*it)) {
      scene.destroyNode(*it);
    }
  }
  for (const world::Entity entity : entities) {
    if (world.isAlive(entity)) {
      world.destroyEntity(entity);
    }
  }
  releasePrefabPackageByKey(package_key);
  return true;
}

void clearPrefabAssetPackages() {
  for (auto& [key, cached] : g_cached_prefab_packages) {
    (void)key;
    if (cached.assets != nullptr) {
      assets::unloadAssetPackage(*cached.assets, cached.handle);
    }
  }
  g_cached_prefab_packages.clear();
  g_package_by_root_entity.clear();
  g_default_prefab_assets = nullptr;
}

void bindPrefabAssetRegistry(assets::AssetRegistry* assets) {
  g_default_prefab_assets = assets;
}

}  // namespace karma::prefabs
