#pragma once

#include <memory>

#include "karma/runtime/app/runtime_module.h"

namespace karma::beams {

class BeamPathSystem;

/// \ingroup karma_beams
/// Runtime module that owns `BeamPathSystem`.
///
/// Register with `EngineApp::addRuntimeModule(...)` before startup when a scene
/// contains `BeamPathComponent` entities.
class BeamPathRuntimeModule final : public app::RuntimeModule {
 public:
  BeamPathRuntimeModule();
  ~BeamPathRuntimeModule() override;

  BeamPathRuntimeModule(const BeamPathRuntimeModule&) = delete;
  BeamPathRuntimeModule& operator=(const BeamPathRuntimeModule&) = delete;

  void onAttach(const app::RuntimeModuleContext& context) override;
  void onDetach() override;
  void onUpdate(ecs::World& world, float dt, float interpolation_alpha) override;

 private:
  std::unique_ptr<BeamPathSystem> system_;
};

}  // namespace karma::beams
