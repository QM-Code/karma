#include "karma/rendering/renderer/render_system.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
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

std::string makeMaterialVariantCacheKey(const std::string& mesh_key,
                                        const std::string& material_key) {
  std::string key;
  key.reserve(mesh_key.size() + material_key.size() + 1);
  key.append(mesh_key);
  key.push_back('\n');
  key.append(material_key);
  return key;
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

  record.mesh = renderer::kInvalidMesh;
  record.bounds_center = glm::vec3(0.0f);
  record.bounds_radius = 0.0f;
  record.bounds_valid = false;
}

void RenderSystem::releaseMaterialBinding(RenderRecord& record) {
  releaseSharedMaterialVariant(record.mesh_key, record.material_key);
  record.material_key.clear();

  record.material = renderer::kInvalidMaterial;
  record.material_set = renderer::kInvalidMaterialSet;
}

void RenderSystem::bindMesh(const components::MeshComponent& mesh, RenderRecord& record) {
  record.mesh_key = mesh.mesh_key;
  acquireSharedMesh(mesh.mesh_key, record);
}

void RenderSystem::bindMaterial(const components::MeshComponent& mesh, RenderRecord& record) {
  record.material = renderer::kInvalidMaterial;
  record.material_set = renderer::kInvalidMaterialSet;

  record.material_key = mesh.material_key;
  acquireSharedMaterialVariant(record.mesh_key, mesh.material_key, record);
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

void RenderSystem::acquireSharedMaterialVariant(const std::string& mesh_key,
                                                const std::string& material_key,
                                                RenderRecord& record) {
  record.material_set = renderer::kInvalidMaterialSet;
  if (mesh_key.empty() || material_key.empty() || record.mesh == renderer::kInvalidMesh ||
      material_library_ == nullptr) {
    return;
  }

  const auto* desc = material_library_->find(material_key);
  if (desc == nullptr) {
    if (!warned_missing_material_keys_.contains(material_key)) {
      spdlog::warn("Karma: material key '{}' was not registered; using source mesh materials",
                   material_key);
      warned_missing_material_keys_.emplace(material_key, true);
    }
    return;
  }

  if (!desc->source_mesh_key.empty() && desc->source_mesh_key != mesh_key) {
    const std::string warning_key = makeMaterialVariantCacheKey(mesh_key, material_key);
    if (!warned_material_mesh_mismatch_keys_.contains(warning_key)) {
      spdlog::warn(
          "Karma: material key '{}' was registered for mesh '{}' but applied to '{}'; using source mesh materials",
          material_key, desc->source_mesh_key, mesh_key);
      warned_material_mesh_mismatch_keys_.emplace(warning_key, true);
    }
    return;
  }

  const std::string cache_key = makeMaterialVariantCacheKey(mesh_key, material_key);
  auto shared_it = shared_material_variants_.find(cache_key);
  if (shared_it == shared_material_variants_.end()) {
    SharedMaterialVariant shared{};
    shared.material_set = device_.createMaterialSetFromMesh(record.mesh, *desc);
    if (shared.material_set == renderer::kInvalidMaterialSet) {
      return;
    }
    shared.ref_count = 1;
    shared_it = shared_material_variants_.emplace(cache_key, std::move(shared)).first;
  } else {
    shared_it->second.ref_count += 1;
  }

  record.material_set = shared_it->second.material_set;
}

void RenderSystem::releaseSharedMaterialVariant(const std::string& mesh_key,
                                                const std::string& material_key) {
  if (mesh_key.empty() || material_key.empty()) {
    return;
  }

  const std::string cache_key = makeMaterialVariantCacheKey(mesh_key, material_key);
  auto shared_it = shared_material_variants_.find(cache_key);
  if (shared_it == shared_material_variants_.end()) {
    return;
  }
  if (shared_it->second.ref_count > 0) {
    shared_it->second.ref_count -= 1;
  }
  if (shared_it->second.ref_count == 0) {
    if (shared_it->second.material_set != renderer::kInvalidMaterialSet) {
      device_.destroyMaterialSet(shared_it->second.material_set);
    }
    shared_material_variants_.erase(shared_it);
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
      if (!record.material_key.empty()) {
        releaseSharedMaterialVariant(record.mesh_key, record.material_key);
        record.material_set = renderer::kInvalidMaterialSet;
      }
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
            "RenderSystem new record entity={}:{} material='{}' bindMaterial took {:.2f} ms",
            entity.index,
            entity.generation,
            mesh.material_key.empty() ? "<source mesh>" : mesh.material_key,
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
        const bool material_binding_changed = it->second.material_key != mesh.material_key;
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
    item.material = it->second.material;
    item.material_set = it->second.material_set;
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
  world.forEach<components::TransformComponent, components::BoxColliderComponent>(
      [&](const ecs::Entity entity) {
    const auto& collider = world.get<components::BoxColliderComponent>(entity);
    if (!collider.debug_draw) {
      return;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    drawBoxWire(device_, transform, collider.center, collider.half_extents, debug_color,
                interpolation_alpha);
  });

  world.forEach<components::TransformComponent, components::SphereColliderComponent>(
      [&](const ecs::Entity entity) {
    const auto& collider = world.get<components::SphereColliderComponent>(entity);
    if (!collider.debug_draw) {
      return;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    drawSphereWire(device_, transform, collider.center, collider.radius, debug_color,
                   interpolation_alpha);
  });

  world.forEach<components::TransformComponent, components::CapsuleColliderComponent>(
      [&](const ecs::Entity entity) {
    const auto& collider = world.get<components::CapsuleColliderComponent>(entity);
    if (!collider.debug_draw) {
      return;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    drawCapsuleWire(device_, transform, collider.center, collider.radius, collider.height,
                    debug_color, interpolation_alpha);
  });

  world.forEach<components::TransformComponent, components::MeshColliderComponent, components::MeshComponent>(
      [&](const ecs::Entity entity) {
    const auto& collider = world.get<components::MeshColliderComponent>(entity);
    if (!collider.debug_draw) {
      return;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    const auto& mesh = world.get<components::MeshComponent>(entity);
    if (mesh.mesh_key.empty()) {
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
