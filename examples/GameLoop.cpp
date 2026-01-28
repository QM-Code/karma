#include "karma/app/engine_app.h"

namespace karma::demo {

class DemoGame : public app::GameInterface {
 public:
  void onStart(ecs::World& world, scene::Scene& scene, input::InputSystem& input,
               physics::World& physics) override {
    // Build initial scene/entities here.
  }

  void onFixedUpdate(ecs::World& world, scene::Scene& scene, input::InputSystem& input,
                     physics::World& physics, float dt) override {
    // Deterministic simulation step.
  }

  void onUpdate(ecs::World& world, scene::Scene& scene, input::InputSystem& input,
                physics::World& physics, float dt) override {
    // Frame-rate-dependent logic.
  }

  void onShutdown() override {
    // Cleanup if needed.
  }
};

// Pseudocode: game owns the loop, engine owns lifecycle + subsystems.
void RunGameLoop() {
  app::EngineApp engine;
  DemoGame game;

  app::EngineConfig config;
  config.window.title = "Karma Demo";
  config.window.samples = 8;
  config.window.icon_path = "icons/karma.png";
  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
}

}  // namespace karma::demo
