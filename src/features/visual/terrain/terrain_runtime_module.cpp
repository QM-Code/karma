#include "karma/visual.h"

#include "karma/visual.h"

namespace karma::visual::terrain {

TerrainRuntimeModule::TerrainRuntimeModule() = default;

TerrainRuntimeModule::~TerrainRuntimeModule() = default;

void TerrainRuntimeModule::onAttach(const app::RuntimeModuleContext& context) {
  system_.reset();
  system_ = std::make_unique<TerrainSystem>(context.graphics, context.assets);
}

void TerrainRuntimeModule::onDetach() {
  system_.reset();
}

void TerrainRuntimeModule::onFrameBegin(world::World& world, float) {
  if (system_) {
    system_->syncTerrainColliders(world);
  }
}

void TerrainRuntimeModule::onUpdate(world::World& world,
                                    float dt,
                                    float interpolation_alpha) {
  if (system_) {
    system_->update(world, dt, interpolation_alpha);
  }
}

}  // namespace karma::visual::terrain
