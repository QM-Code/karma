#pragma once

#include "karma/world/ecs/world.h"

namespace karma::renderer {
class GraphicsDevice;
class MaterialLibrary;
}  // namespace karma::renderer

namespace karma::particles {
class ParticleLibrary;
}  // namespace karma::particles

namespace karma::scene {
class Scene;
}  // namespace karma::scene

namespace karma::app {

/// \ingroup karma_runtime
/// Borrowed subsystem pointers provided to runtime modules on attach.
struct RuntimeModuleContext {
  scene::Scene* scene = nullptr;
  renderer::GraphicsDevice* graphics = nullptr;
  renderer::MaterialLibrary* materials = nullptr;
  particles::ParticleLibrary* particle_effects = nullptr;
};

/// \ingroup karma_runtime
/// Optional runtime feature hook owned by `EngineApp`.
///
/// Runtime modules are the extension point for feature systems that need per
/// frame updates or renderer/resource warmup but should not be hardwired into
/// the core app startup path.
class RuntimeModule {
 public:
  virtual ~RuntimeModule() = default;

  /// Called when the module is registered with an initialized engine context.
  virtual void onAttach(const RuntimeModuleContext& context) = 0;
  /// Called before the module is destroyed or the engine shuts down.
  virtual void onDetach() {}
  /// Called during renderer warmup.
  virtual void onWarmUp(ecs::World& world) {
    onUpdate(world, 0.0f, 1.0f);
  }
  /// Called once per rendered frame.
  virtual void onUpdate(ecs::World& world, float dt, float interpolation_alpha) = 0;
};

}  // namespace karma::app
