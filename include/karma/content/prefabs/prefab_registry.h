#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "karma/content/prefabs/prefab_runtime.h"

namespace karma::renderer {
class GraphicsDevice;
class MaterialLibrary;
}  // namespace karma::renderer

namespace karma::particles {
class ParticleLibrary;
}  // namespace karma::particles

namespace karma::prefabs {

struct PrefabPackageContext {
  renderer::GraphicsDevice* graphics = nullptr;
  renderer::MaterialLibrary* materials = nullptr;
  particles::ParticleLibrary* particle_effects = nullptr;
};

using PrefabPrepareCallback = std::function<bool(const PrefabPackageContext&)>;
using PrefabCleanupCallback = std::function<void(const PrefabPackageContext&)>;

struct RegisteredPrefabDesc {
  std::filesystem::path prefab_path;
  PrefabPrepareCallback prepare;
  PrefabCleanupCallback cleanup;
};

class PrefabRegistry {
 public:
  PrefabRegistry() = default;
  ~PrefabRegistry();

  void bindContext(const PrefabPackageContext& context);
  void clearContext();

  bool registerPrefab(const std::string& key, RegisteredPrefabDesc desc);
  void unregisterPrefab(const std::string& key);
  void clear();
  bool hasPrefab(std::string_view key) const;

  const RegisteredPrefabDesc* find(std::string_view key) const;
  bool prepare(std::string_view key);
  void shutdown();

  std::optional<PrefabInstance> instantiate(
      ecs::World& world,
      std::string_view key,
      const PrefabInstantiateDesc& desc = {});

 private:
  struct Entry {
    RegisteredPrefabDesc desc;
    bool prepared = false;
  };

  void cleanupEntry(Entry& entry);

  std::unordered_map<std::string, Entry> entries_;
  PrefabPackageContext context_{};
};

std::optional<PrefabInstance> instantiateRegisteredPrefab(
    ecs::World& world,
    PrefabRegistry& registry,
    std::string_view key,
    const PrefabInstantiateDesc& desc = {});

inline std::optional<PrefabInstance> instantiatePrefab(
    ecs::World& world,
    PrefabRegistry& registry,
    std::string_view key,
    const PrefabInstantiateDesc& desc = {}) {
  return registry.instantiate(world, key, desc);
}

}  // namespace karma::prefabs
