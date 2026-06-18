#include "karma/rendering/renderer/render_system.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <variant>
#include <vector>
#include <spdlog/spdlog.h>

#include "karma/core/time.h"
#include "render_system/debug_draw.h"
#include "render_system/extractors.h"
#include "karma/world/components/camera.h"
#include "karma/world/components/collider.h"
#include "karma/world/components/environment.h"
#include "karma/world/components/light.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/visibility.h"

namespace karma::renderer {

namespace {
using render_system::drawBoxWire;
using render_system::drawCapsuleWire;
using render_system::drawSphereWire;
using render_system::toCameraData;
using render_system::toDirectionalLight;
using render_system::toLightData;
using render_system::toTransform;

bool envFlagEnabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") != 0 &&
         std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "FALSE") != 0 &&
         std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "OFF") != 0;
}

bool renderSystemDiagEnabled() {
  static const bool enabled = envFlagEnabled(std::getenv("KARMA_RENDER_SYSTEM_DIAG"));
  return enabled;
}

bool renderSystemDiagEveryFrameEnabled() {
  static const bool enabled =
      envFlagEnabled(std::getenv("KARMA_RENDER_SYSTEM_DIAG_EVERY_FRAME"));
  return enabled;
}

void logRenderSystemStage(const bool enabled,
                          const char* name,
                          const core::SteadyClock::time_point start,
                          const core::SteadyClock::time_point end) {
  if (enabled) {
    spdlog::info("RenderSystem stage '{}' took {:.2f} ms",
                 name,
                 core::elapsedMilliseconds(start, end));
  }
}

renderer::PostProcessSettings resolvePostProcessSettings(
    const renderer::PostProcessProfileLibrary* profiles,
    const std::string& key) {
  if (profiles == nullptr) {
    return {};
  }
  return profiles->resolve(key);
}

}

void RenderSystem::releaseRecord(uint64_t key, RenderRecord& record) {
  device_.retireInstance(static_cast<InstanceId>(key));
  releaseMaterialBinding(record);
  releaseMeshBinding(record);
}

void RenderSystem::cleanupStaleRecords(ecs::World& world) {
  for (auto it = records_.begin(); it != records_.end();) {
    const ecs::Entity entity = entityFromKey(it->first);
    const bool stale = !world.isAlive(entity) ||
                       !world.has<components::MeshComponent>(entity) ||
                       !world.has<components::TransformComponent>(entity);
    if (!stale) {
      ++it;
      continue;
    }
    releaseRecord(it->first, it->second);
    it = records_.erase(it);
  }
}

void RenderSystem::releaseMeshBinding(RenderRecord& record) {
  releaseSharedMesh(record.mesh_key);
  record.mesh_key.clear();
  record.material_slots.clear();

  record.mesh = renderer::kInvalidMesh;
  record.bounds_center = glm::vec3(0.0f);
  record.bounds_radius = 0.0f;
  record.bounds_valid = false;
}

void RenderSystem::releaseMaterialBinding(RenderRecord& record) {
  for (const std::string& material_key : record.acquired_material_keys) {
    releaseSharedMaterial(material_key);
  }
  record.acquired_material_keys.clear();
  record.material_bindings.clear();
}

void RenderSystem::bindMesh(const components::MeshComponent& mesh, RenderRecord& record) {
  record.mesh_key = mesh.mesh_key;
  acquireSharedMesh(mesh.mesh_key, record);
  record.material_slots.clear();
  if (record.mesh != renderer::kInvalidMesh) {
    device_.getMeshMaterialSlots(record.mesh, record.material_slots);
  }
}

void RenderSystem::bindMaterial(const components::MeshComponent& mesh, RenderRecord& record) {
  record.component_materials = mesh.materials;
  record.material_bindings.clear();
  record.acquired_material_keys.clear();

  uint32_t slot_count = static_cast<uint32_t>(record.material_slots.size());
  for (const auto& binding : mesh.materials) {
    slot_count = std::max(slot_count, binding.slot + 1u);
  }

  auto override_for_slot = [&](uint32_t slot) -> const std::string* {
    for (const auto& binding : mesh.materials) {
      if (binding.slot == slot && !binding.material_key.empty()) {
        return &binding.material_key;
      }
    }
    return nullptr;
  };

  for (uint32_t slot = 0; slot < slot_count; ++slot) {
    const std::string* material_key = override_for_slot(slot);
    const std::string* fallback_key =
        slot < record.material_slots.size() &&
                !record.material_slots[slot].default_material_key.empty()
            ? &record.material_slots[slot].default_material_key
            : nullptr;

    renderer::MaterialId material = renderer::kInvalidMaterial;
    const std::string* acquired_key = nullptr;
    if (material_key != nullptr) {
      material = acquireSharedMaterial(*material_key);
      if (material != renderer::kInvalidMaterial) {
        acquired_key = material_key;
      }
    }
    if (material == renderer::kInvalidMaterial && fallback_key != nullptr &&
        (material_key == nullptr || *fallback_key != *material_key)) {
      material = acquireSharedMaterial(*fallback_key);
      if (material != renderer::kInvalidMaterial) {
        acquired_key = fallback_key;
      }
    }
    if (material == renderer::kInvalidMaterial) {
      continue;
    }
    record.acquired_material_keys.push_back(*acquired_key);
    record.material_bindings.push_back(renderer::DrawMaterialBinding{
        .slot = slot,
        .material = material,
    });
  }
}

void RenderSystem::acquireSharedMesh(const std::string& mesh_key, RenderRecord& record) {
  record.mesh = renderer::kInvalidMesh;
  record.bounds_center = glm::vec3(0.0f);
  record.bounds_radius = 0.0f;
  record.bounds_valid = false;
  if (mesh_key.empty()) {
    return;
  }

  auto shared_it = shared_meshes_.find(mesh_key);
  if (shared_it == shared_meshes_.end()) {
    SharedMeshResource shared{};
    shared.mesh = device_.findRuntimeMesh(mesh_key);
    shared.owned_by_render_system = shared.mesh == renderer::kInvalidMesh;
    if (shared.owned_by_render_system) {
      shared.mesh = device_.createMeshFromFile(mesh_key);
    }
    if (shared.mesh != renderer::kInvalidMesh) {
      shared.bounds_valid =
          device_.getMeshBounds(shared.mesh, shared.bounds_center, shared.bounds_radius);
    }
    shared.ref_count = 1;
    shared_it = shared_meshes_.emplace(mesh_key, std::move(shared)).first;
  } else {
    shared_it->second.ref_count += 1;
  }

  record.mesh = shared_it->second.mesh;
  record.bounds_center = shared_it->second.bounds_center;
  record.bounds_radius = shared_it->second.bounds_radius;
  record.bounds_valid = shared_it->second.bounds_valid;
}

void RenderSystem::releaseSharedMesh(const std::string& mesh_key) {
  if (mesh_key.empty()) {
    return;
  }
  auto shared_it = shared_meshes_.find(mesh_key);
  if (shared_it == shared_meshes_.end()) {
    return;
  }
  if (shared_it->second.ref_count > 0) {
    shared_it->second.ref_count -= 1;
  }
  if (shared_it->second.ref_count == 0) {
    if (shared_it->second.owned_by_render_system &&
        shared_it->second.mesh != renderer::kInvalidMesh) {
      device_.destroyMesh(shared_it->second.mesh);
    }
    shared_meshes_.erase(shared_it);
  }
}

renderer::MaterialId RenderSystem::acquireSharedMaterial(const std::string& material_key) {
  if (material_key.empty() || material_library_ == nullptr) {
    return renderer::kInvalidMaterial;
  }

  const bool diag_enabled = renderSystemDiagEnabled();
  const auto cache_start = core::SteadyClock::now();
  auto shared_it = shared_materials_.find(material_key);
  if (shared_it == shared_materials_.end()) {
    auto resolved = material_library_->resolve(material_key);
    if (!resolved.has_value()) {
      if (!warned_missing_material_keys_.contains(material_key)) {
        spdlog::warn("Karma: material key '{}' was not registered; using mesh slot default",
                     material_key);
        warned_missing_material_keys_.emplace(material_key, true);
      }
      return renderer::kInvalidMaterial;
    }

    SharedMaterialResource shared{};
    shared.material = device_.createMaterial(*resolved);
    if (shared.material == renderer::kInvalidMaterial) {
      if (diag_enabled) {
        spdlog::info("RenderSystem material cache miss material='{}' failed in {:.2f} ms",
                     material_key,
                     core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
      }
      return renderer::kInvalidMaterial;
    }
    shared.ref_count = 1;
    shared_it = shared_materials_.emplace(material_key, std::move(shared)).first;
    if (diag_enabled) {
      spdlog::info("RenderSystem material cache miss material='{}' took {:.2f} ms",
                   material_key,
                   core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
    }
  } else {
    shared_it->second.ref_count += 1;
    if (diag_enabled) {
      spdlog::info("RenderSystem material cache hit material='{}' took {:.2f} ms",
                   material_key,
                   core::elapsedMilliseconds(cache_start, core::SteadyClock::now()));
    }
  }

  return shared_it->second.material;
}

void RenderSystem::releaseSharedMaterial(const std::string& material_key) {
  if (material_key.empty()) {
    return;
  }

  auto shared_it = shared_materials_.find(material_key);
  if (shared_it == shared_materials_.end()) {
    return;
  }
  if (shared_it->second.ref_count > 0) {
    shared_it->second.ref_count -= 1;
  }
  if (shared_it->second.ref_count == 0) {
    if (shared_it->second.material != renderer::kInvalidMaterial) {
      device_.destroyMaterial(shared_it->second.material);
    }
    shared_materials_.erase(shared_it);
  }
}

void RenderSystem::update(ecs::World& world, scene::Scene& /*scene*/, float /*dt*/,
                          float interpolation_alpha) {
  static size_t diag_update_count = 0;
  const bool diag_requested = renderSystemDiagEnabled();
  const bool diag_enabled = diag_requested &&
                            (renderSystemDiagEveryFrameEnabled() || diag_update_count == 0);
  if (diag_requested) {
    ++diag_update_count;
  }
  const auto update_start = core::SteadyClock::now();
  auto section_start = update_start;
  auto section_end = update_start;

  if (material_library_ != nullptr &&
      material_library_->version() != last_material_library_version_) {
    for (auto& [key, record] : records_) {
      (void)key;
      components::MeshComponent mesh_binding{};
      mesh_binding.mesh_key = record.mesh_key;
      mesh_binding.materials = record.component_materials;
      releaseMaterialBinding(record);
      bindMaterial(mesh_binding, record);
    }
    last_material_library_version_ = material_library_->version();
  }
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "material cache refresh", section_start, section_end);
  section_start = section_end;

  static bool logged_start = false;
  if (!logged_start) {
    logged_start = true;
  }
  bool has_camera = false;
  renderer::CameraData primary_camera{};
  renderer::PostProcessSettings primary_post_process{};
  struct OffscreenPass {
    renderer::CameraData camera;
    renderer::RenderTargetId target = renderer::kDefaultRenderTarget;
    renderer::PostProcessSettings post_process;
  };
  std::vector<OffscreenPass> offscreen_passes;
  std::unordered_set<std::string> active_render_target_keys;
  world.forEach<components::CameraComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    const auto& camera = world.get<components::CameraComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);
    const renderer::CameraData cam = toCameraData(camera, transform, interpolation_alpha);
    const renderer::PostProcessSettings post_process =
        resolvePostProcessSettings(post_process_profiles_, camera.post_process_profile_key);

    if (camera.render_to_texture) {
      renderer::RenderTargetId target_id = camera.render_target;
      if (target_id == renderer::kDefaultRenderTarget && !camera.render_target_key.empty()) {
        active_render_target_keys.insert(camera.render_target_key);
        auto target_it = render_targets_by_key_.find(camera.render_target_key);
        if (target_it == render_targets_by_key_.end()) {
          renderer::RenderTargetDesc target_desc{};
          target_desc.width = 512;
          target_desc.height = 512;
          target_desc.depth = true;
          target_desc.stencil = false;
          target_id = device_.createRenderTarget(target_desc);
          if (target_id != renderer::kDefaultRenderTarget) {
            render_targets_by_key_[camera.render_target_key] = target_id;
          }
        } else {
          target_id = target_it->second;
        }
      }
      if (target_id != renderer::kDefaultRenderTarget) {
        offscreen_passes.push_back(OffscreenPass{
            .camera = cam,
            .target = target_id,
            .post_process = post_process,
        });
      }
    }

    if (!has_camera && camera.is_primary) {
      primary_camera = cam;
      primary_post_process = post_process;
      has_camera = true;
    }
    return true;
  });

  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "camera collection", section_start, section_end);
  section_start = section_end;

  for (auto it = render_targets_by_key_.begin(); it != render_targets_by_key_.end();) {
    if (active_render_target_keys.find(it->first) != active_render_target_keys.end()) {
      ++it;
      continue;
    }
    if (it->second != renderer::kDefaultRenderTarget) {
      device_.destroyRenderTarget(it->second);
    }
    it = render_targets_by_key_.erase(it);
  }
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "render target cleanup", section_start, section_end);
  section_start = section_end;

  if (!has_camera) {
    if (!warned_no_camera_) {
      warned_no_camera_ = true;
    }
    device_.setCameraActive(false);
    device_.renderLayer(0,
                        renderer::kDefaultRenderTarget,
                        resolvePostProcessSettings(post_process_profiles_, {}));
    logRenderSystemStage(diag_enabled, "total", update_start, core::SteadyClock::now());
    return;
  }
  warned_no_camera_ = false;
  device_.setCamera(primary_camera);
  device_.setCameraActive(true);

  renderer::DirectionalLightData light{};
  std::vector<renderer::LightData> lights;
  lights.reserve(16);
  bool has_light = false;
  static bool warned_missing_light_transform = false;
  if (!warned_missing_light_transform) {
    world.forEach<components::LightComponent>([&](const ecs::Entity entity) {
      if (!world.has<components::TransformComponent>(entity)) {
        warned_missing_light_transform = true;
        return false;
      }
      return true;
    });
  }
  world.forEach<components::LightComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    const auto& light_component = world.get<components::LightComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);
    if (light_component.type != components::LightComponent::Type::Directional) {
      bool visible = true;
      if (world.has<components::VisibilityComponent>(entity)) {
        visible = world.get<components::VisibilityComponent>(entity).visible;
      }
      if (!visible || light_component.intensity <= 0.0f || light_component.range <= 0.0f) {
        return true;
      }
    }
    lights.push_back(toLightData(light_component, transform, interpolation_alpha));
    if (!has_light && light_component.type == components::LightComponent::Type::Directional) {
      light = toDirectionalLight(light_component, transform, interpolation_alpha);
      has_light = true;
    }
    return true;
  });
  if (!has_light) {
    light.direction = glm::vec3(0.3f, -1.0f, 0.2f);
    light.color = math::Color{1.0f, 1.0f, 1.0f, 1.0f};
    light.intensity = 1.0f;
  }
  device_.setDirectionalLight(light);
  device_.setLights(lights);
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "lights", section_start, section_end);
  section_start = section_end;

  bool env_found = false;
  world.forEach<components::EnvironmentComponent>([&](const ecs::Entity entity) {
    const auto& env = world.get<components::EnvironmentComponent>(entity);
    if (!env.enabled) {
      return true;
    }
    if (env.environment_map != last_env_path_ ||
        env.intensity != last_env_intensity_ ||
        env.draw_skybox != last_env_draw_skybox_) {
      device_.setEnvironmentMap(env.environment_map, env.intensity, env.draw_skybox);
      last_env_path_ = env.environment_map;
      last_env_intensity_ = env.intensity;
      last_env_draw_skybox_ = env.draw_skybox;
    }
    env_found = true;
    return false;
  });
  if (!env_found &&
      (!last_env_path_.empty() || last_env_intensity_ >= 0.0f || last_env_draw_skybox_)) {
    device_.setEnvironmentMap({}, 0.0f, false);
    last_env_path_.clear();
    last_env_intensity_ = -1.0f;
    last_env_draw_skybox_ = false;
  }
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "environment", section_start, section_end);
  section_start = section_end;

  size_t mesh_entity_count = 0;
  size_t new_render_record_count = 0;
  world.forEach<components::MeshComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    ++mesh_entity_count;
    const auto& mesh = world.get<components::MeshComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);

    bool visible = mesh.visible;
    if (world.has<components::VisibilityComponent>(entity)) {
      visible = visible && world.get<components::VisibilityComponent>(entity).visible;
    }

    const uint64_t key = entityKey(entity);
    auto it = records_.find(key);
    if (it == records_.end()) {
      RenderRecord record;
      const auto bind_mesh_start = core::SteadyClock::now();
      bindMesh(mesh, record);
      const auto bind_mesh_end = core::SteadyClock::now();
      if (diag_enabled) {
        spdlog::info("RenderSystem new record entity={}:{} mesh='{}' bindMesh took {:.2f} ms",
                     entity.index,
                     entity.generation,
                     mesh.mesh_key.empty() ? "<empty>" : mesh.mesh_key,
                     core::elapsedMilliseconds(bind_mesh_start, bind_mesh_end));
      }

      const auto bind_material_start = bind_mesh_end;
      bindMaterial(mesh, record);
      const auto bind_material_end = core::SteadyClock::now();
      if (diag_enabled) {
        spdlog::info(
            "RenderSystem new record entity={}:{} material_slots={} bindMaterial took {:.2f} ms",
            entity.index,
            entity.generation,
            mesh.materials.size(),
            core::elapsedMilliseconds(bind_material_start, bind_material_end));
      }
      it = records_.emplace(key, std::move(record)).first;
      ++new_render_record_count;
    } else {
      const bool mesh_binding_changed = it->second.mesh_key != mesh.mesh_key;

      if (mesh_binding_changed) {
        releaseMaterialBinding(it->second);
        releaseMeshBinding(it->second);
        bindMesh(mesh, it->second);
        bindMaterial(mesh, it->second);
      } else {
        const bool material_binding_changed = it->second.component_materials != mesh.materials;
        if (material_binding_changed) {
          releaseMaterialBinding(it->second);
          bindMaterial(mesh, it->second);
        }
      }
    }

    glm::mat4 world_matrix = toTransform(transform, interpolation_alpha);
    const components::SkinnedMeshComponent* skinned_mesh = nullptr;
    if (world.has<components::SkinnedMeshComponent>(entity)) {
      const auto& skin = world.get<components::SkinnedMeshComponent>(entity);
      skinned_mesh = &skin;
      if (skin.override_render_transform) {
        world_matrix = glm::mat4(1.0f);
        if (skin.render_transform_entity.isValid() &&
            world.isAlive(skin.render_transform_entity) &&
            world.has<components::TransformComponent>(skin.render_transform_entity)) {
          world_matrix =
              toTransform(world.get<components::TransformComponent>(skin.render_transform_entity),
                          interpolation_alpha);
        }
      }
    }
    DrawItem item{};
    item.instance = static_cast<InstanceId>(key);
    item.mesh = it->second.mesh;
    item.materials = it->second.material_bindings;
    item.transform = world_matrix;
    item.layer = 0;
    item.visible = visible;
    item.shadow_visible = visible && mesh.shadow_visible;
    if (skinned_mesh != nullptr &&
        skinned_mesh->enabled &&
        skinned_mesh->skinning_path == components::SkinningPath::Gpu &&
        skinned_mesh->palette_valid &&
        !skinned_mesh->joint_palette.empty()) {
      item.skinning_enabled = true;
      item.skinning_palette = skinned_mesh->joint_palette;
    }
    device_.submit(item);
  });
  section_end = core::SteadyClock::now();
  if (diag_enabled) {
    spdlog::info("RenderSystem stage 'mesh submit' took {:.2f} ms (meshes={} new_records={})",
                 core::elapsedMilliseconds(section_start, section_end),
                 mesh_entity_count,
                 new_render_record_count);
  }
  section_start = section_end;

  cleanupStaleRecords(world);
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "stale record cleanup", section_start, section_end);
  section_start = section_end;

  const math::Color debug_color{0.1f, 1.0f, 0.1f, 1.0f};
  world.forEach<components::TransformComponent, components::ColliderComponent>(
      [&](const ecs::Entity entity) {
    const auto& collider = world.get<components::ColliderComponent>(entity);
    if (!collider.debug_draw) {
      return;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    if (const auto* box = std::get_if<components::BoxColliderShape>(&collider.shape)) {
      drawBoxWire(device_, transform, box->center, box->half_extents, debug_color,
                  interpolation_alpha);
      return;
    }
    if (const auto* sphere = std::get_if<components::SphereColliderShape>(&collider.shape)) {
      drawSphereWire(device_, transform, sphere->center, sphere->radius, debug_color,
                     interpolation_alpha);
      return;
    }
    if (const auto* capsule = std::get_if<components::CapsuleColliderShape>(&collider.shape)) {
      drawCapsuleWire(device_, transform, capsule->center, capsule->radius, capsule->height,
                      debug_color, interpolation_alpha);
      return;
    }
    if (!std::holds_alternative<components::MeshColliderShape>(collider.shape) ||
        !world.has<components::MeshComponent>(entity)) {
      return;
    }
    const uint64_t key = entityKey(entity);
    auto record_it = records_.find(key);
    if (record_it == records_.end() || !record_it->second.bounds_valid) {
      return;
    }
    drawSphereWire(device_, transform,
                   {record_it->second.bounds_center.x, record_it->second.bounds_center.y,
                    record_it->second.bounds_center.z},
                   record_it->second.bounds_radius, debug_color, interpolation_alpha);
  });
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "debug collider draw", section_start, section_end);
  section_start = section_end;

  for (const auto& pass : offscreen_passes) {
    device_.setCamera(pass.camera);
    device_.setCameraActive(true);
    device_.renderLayer(0, pass.target, pass.post_process);
  }
  device_.setCamera(primary_camera);
  device_.setCameraActive(true);
  device_.renderLayer(0, renderer::kDefaultRenderTarget, primary_post_process);
  section_end = core::SteadyClock::now();
  logRenderSystemStage(diag_enabled, "offscreen passes", section_start, section_end);
  logRenderSystemStage(diag_enabled, "total", update_start, section_end);
}

}  // namespace karma::renderer
