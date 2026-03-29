#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "karma/prefabs/prefab.h"

namespace karma::prefabs {

struct PrefabEntryHandlerContext {
  ecs::World& world;
  renderer::GraphicsDevice* graphics = nullptr;
  const PrefabEntry& entry;
  const std::string& entity_name;
  const components::TransformComponent& world_transform;
  const std::unordered_map<std::string, PrefabParamValue>& resolved_params;
};

using PrefabEntryHandler = std::function<ecs::Entity(const PrefabEntryHandlerContext&)>;

bool registerPrefabEntryHandler(PrefabEntry::Type type, PrefabEntryHandler handler);
void unregisterPrefabEntryHandler(PrefabEntry::Type type);
void clearPrefabEntryHandlers();
bool hasPrefabEntryHandler(PrefabEntry::Type type);
ecs::Entity instantiatePrefabEntry(const PrefabEntryHandlerContext& context);

}  // namespace karma::prefabs
