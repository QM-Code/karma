#include "karma/features/visual/beams/beam_path_runtime_module.h"

#include "karma/features/visual/beams/beam_path_system.h"

namespace karma::beams {

BeamPathRuntimeModule::BeamPathRuntimeModule() = default;

BeamPathRuntimeModule::~BeamPathRuntimeModule() = default;

void BeamPathRuntimeModule::onAttach(const app::RuntimeModuleContext& context) {
  system_.reset();
  if (context.graphics != nullptr) {
    system_ = std::make_unique<BeamPathSystem>(context.graphics);
  }
}

void BeamPathRuntimeModule::onDetach() {
  system_.reset();
}

void BeamPathRuntimeModule::onUpdate(ecs::World& world,
                                     float dt,
                                     float interpolation_alpha) {
  if (system_) {
    system_->update(world, dt, interpolation_alpha);
  }
}

}  // namespace karma::beams
