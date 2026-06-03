#pragma once

#include <filesystem>
#include <functional>

#include "karma/rendering/renderer/ids.h"

namespace karma::particles {
class ParticleLibrary;
}  // namespace karma::particles

namespace karma::renderer {
class GraphicsDevice;
}  // namespace karma::renderer

namespace karma::prefabs {

/// \ingroup karma_prefabs
/// Runtime resource services used by prefab sidecar loading.
struct PrefabResourceContext {
  renderer::GraphicsDevice* graphics = nullptr;
  particles::ParticleLibrary* particle_effects = nullptr;
  std::function<renderer::TextureId(int, int, const void*)> create_texture_rgba8;
  std::function<void(renderer::TextureId)> destroy_texture;
};

/// Binds global services used by `ensurePrefabResourcesLoaded`.
void bindPrefabResourceContext(PrefabResourceContext context);
/// Clears global prefab resource services.
void clearPrefabResourceContext();

/// Loads resources declared beside a prefab path.
bool ensurePrefabResourcesLoaded(const std::filesystem::path& prefab_path);

}  // namespace karma::prefabs
