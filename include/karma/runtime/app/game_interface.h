#pragma once

#include "karma/content/assets/asset_registry.h"
#include "karma/world/ecs/world.h"
#include "karma/runtime/input/input_system.h"
#include "karma/simulation/physics/physics_world.hpp"
#include "karma/rendering/renderer/device.h"
#include "karma/world/scene/scene.h"
#include "karma/world/systems/system_graph.h"

namespace karma::app {

/// \ingroup karma_runtime
/// Base class implemented by game/application code.
///
/// `EngineApp` binds protected subsystem pointers before `onStart()` and clears
/// ownership at shutdown. The pointers are borrowed; do not store them beyond
/// the lifetime of the game object and engine.
class GameInterface {
 public:
  virtual ~GameInterface() = default;

  /// Called once after engine subsystems are initialized.
  virtual void onStart() = 0;
  /// Called zero or more times per frame at fixed timestep.
  virtual void onFixedUpdate(float dt) = 0;
  /// Called after fixed physics/simulation work for each fixed step.
  virtual void onPostFixedUpdate(float dt) { (void)dt; }
  /// Called once per rendered frame.
  virtual void onUpdate(float dt) = 0;
  /// Called during engine shutdown.
  virtual void onShutdown() = 0;

 protected:
  /// Interpolation alpha for rendering between fixed simulation steps.
  float renderInterpolationAlpha() const { return render_interpolation_alpha_; }
  /// Borrowed ECS world.
  ecs::World* world = nullptr;
  /// Borrowed scene hierarchy.
  scene::Scene* scene = nullptr;
  /// Borrowed input system.
  input::InputSystem* input = nullptr;
  /// Borrowed physics world.
  physics::World* physics = nullptr;
  /// Borrowed graphics device. Null in headless builds.
  renderer::GraphicsDevice* graphics = nullptr;
  /// Borrowed normalized runtime asset registry.
  content::AssetRegistry* assets = nullptr;
  /// Borrowed optional system graph.
  systems::SystemGraph* systems = nullptr;

 private:
  friend class EngineApp;
  void bindContext(ecs::World& world, scene::Scene& scene, input::InputSystem& input,
                   physics::World& physics, renderer::GraphicsDevice* graphics,
                   content::AssetRegistry& assets,
                   systems::SystemGraph& systems) {
    this->world = &world;
    this->scene = &scene;
    this->input = &input;
    this->physics = &physics;
    this->graphics = graphics;
    this->assets = &assets;
    this->systems = &systems;
  }

  float render_interpolation_alpha_ = 1.0f;
};

}  // namespace karma::app
