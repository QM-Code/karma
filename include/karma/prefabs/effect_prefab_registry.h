#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "karma/prefabs/effect_prefab.h"

namespace karma::renderer {
class GraphicsDevice;
class MaterialLibrary;
}  // namespace karma::renderer

namespace karma::particles {
class ParticleLibrary;
}  // namespace karma::particles

namespace karma::prefabs {

struct EffectPrefabPackageContext {
  renderer::GraphicsDevice* graphics = nullptr;
  renderer::MaterialLibrary* materials = nullptr;
  particles::ParticleLibrary* particle_effects = nullptr;
};

using EffectPrefabPrepareCallback = std::function<bool(const EffectPrefabPackageContext&)>;
using EffectPrefabCleanupCallback = std::function<void(const EffectPrefabPackageContext&)>;

struct RegisteredEffectPrefabDesc {
  std::filesystem::path prefab_path;
  EffectPrefabPrepareCallback prepare;
  EffectPrefabCleanupCallback cleanup;
};

class EffectPrefabRegistry {
 public:
  EffectPrefabRegistry() = default;
  ~EffectPrefabRegistry();

  void bindContext(const EffectPrefabPackageContext& context);
  void clearContext();

  bool registerPrefab(const std::string& key, RegisteredEffectPrefabDesc desc);
  void unregisterPrefab(const std::string& key);
  void clear();
  bool hasPrefab(std::string_view key) const;

  const RegisteredEffectPrefabDesc* find(std::string_view key) const;
  bool prepare(std::string_view key);
  void shutdown();

  std::optional<EffectPrefabInstance> instantiate(
      ecs::World& world,
      std::string_view key,
      const EffectPrefabInstantiateDesc& desc = {});

 private:
  struct Entry {
    RegisteredEffectPrefabDesc desc;
    bool prepared = false;
  };

  void cleanupEntry(Entry& entry);

  std::unordered_map<std::string, Entry> entries_;
  EffectPrefabPackageContext context_{};
};

std::optional<EffectPrefabInstance> instantiateRegisteredPrefab(
    ecs::World& world,
    EffectPrefabRegistry& registry,
    std::string_view key,
    const EffectPrefabInstantiateDesc& desc = {});

using PrefabPackageContext = EffectPrefabPackageContext;
using PrefabPrepareCallback = EffectPrefabPrepareCallback;
using PrefabCleanupCallback = EffectPrefabCleanupCallback;
using RegisteredPrefabDesc = RegisteredEffectPrefabDesc;
using PrefabRegistry = EffectPrefabRegistry;

inline std::optional<PrefabInstance> instantiatePrefab(
    ecs::World& world,
    PrefabRegistry& registry,
    std::string_view key,
    const PrefabInstantiateDesc& desc = {}) {
  return registry.instantiate(world, key, desc);
}

}  // namespace karma::prefabs
