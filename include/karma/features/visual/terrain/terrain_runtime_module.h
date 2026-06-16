#pragma once

#include <memory>

#include "karma/runtime/app/runtime_module.h"

namespace karma::terrain {

class TerrainSystem;

/// Runtime module that owns `TerrainSystem`.
class TerrainRuntimeModule final : public app::RuntimeModule {
 public:
  TerrainRuntimeModule();
  ~TerrainRuntimeModule() override;

  TerrainRuntimeModule(const TerrainRuntimeModule&) = delete;
  TerrainRuntimeModule& operator=(const TerrainRuntimeModule&) = delete;

  void onAttach(const app::RuntimeModuleContext& context) override;
  void onDetach() override;
  void onUpdate(ecs::World& world, float dt, float interpolation_alpha) override;

 private:
  std::unique_ptr<TerrainSystem> system_;
};

}  // namespace karma::terrain
