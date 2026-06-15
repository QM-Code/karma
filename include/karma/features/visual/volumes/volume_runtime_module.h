#pragma once

#include <memory>

#include "karma/runtime/app/runtime_module.h"

namespace karma::volumes {

class VolumeSystem;

/// \ingroup karma_volumes
/// Runtime module that owns `VolumeSystem`.
class VolumeRuntimeModule final : public app::RuntimeModule {
 public:
  VolumeRuntimeModule();
  ~VolumeRuntimeModule() override;

  VolumeRuntimeModule(const VolumeRuntimeModule&) = delete;
  VolumeRuntimeModule& operator=(const VolumeRuntimeModule&) = delete;

  void onAttach(const app::RuntimeModuleContext& context) override;
  void onDetach() override;
  void onUpdate(ecs::World& world, float dt, float interpolation_alpha) override;

 private:
  std::unique_ptr<VolumeSystem> system_;
};

}  // namespace karma::volumes
