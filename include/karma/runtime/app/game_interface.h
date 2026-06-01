#pragma once

#include "karma/world/ecs/world.h"
#include "karma/runtime/input/input_system.h"
#include "karma/simulation/physics/physics_world.hpp"
#include "karma/content/prefabs/prefab_registry.h"
#include "karma/rendering/renderer/device.h"
#include "karma/rendering/renderer/material_library.h"
#include "karma/features/visual/particles/effect_library.h"
#include "karma/world/scene/scene.h"
#include "karma/world/systems/system_graph.h"

namespace karma::app {

class GameInterface {
 public:
  virtual ~GameInterface() = default;

  virtual void onStart() = 0;
  virtual void onFixedUpdate(float dt) = 0;
  virtual void onPostFixedUpdate(float dt) { (void)dt; }
  virtual void onUpdate(float dt) = 0;
  virtual void onShutdown() = 0;

 protected:
  float renderInterpolationAlpha() const { return render_interpolation_alpha_; }
  ecs::World* world = nullptr;
  scene::Scene* scene = nullptr;
  input::InputSystem* input = nullptr;
  physics::World* physics = nullptr;
  renderer::GraphicsDevice* graphics = nullptr;
  renderer::MaterialLibrary* materials = nullptr;
  particles::ParticleLibrary* particle_effects = nullptr;
  prefabs::PrefabRegistry* prefab_registry = nullptr;
  systems::SystemGraph* systems = nullptr;

  private:
  friend class EngineApp;
  void bindContext(ecs::World& world, scene::Scene& scene, input::InputSystem& input,
                   physics::World& physics, renderer::GraphicsDevice* graphics,
                   renderer::MaterialLibrary& materials,
                   particles::ParticleLibrary& particle_effects,
                   prefabs::PrefabRegistry& prefab_registry,
                   systems::SystemGraph& systems) {
    this->world = &world;
    this->scene = &scene;
    this->input = &input;
    this->physics = &physics;
    this->graphics = graphics;
    this->materials = &materials;
    this->particle_effects = &particle_effects;
    this->prefab_registry = &prefab_registry;
    this->systems = &systems;
  }

  float render_interpolation_alpha_ = 1.0f;
};

}  // namespace karma::app
