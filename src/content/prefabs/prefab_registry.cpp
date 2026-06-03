#include "karma/content/prefabs/prefab_registry.h"

#include <spdlog/spdlog.h>

namespace karma::prefabs {

PrefabRegistry::~PrefabRegistry() {
  shutdown();
}

void PrefabRegistry::bindContext(const PrefabPackageContext& context) {
  context_ = context;
}

void PrefabRegistry::clearContext() {
  context_ = {};
}

bool PrefabRegistry::registerPrefab(const std::string& key, RegisteredPrefabDesc desc) {
  if (key.empty()) {
    return false;
  }
  if (desc.prefab_path.empty()) {
    return false;
  }

  auto it = entries_.find(key);
  if (it != entries_.end()) {
    cleanupEntry(it->second);
    it->second = Entry{.desc = std::move(desc), .prepared = false};
    return true;
  }

  entries_.emplace(key, Entry{.desc = std::move(desc), .prepared = false});
  return true;
}

void PrefabRegistry::unregisterPrefab(const std::string& key) {
  auto it = entries_.find(key);
  if (it == entries_.end()) {
    return;
  }
  cleanupEntry(it->second);
  entries_.erase(it);
}

void PrefabRegistry::clear() {
  shutdown();
  entries_.clear();
}

bool PrefabRegistry::hasPrefab(std::string_view key) const {
  return entries_.find(std::string(key)) != entries_.end();
}

const RegisteredPrefabDesc* PrefabRegistry::find(std::string_view key) const {
  const auto it = entries_.find(std::string(key));
  if (it == entries_.end()) {
    return nullptr;
  }
  return &it->second.desc;
}

bool PrefabRegistry::prepare(std::string_view key) {
  auto it = entries_.find(std::string(key));
  if (it == entries_.end()) {
    spdlog::error("Prefab registry prepare failed: unknown key '{}'", key);
    return false;
  }

  Entry& entry = it->second;
  if (entry.prepared) {
    return true;
  }

  if (entry.desc.prepare && !entry.desc.prepare(context_)) {
    spdlog::error("Prefab registry prepare failed: key '{}' setup callback returned false",
                  key);
    return false;
  }

  entry.prepared = true;
  return true;
}

void PrefabRegistry::shutdown() {
  for (auto& [key, entry] : entries_) {
    (void)key;
    cleanupEntry(entry);
  }
}

std::optional<PrefabInstance> PrefabRegistry::instantiate(
    ecs::World& world,
    scene::Scene& scene,
    std::string_view key,
    const PrefabInstantiateDesc& desc) {
  auto it = entries_.find(std::string(key));
  if (it == entries_.end()) {
    spdlog::error("Prefab instantiate failed: unknown registered key '{}'", key);
    return std::nullopt;
  }

  if (!prepare(key)) {
    return std::nullopt;
  }

  return instantiatePrefab(world, scene, it->second.desc.prefab_path, desc);
}

void PrefabRegistry::cleanupEntry(Entry& entry) {
  if (!entry.prepared) {
    return;
  }
  if (entry.desc.cleanup) {
    entry.desc.cleanup(context_);
  }
  entry.prepared = false;
}

std::optional<PrefabInstance> instantiateRegisteredPrefab(
    ecs::World& world,
    scene::Scene& scene,
    PrefabRegistry& registry,
    std::string_view key,
    const PrefabInstantiateDesc& desc) {
  return registry.instantiate(world, scene, key, desc);
}

}  // namespace karma::prefabs
