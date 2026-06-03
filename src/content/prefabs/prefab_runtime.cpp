#include "karma/content/prefabs/prefab.h"

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

#include "karma/content/prefabs/component_serializer_registry.h"
#include "karma/content/prefabs/prefab_resource_context.h"
#include "karma/core/math/quat.h"
#include "karma/world/components/tag.h"
#include "karma/world/scene/transform_hierarchy.h"

namespace karma::prefabs {

namespace {

using Json = nlohmann::json;

math::Vec3 multiplyVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x * b.x, a.y * b.y, a.z * b.z};
}

math::Vec3 addVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

components::TransformComponent composeTransform(
    const components::TransformComponent& parent,
    const components::TransformComponent& local) {
  components::TransformComponent transform{};
  const math::Vec3 scaled_local = multiplyVec3(local.getPosition(), parent.getScale());
  const math::Vec3 rotated_local = math::rotateVec(parent.getRotation(), scaled_local);
  transform.setPosition(addVec3(parent.getPosition(), rotated_local));
  transform.setRotation(math::mul(parent.getRotation(), local.getRotation()));
  transform.setScale(multiplyVec3(parent.getScale(), local.getScale()));
  return transform;
}

components::TransformComponent toTransform(
    const components::LocalTransformComponent& local) {
  return components::TransformComponent{local.position, local.rotation, local.scale};
}

components::LocalTransformComponent toLocalTransform(
    const components::TransformComponent& transform) {
  return components::LocalTransformComponent{
      transform.getPosition(),
      transform.getRotation(),
      transform.getScale(),
  };
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

std::string entityName(const ecs::World& world, ecs::Entity entity) {
  if (!world.isAlive(entity) || !world.has<components::TagComponent>(entity)) {
    return {};
  }
  return world.get<components::TagComponent>(entity).name;
}

void collectSubtree(const ecs::World& world,
                    const scene::Scene& scene,
                    scene::NodeId node_id,
                    std::vector<scene::NodeId>& out_nodes) {
  if (!scene.isAlive(node_id)) {
    return;
  }
  const scene::Node& node = scene.get(node_id);
  if (!node.entity.isValid() || !world.isAlive(node.entity)) {
    return;
  }
  out_nodes.push_back(node_id);
  for (const scene::NodeId child : node.children) {
    collectSubtree(world, scene, child, out_nodes);
  }
}

PrefabDocument buildDocument(const ecs::World& world,
                             const scene::Scene& scene,
                             ecs::Entity root,
                             const PrefabSaveOptions& options) {
  ensureBuiltinComponentSerializers();
  const ComponentSerializerRegistry& registry = componentSerializerRegistry();

  std::vector<scene::NodeId> scene_nodes;
  const scene::NodeId root_node = scene.findNode(root);
  if (options.include_children && scene.isAlive(root_node)) {
    collectSubtree(world, scene, root_node, scene_nodes);
  } else if (scene.isAlive(root_node)) {
    scene_nodes.push_back(root_node);
  }

  PrefabDocument document{};
  document.root = 0;

  std::unordered_map<scene::NodeId, size_t> index_by_node;
  if (!scene_nodes.empty()) {
    document.nodes.reserve(scene_nodes.size());
    for (size_t index = 0; index < scene_nodes.size(); ++index) {
      index_by_node[scene_nodes[index]] = index;
    }
    for (size_t index = 0; index < scene_nodes.size(); ++index) {
      const scene::Node& scene_node = scene.get(scene_nodes[index]);
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

void destroyCreated(ecs::World& world,
                    scene::Scene& scene,
                    const std::vector<ecs::Entity>& entities,
                    const std::vector<scene::NodeId>& nodes) {
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    if (scene.isAlive(*it)) {
      scene.destroyNode(*it);
    }
  }
  for (const ecs::Entity entity : entities) {
    if (world.isAlive(entity)) {
      world.destroyEntity(entity);
    }
  }
}

bool deserializeComponents(ecs::World& world,
                           ecs::Entity entity,
                           const PrefabNode& node,
                           const std::filesystem::path& path) {
  ComponentSerializerRegistry& registry = componentSerializerRegistry();
  for (auto it = node.components.begin(); it != node.components.end(); ++it) {
    const std::string type_name = it.key();
    const ComponentSerializer* serializer = registry.find(type_name);
    if (serializer == nullptr) {
      spdlog::warn("Prefab '{}' node '{}' has unknown component '{}'; skipping",
                   path.string(),
                   node.name,
                   type_name);
      continue;
    }
    if (!serializer->deserialize(world, entity, it.value())) {
      spdlog::error("Prefab '{}' node '{}' has invalid '{}' component payload",
                    path.string(),
                    node.name,
                    type_name);
      return false;
    }
  }
  return true;
}

void applyRootTransform(ecs::World& world,
                        ecs::Entity root,
                        const components::TransformComponent& root_transform) {
  components::TransformComponent saved{};
  if (world.has<components::TransformComponent>(root)) {
    saved = world.get<components::TransformComponent>(root);
  } else if (world.has<components::LocalTransformComponent>(root)) {
    saved = toTransform(world.get<components::LocalTransformComponent>(root));
  }

  const components::TransformComponent final_transform =
      composeTransform(root_transform, saved);
  world.add(root, final_transform);
  if (world.has<components::LocalTransformComponent>(root)) {
    world.add(root, toLocalTransform(final_transform));
  }
}

void ensureTransformsForHierarchy(ecs::World& world, ecs::Entity entity) {
  if (!world.has<components::TransformComponent>(entity) &&
      world.has<components::LocalTransformComponent>(entity)) {
    world.add(entity, toTransform(world.get<components::LocalTransformComponent>(entity)));
  }
}

void collectSubtreeNodes(const scene::Scene& scene,
                         scene::NodeId root,
                         std::vector<scene::NodeId>& out_nodes) {
  if (!scene.isAlive(root)) {
    return;
  }
  out_nodes.push_back(root);
  const scene::Node& node = scene.get(root);
  for (const scene::NodeId child : node.children) {
    collectSubtreeNodes(scene, child, out_nodes);
  }
}

}  // namespace

bool savePrefab(const ecs::World& world,
                const scene::Scene& scene,
                ecs::Entity root,
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
    ecs::World& world,
    scene::Scene& scene,
    const std::filesystem::path& input_path,
    const PrefabInstantiateDesc& desc) {
  ensureBuiltinComponentSerializers();
  const std::filesystem::path path = resolvePrefabPath(input_path);
  std::optional<PrefabDocument> document = loadDocument(path);
  if (!document.has_value()) {
    return std::nullopt;
  }
  if (!ensurePrefabResourcesLoaded(path)) {
    return std::nullopt;
  }

  PrefabInstance instance{};
  std::vector<ecs::Entity> created_entities;
  std::vector<scene::NodeId> created_nodes;
  created_entities.reserve(document->nodes.size());
  created_nodes.reserve(document->nodes.size());

  for (size_t index = 0; index < document->nodes.size(); ++index) {
    ecs::Entity entity = world.createEntity();
    scene::NodeId scene_node = scene.createNode(entity);
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
  applyRootTransform(world, instance.root, desc.root_transform);
  scene::updateWorldTransforms(world, scene);
  return instance;
}

bool destroyPrefab(ecs::World& world, scene::Scene& scene, ecs::Entity root) {
  if (!world.isAlive(root)) {
    return false;
  }

  const scene::NodeId root_node = scene.findNode(root);
  if (!scene.isAlive(root_node)) {
    world.destroyEntity(root);
    return true;
  }

  std::vector<scene::NodeId> nodes;
  collectSubtreeNodes(scene, root_node, nodes);

  std::vector<ecs::Entity> entities;
  entities.reserve(nodes.size());
  for (const scene::NodeId node_id : nodes) {
    const scene::Node& node = scene.get(node_id);
    if (node.entity.isValid()) {
      entities.push_back(node.entity);
    }
  }

  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
    if (scene.isAlive(*it)) {
      scene.destroyNode(*it);
    }
  }
  for (const ecs::Entity entity : entities) {
    if (world.isAlive(entity)) {
      world.destroyEntity(entity);
    }
  }
  return true;
}

}  // namespace karma::prefabs
