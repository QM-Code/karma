#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "karma/content/prefabs/prefab.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::prefabs {

struct PrefabParamOverride {
  std::string name;
  PrefabParamValue value{math::Color{1.0f, 1.0f, 1.0f, 1.0f}};
};

struct PrefabInstantiateDesc {
  std::string name;
  components::TransformComponent transform{};
  std::vector<PrefabParamOverride> param_overrides;
};

struct PrefabInstance {
  ecs::Entity root{};
  std::vector<ecs::Entity> members;
  std::unordered_map<std::string, ecs::Entity> named_members;

  bool valid() const { return root.isValid(); }

  ecs::Entity find(std::string_view name) const {
    const auto it = named_members.find(std::string(name));
    if (it == named_members.end()) {
      return {};
    }
    return it->second;
  }
};

std::optional<PrefabInstance> instantiatePrefab(
    ecs::World& world,
    renderer::GraphicsDevice* graphics,
    const Prefab& prefab,
    const PrefabInstantiateDesc& desc = {});

std::optional<PrefabInstance> instantiatePrefab(
    ecs::World& world,
    renderer::GraphicsDevice* graphics,
    const std::filesystem::path& path,
    const PrefabInstantiateDesc& desc = {});

bool setPrefabPlayback(ecs::World& world, ecs::Entity root, bool enabled);
bool restartPrefab(ecs::World& world, ecs::Entity root);
bool destroyPrefab(ecs::World& world, ecs::Entity root);

}  // namespace karma::prefabs
