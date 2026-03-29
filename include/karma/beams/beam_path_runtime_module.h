#pragma once

#include <memory>

#include "karma/app/runtime_module.h"

namespace karma::beams {

class BeamPathSystem;

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
