#include "karma/prefabs/effect_prefab_registry.h"

#include <spdlog/spdlog.h>

namespace karma::prefabs {

EffectPrefabRegistry::~EffectPrefabRegistry() {
  shutdown();
}

void EffectPrefabRegistry::bindContext(const EffectPrefabPackageContext& context) {
  context_ = context;
}

void EffectPrefabRegistry::clearContext() {
  context_ = {};
}

bool EffectPrefabRegistry::registerPrefab(const std::string& key, RegisteredEffectPrefabDesc desc) {
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

void EffectPrefabRegistry::unregisterPrefab(const std::string& key) {
  auto it = entries_.find(key);
  if (it == entries_.end()) {
    return;
  }
  cleanupEntry(it->second);
  entries_.erase(it);
}

void EffectPrefabRegistry::clear() {
  shutdown();
  entries_.clear();
}

bool EffectPrefabRegistry::hasPrefab(std::string_view key) const {
  return entries_.find(std::string(key)) != entries_.end();
}

const RegisteredEffectPrefabDesc* EffectPrefabRegistry::find(std::string_view key) const {
  const auto it = entries_.find(std::string(key));
  if (it == entries_.end()) {
    return nullptr;
  }
  return &it->second.desc;
}

bool EffectPrefabRegistry::prepare(std::string_view key) {
  auto it = entries_.find(std::string(key));
  if (it == entries_.end()) {
    spdlog::error("Effect prefab registry prepare failed: unknown key '{}'", key);
    return false;
  }

  Entry& entry = it->second;
  if (entry.prepared) {
    return true;
  }

  if (entry.desc.prepare && !entry.desc.prepare(context_)) {
    spdlog::error("Effect prefab registry prepare failed: key '{}' setup callback returned false",
                  key);
    return false;
  }

  entry.prepared = true;
  return true;
}

void EffectPrefabRegistry::shutdown() {
  for (auto& [key, entry] : entries_) {
    (void)key;
    cleanupEntry(entry);
  }
}

std::optional<EffectPrefabInstance> EffectPrefabRegistry::instantiate(
    ecs::World& world,
    std::string_view key,
    const EffectPrefabInstantiateDesc& desc) {
  auto it = entries_.find(std::string(key));
  if (it == entries_.end()) {
    spdlog::error("Effect prefab instantiate failed: unknown registered key '{}'", key);
    return std::nullopt;
  }

  if (!prepare(key)) {
    return std::nullopt;
  }

  return instantiateEffectPrefab(world, context_.graphics, it->second.desc.prefab_path, desc);
}

void EffectPrefabRegistry::cleanupEntry(Entry& entry) {
  if (!entry.prepared) {
    return;
  }
  if (entry.desc.cleanup) {
    entry.desc.cleanup(context_);
  }
  entry.prepared = false;
}

std::optional<EffectPrefabInstance> instantiateRegisteredPrefab(
    ecs::World& world,
    EffectPrefabRegistry& registry,
    std::string_view key,
    const EffectPrefabInstantiateDesc& desc) {
  return registry.instantiate(world, key, desc);
}

}  // namespace karma::prefabs
