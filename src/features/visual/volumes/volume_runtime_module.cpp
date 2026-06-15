#include "karma/features/visual/volumes/volume_runtime_module.h"

#include "karma/features/visual/volumes/volume_system.h"

namespace karma::volumes {

VolumeRuntimeModule::VolumeRuntimeModule() = default;

VolumeRuntimeModule::~VolumeRuntimeModule() = default;

void VolumeRuntimeModule::onAttach(const app::RuntimeModuleContext& context) {
  system_.reset();
  if (context.graphics != nullptr) {
    system_ = std::make_unique<VolumeSystem>(context.graphics);
  }
}

void VolumeRuntimeModule::onDetach() {
  system_.reset();
}

void VolumeRuntimeModule::onUpdate(ecs::World& world,
                                   float dt,
                                   float interpolation_alpha) {
  if (system_) {
    system_->update(world, dt, interpolation_alpha);
  }
}

}  // namespace karma::volumes
