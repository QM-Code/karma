#include "karma/visual.h"

#include "karma/visual.h"

namespace karma::visual::volumes {

VolumeRuntimeModule::VolumeRuntimeModule() = default;

VolumeRuntimeModule::~VolumeRuntimeModule() = default;

void VolumeRuntimeModule::onAttach(const app::RuntimeModuleContext& context) {
  system_.reset();
  if (context.graphics != nullptr) {
    system_ = std::make_unique<VolumeSystem>(context.graphics, context.assets);
  }
}

void VolumeRuntimeModule::onDetach() {
  system_.reset();
}

void VolumeRuntimeModule::onUpdate(world::World& world,
                                   float dt,
                                   float interpolation_alpha) {
  if (system_) {
    system_->update(world, dt, interpolation_alpha);
  }
}

}  // namespace karma::visual::volumes
