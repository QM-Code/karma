#include "karma/features/visual/beams/beam_path_runtime_module.h"

#include "karma/features/visual/beams/beam_path_api.h"
#include "karma/features/visual/beams/beam_path_system.h"
#include "karma/content/prefabs/prefab_entry_handler.h"

#include "../../../content/prefabs/prefab_binding_utils.h"

namespace karma::beams {

namespace {

ecs::Entity instantiateBeamPrefabEntry(const prefabs::PrefabEntryHandlerContext& context) {
  components::BeamPathComponent beam = context.entry.beam.beam;
  if (context.entry.beam.core_color_binding.enabled) {
    beam.core_color = prefabs::detail::resolveColorBinding(
        context.entry.beam.core_color_binding,
        context.resolved_params,
        beam.core_color);
  }
  if (context.entry.beam.glow_color_binding.enabled) {
    beam.glow_color = prefabs::detail::resolveColorBinding(
        context.entry.beam.glow_color_binding,
        context.resolved_params,
        beam.glow_color);
  }

  return createBeamPathEntity(
      context.world,
      BeamPathEntityDesc{
          .name = context.entity_name,
          .transform = context.world_transform,
          .beam = std::move(beam),
      });
}

}  // namespace

BeamPathRuntimeModule::BeamPathRuntimeModule() = default;

BeamPathRuntimeModule::~BeamPathRuntimeModule() = default;

void BeamPathRuntimeModule::onAttach(const app::RuntimeModuleContext& context) {
  system_.reset();
  if (context.graphics != nullptr) {
    system_ = std::make_unique<BeamPathSystem>(context.graphics);
  }
  prefabs::registerPrefabEntryHandler(prefabs::PrefabEntry::Type::Beam,
                                      instantiateBeamPrefabEntry);
}

void BeamPathRuntimeModule::onDetach() {
  prefabs::unregisterPrefabEntryHandler(prefabs::PrefabEntry::Type::Beam);
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
