#include "karma/features/visual/terrain/terrain_runtime_module.h"

#include "karma/features/visual/terrain/terrain_system.h"

namespace karma::terrain {

TerrainRuntimeModule::TerrainRuntimeModule() = default;

TerrainRuntimeModule::~TerrainRuntimeModule() = default;

void TerrainRuntimeModule::onAttach(const app::RuntimeModuleContext& context) {
  system_.reset();
  system_ = std::make_unique<TerrainSystem>(context.graphics, context.assets);
}

void TerrainRuntimeModule::onDetach() {
  system_.reset();
}

void TerrainRuntimeModule::onFrameBegin(ecs::World& world, float) {
  if (system_) {
    system_->syncTerrainColliders(world);
  }
}

void TerrainRuntimeModule::onUpdate(ecs::World& world,
                                    float dt,
                                    float interpolation_alpha) {
  if (system_) {
    system_->update(world, dt, interpolation_alpha);
  }
}

}  // namespace karma::terrain
