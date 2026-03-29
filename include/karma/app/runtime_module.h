#pragma once

#include "karma/ecs/world.h"

namespace karma::renderer {
class GraphicsDevice;
class MaterialLibrary;
}  // namespace karma::renderer

namespace karma::particles {
class ParticleLibrary;
}  // namespace karma::particles

namespace karma::prefabs {
class PrefabRegistry;
}  // namespace karma::prefabs

namespace karma::scene {
class Scene;
}  // namespace karma::scene

namespace karma::app {

struct RuntimeModuleContext {
  scene::Scene* scene = nullptr;
  renderer::GraphicsDevice* graphics = nullptr;
  renderer::MaterialLibrary* materials = nullptr;
  particles::ParticleLibrary* particle_effects = nullptr;
  prefabs::PrefabRegistry* prefab_registry = nullptr;
};

class RuntimeModule {
 public:
  virtual ~RuntimeModule() = default;

  virtual void onAttach(const RuntimeModuleContext& context) = 0;
  virtual void onDetach() {}
  virtual void onWarmUp(ecs::World& world) {
    onUpdate(world, 0.0f, 1.0f);
  }
  virtual void onUpdate(ecs::World& world, float dt, float interpolation_alpha) = 0;
};

}  // namespace karma::app
