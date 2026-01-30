#pragma once

#include "karma/ecs/world.h"
#include "karma/input/input_system.h"
#include "karma/physics/physics_world.hpp"
#include "karma/renderer/device.h"
#include "karma/scene/scene.h"

namespace karma::app {

class GameInterface {
 public:
  virtual ~GameInterface() = default;

  virtual void onStart() = 0;
  virtual void onFixedUpdate(float dt) = 0;
  virtual void onUpdate(float dt) = 0;
  virtual void onShutdown() = 0;

 protected:
  ecs::World* world = nullptr;
  scene::Scene* scene = nullptr;
  input::InputSystem* input = nullptr;
  physics::World* physics = nullptr;
  renderer::GraphicsDevice* graphics = nullptr;

 private:
  friend class EngineApp;
  void bindContext(ecs::World& world, scene::Scene& scene, input::InputSystem& input,
                   physics::World& physics, renderer::GraphicsDevice* graphics) {
    this->world = &world;
    this->scene = &scene;
    this->input = &input;
    this->physics = &physics;
    this->graphics = graphics;
  }
};

}  // namespace karma::app
