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

struct PrefabResourceContext {
  renderer::GraphicsDevice* graphics = nullptr;
  particles::ParticleLibrary* particle_effects = nullptr;
  std::function<renderer::TextureId(int, int, const void*)> create_texture_rgba8;
  std::function<void(renderer::TextureId)> destroy_texture;
};

void bindPrefabResourceContext(PrefabResourceContext context);
void clearPrefabResourceContext();

bool ensurePrefabResourcesLoaded(const std::filesystem::path& prefab_path);

}  // namespace karma::prefabs
