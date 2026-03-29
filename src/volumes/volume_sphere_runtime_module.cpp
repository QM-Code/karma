#include "karma/volumes/volume_sphere_runtime_module.h"

#include "karma/components/transform.h"
#include "karma/volumes/volume_sphere_system.h"
#include "karma/prefabs/prefab_entry_handler.h"

#include "../prefabs/prefab_binding_utils.h"

namespace karma::volumes {

namespace {

ecs::Entity instantiateVolumeSpherePrefabEntry(
    const prefabs::PrefabEntryHandlerContext& context) {
  ecs::Entity entity = context.world.createEntity();
  if (!context.entity_name.empty()) {
    context.world.setName(entity, context.entity_name);
  }
  context.world.add(entity, context.world_transform);

  components::VolumeSphereComponent sphere = context.entry.volume_sphere.volume;
  if (context.entry.volume_sphere.color_binding.enabled) {
    sphere.color = prefabs::detail::resolveColorBinding(
        context.entry.volume_sphere.color_binding,
        context.resolved_params,
        sphere.color);
  }
  if (context.entry.volume_sphere.emissive_color_binding.enabled) {
    sphere.emissive_color = prefabs::detail::resolveColorBinding(
        context.entry.volume_sphere.emissive_color_binding,
        context.resolved_params,
        sphere.emissive_color);
  }
  if (context.entry.volume_sphere.radius_binding.enabled) {
    sphere.radius = prefabs::detail::resolveFloatBinding(
        context.entry.volume_sphere.radius_binding,
        context.resolved_params,
        sphere.radius);
  }
  if (context.entry.volume_sphere.center_opacity_binding.enabled) {
    sphere.center_opacity = prefabs::detail::resolveFloatBinding(
        context.entry.volume_sphere.center_opacity_binding,
        context.resolved_params,
        sphere.center_opacity);
  }
  if (context.entry.volume_sphere.distortion_strength_binding.enabled) {
    sphere.distortion_strength = prefabs::detail::resolveFloatBinding(
        context.entry.volume_sphere.distortion_strength_binding,
        context.resolved_params,
        sphere.distortion_strength);
  }
  if (context.entry.volume_sphere.noise_strength_binding.enabled) {
    sphere.noise_strength = prefabs::detail::resolveFloatBinding(
        context.entry.volume_sphere.noise_strength_binding,
        context.resolved_params,
        sphere.noise_strength);
  }
  if (context.entry.volume_sphere.overlay_depth_binding.enabled) {
    sphere.overlay_depth = prefabs::detail::resolveFloatBinding(
        context.entry.volume_sphere.overlay_depth_binding,
        context.resolved_params,
        sphere.overlay_depth);
  }

  context.world.add(entity, sphere);
  return entity;
}

}  // namespace

VolumeSphereRuntimeModule::VolumeSphereRuntimeModule() = default;

VolumeSphereRuntimeModule::~VolumeSphereRuntimeModule() = default;

void VolumeSphereRuntimeModule::onAttach(const app::RuntimeModuleContext& context) {
  system_.reset();
  if (context.graphics != nullptr) {
    system_ = std::make_unique<VolumeSphereSystem>(context.graphics);
  }
  prefabs::registerPrefabEntryHandler(prefabs::PrefabEntry::Type::VolumeSphere,
                                      instantiateVolumeSpherePrefabEntry);
}

void VolumeSphereRuntimeModule::onDetach() {
  prefabs::unregisterPrefabEntryHandler(prefabs::PrefabEntry::Type::VolumeSphere);
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
