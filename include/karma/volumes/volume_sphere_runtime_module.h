#pragma once

#include <memory>

#include "karma/app/runtime_module.h"

namespace karma::volumes {

class VolumeSphereSystem;

class VolumeSphereRuntimeModule final : public app::RuntimeModule {
 public:
  VolumeSphereRuntimeModule();
  ~VolumeSphereRuntimeModule() override;

  VolumeSphereRuntimeModule(const VolumeSphereRuntimeModule&) = delete;
  VolumeSphereRuntimeModule& operator=(const VolumeSphereRuntimeModule&) = delete;

  void onAttach(const app::RuntimeModuleContext& context) override;
  void onDetach() override;
  void onUpdate(ecs::World& world, float dt, float interpolation_alpha) override;

 private:
  std::unique_ptr<VolumeSphereSystem> system_;
};

}  // namespace karma::volumes
