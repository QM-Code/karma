#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "karma/world/components/transform.h"
#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/world.h"
#include "karma/world/scene/node.h"
#include "karma/world/scene/scene.h"

namespace karma::prefabs {

struct PrefabNode {
  uint32_t id = 0;
  std::string name;
  std::optional<size_t> parent;
  nlohmann::json components = nlohmann::json::object();
};

struct PrefabDocument {
  uint32_t version = 1;
  size_t root = 0;
  std::vector<PrefabNode> nodes;
};

struct PrefabSaveOptions {
  bool include_children = true;
};

struct PrefabInstantiateDesc {
  components::TransformComponent root_transform{};
  std::string name_override;
};

struct PrefabInstance {
  ecs::Entity root{};
  scene::NodeId root_scene_node = scene::Node::kInvalidId;
  std::vector<ecs::Entity> entities;
  std::unordered_map<std::string, ecs::Entity> named_entities;
  std::unordered_map<uint32_t, ecs::Entity> entities_by_id;

  bool valid() const { return root.isValid(); }

  ecs::Entity find(std::string_view name) const {
    const auto it = named_entities.find(std::string(name));
    if (it == named_entities.end()) {
      return {};
    }
    return it->second;
  }

  ecs::Entity find(uint32_t saved_node_id) const {
    const auto it = entities_by_id.find(saved_node_id);
    if (it == entities_by_id.end()) {
      return {};
    }
    return it->second;
  }
};

bool savePrefab(const ecs::World& world,
                const scene::Scene& scene,
                ecs::Entity root,
                const std::filesystem::path& path,
                const PrefabSaveOptions& options = {});

std::optional<PrefabInstance> instantiatePrefab(
    ecs::World& world,
    scene::Scene& scene,
    const std::filesystem::path& path,
    const PrefabInstantiateDesc& desc = {});

bool destroyPrefab(ecs::World& world, scene::Scene& scene, ecs::Entity root);

}  // namespace karma::prefabs
