#include "karma/features/visual/volumes/volume_sphere_runtime_module.h"

#include "karma/features/visual/volumes/volume_sphere_system.h"

namespace karma::volumes {

VolumeSphereRuntimeModule::VolumeSphereRuntimeModule() = default;

VolumeSphereRuntimeModule::~VolumeSphereRuntimeModule() = default;

void VolumeSphereRuntimeModule::onAttach(const app::RuntimeModuleContext& context) {
  system_.reset();
  if (context.graphics != nullptr) {
    system_ = std::make_unique<VolumeSphereSystem>(context.graphics);
  }
}

void VolumeSphereRuntimeModule::onDetach() {
  system_.reset();
}

void VolumeSphereRuntimeModule::onUpdate(ecs::World& world,
                                         float dt,
                                         float interpolation_alpha) {
  if (system_) {
    system_->update(world, dt, interpolation_alpha);
  }
}

}  // namespace karma::volumes
