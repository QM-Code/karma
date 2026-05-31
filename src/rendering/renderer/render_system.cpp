#include "karma/rendering/renderer/render_system.h"

#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>
#include <spdlog/spdlog.h>

#include "karma/world/components/camera.h"
#include "karma/world/components/collider.h"
#include "karma/world/components/environment.h"
#include "karma/world/components/light.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/visibility.h"

namespace karma::renderer {

namespace {
std::string makeMaterialVariantCacheKey(const std::string& mesh_key,
                                        const std::string& material_key) {
  std::string key;
  key.reserve(mesh_key.size() + material_key.size() + 1);
  key.append(mesh_key);
  key.push_back('\n');
  key.append(material_key);
  return key;
}

glm::vec3 toGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

glm::quat toGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

glm::vec3 transformPoint(const components::TransformComponent& transform,
                         const math::Vec3& local,
                         float interpolation_alpha) {
  const glm::vec3 pos = toGlm(transform.getInterpolatedPosition(interpolation_alpha));
  const glm::quat rot = toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  const glm::vec3 scale = toGlm(transform.getScale());
  const glm::vec3 scaled{local.x * scale.x, local.y * scale.y, local.z * scale.z};
  return pos + glm::mat3_cast(rot) * scaled;
}

void drawCircle(GraphicsDevice& device,
                const glm::vec3& center,
                const glm::vec3& axis_x,
                const glm::vec3& axis_y,
                float radius,
                const math::Color& color,
                int segments = 24) {
  constexpr float kPi = 3.14159265358979323846f;
  const float step = static_cast<float>(2.0f * kPi) / static_cast<float>(segments);
  glm::vec3 prev = center + radius * axis_x;
  for (int i = 1; i <= segments; ++i) {
    const float angle = step * static_cast<float>(i);
    const glm::vec3 next = center + radius * (std::cos(angle) * axis_x + std::sin(angle) * axis_y);
    device.drawLine({prev.x, prev.y, prev.z}, {next.x, next.y, next.z}, color, true, 1.0f);
    prev = next;
  }
}

void drawBoxWire(GraphicsDevice& device,
                 const components::TransformComponent& transform,
                 const math::Vec3& center,
                 const math::Vec3& half_extents,
                 const math::Color& color,
                 float interpolation_alpha) {
  const math::Vec3 c = center;
  const math::Vec3 h = half_extents;
  const math::Vec3 corners[8] = {
      {c.x - h.x, c.y - h.y, c.z - h.z},
      {c.x + h.x, c.y - h.y, c.z - h.z},
      {c.x + h.x, c.y + h.y, c.z - h.z},
      {c.x - h.x, c.y + h.y, c.z - h.z},
      {c.x - h.x, c.y - h.y, c.z + h.z},
      {c.x + h.x, c.y - h.y, c.z + h.z},
      {c.x + h.x, c.y + h.y, c.z + h.z},
      {c.x - h.x, c.y + h.y, c.z + h.z},
  };

  const int edges[12][2] = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0},
      {4, 5}, {5, 6}, {6, 7}, {7, 4},
      {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };

  glm::vec3 world_corners[8];
  for (int i = 0; i < 8; ++i) {
    world_corners[i] = transformPoint(transform, corners[i], interpolation_alpha);
  }

  for (const auto& edge : edges) {
    const glm::vec3 a = world_corners[edge[0]];
    const glm::vec3 b = world_corners[edge[1]];
    device.drawLine({a.x, a.y, a.z}, {b.x, b.y, b.z}, color, true, 1.0f);
  }
}

void drawSphereWire(GraphicsDevice& device,
                    const components::TransformComponent& transform,
                    const math::Vec3& center,
                    float radius,
                    const math::Color& color,
                    float interpolation_alpha) {
  const glm::vec3 world_center = transformPoint(transform, center, interpolation_alpha);
  const glm::vec3 scale = toGlm(transform.getScale());
  const float max_scale = std::max(scale.x, std::max(scale.y, scale.z));
  const float r = radius * max_scale;
  drawCircle(device, world_center, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, r, color);
  drawCircle(device, world_center, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, r, color);
  drawCircle(device, world_center, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, r, color);
}

void drawCapsuleWire(GraphicsDevice& device,
                     const components::TransformComponent& transform,
                     const math::Vec3& center,
                     float radius,
                     float height,
                     const math::Color& color,
                     float interpolation_alpha) {
  const glm::vec3 scale = toGlm(transform.getScale());
  const float r = radius * std::max(scale.x, scale.z);
  const float half_height = (height * 0.5f) * scale.y;
  const glm::quat rot = toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  const glm::mat3 basis = glm::mat3_cast(rot);
  const glm::vec3 up = basis * glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::vec3 right = basis * glm::vec3(1.0f, 0.0f, 0.0f);
  const glm::vec3 forward = basis * glm::vec3(0.0f, 0.0f, 1.0f);
  const glm::vec3 world_center = transformPoint(transform, center, interpolation_alpha);
  const glm::vec3 top = world_center + up * half_height;
  const glm::vec3 bottom = world_center - up * half_height;

  drawCircle(device, top, right, forward, r, color);
  drawCircle(device, bottom, right, forward, r, color);

  const glm::vec3 offsets[4] = {right * r, -right * r, forward * r, -forward * r};
  for (const auto& offset : offsets) {
    const glm::vec3 a = top + offset;
    const glm::vec3 b = bottom + offset;
    device.drawLine({a.x, a.y, a.z}, {b.x, b.y, b.z}, color, true, 1.0f);
  }
}

renderer::DirectionalLightData toDirectionalLight(const components::LightComponent& light,
                                                  const components::TransformComponent& transform,
                                                  float interpolation_alpha) {
  renderer::DirectionalLightData out{};
  out.color = light.color;
  out.intensity = light.intensity;
  const glm::quat rot = toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  const glm::mat3 basis = glm::mat3_cast(rot);
  out.direction = basis * glm::vec3(0.0f, 0.0f, -1.0f);
  out.position = toGlm(transform.getInterpolatedPosition(interpolation_alpha));
  out.shadow_extent = light.shadow_extent;
  out.casts_shadows = light.casts_shadows;
  return out;
}

renderer::LightData toLightData(const components::LightComponent& light,
                                const components::TransformComponent& transform,
                                float interpolation_alpha) {
  renderer::LightData out{};
  const glm::quat rot = toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  const glm::mat3 basis = glm::mat3_cast(rot);
  glm::vec3 dir = basis * glm::vec3(0.0f, 0.0f, -1.0f);
  if (glm::length(dir) < 1e-5f) {
    dir = glm::vec3(0.0f, -1.0f, 0.0f);
  } else {
    dir = glm::normalize(dir);
  }
  out.position = toGlm(transform.getInterpolatedPosition(interpolation_alpha));
  out.direction = dir;
  out.color = light.color;
  out.intensity = light.intensity;
  out.range = std::max(light.range, 0.0f);
  out.casts_shadows = light.casts_shadows;

  const float inner_rad = glm::radians(light.inner_cone_degrees);
  const float outer_rad = glm::radians(light.outer_cone_degrees);
  float inner_cos = std::cos(inner_rad);
  float outer_cos = std::cos(outer_rad);
  if (inner_cos < outer_cos) {
    std::swap(inner_cos, outer_cos);
  }
  out.inner_cone_cos = inner_cos;
  out.outer_cone_cos = outer_cos;

  switch (light.type) {
    case components::LightComponent::Type::Directional:
      out.type = renderer::LightType::Directional;
      out.range = 0.0f;
      break;
    case components::LightComponent::Type::Point:
      out.type = renderer::LightType::Point;
      break;
    case components::LightComponent::Type::Spot:
      out.type = renderer::LightType::Spot;
      break;
  }
  return out;
}

glm::mat4 toTransform(const components::TransformComponent& transform, float interpolation_alpha) {
  const glm::vec3 pos = toGlm(transform.getInterpolatedPosition(interpolation_alpha));
  const glm::quat rot = toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  const glm::vec3 scale = toGlm(transform.getScale());
  glm::mat4 matrix(1.0f);
  matrix = glm::translate(matrix, pos);
  matrix *= glm::mat4_cast(rot);
  matrix = glm::scale(matrix, scale);
  return matrix;
}

renderer::CameraData toCameraData(const components::CameraComponent& camera,
                                  const components::TransformComponent& transform,
                                  float interpolation_alpha) {
  renderer::CameraData out{};
  out.position = toGlm(transform.getInterpolatedPosition(interpolation_alpha));
  out.rotation = toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  out.perspective = camera.perspective;
  out.render_shadows = camera.render_shadows;
  out.fov_y_degrees = camera.fov_y_degrees;
  out.aspect = 16.0f / 9.0f;
  out.near_clip = camera.near_clip;
  out.far_clip = camera.far_clip;
  out.ortho_left = camera.ortho_left;
  out.ortho_right = camera.ortho_right;
  out.ortho_top = camera.ortho_top;
  out.ortho_bottom = camera.ortho_bottom;
  out.shader_override_vertex_path = camera.shader_override_vertex_path;
  out.shader_override_fragment_path = camera.shader_override_fragment_path;
  out.shader_user_param_count = 0u;
  for (const auto& [key, value] : camera.shader_user_params) {
    if (out.shader_user_param_count >= renderer::kCameraShaderUserParamCapacity) {
      break;
    }
    auto& dst = out.shader_user_params[out.shader_user_param_count++];
    dst.key_hash = renderer::cameraShaderParamKeyHash(key);
    dst.value = value;
  }
  return out;
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
  if (record.direct_mesh_id != renderer::kInvalidMesh) {
    if (record.owns_direct_mesh_id) {
      device_.destroyMesh(record.direct_mesh_id);
    }
    record.direct_mesh_id = renderer::kInvalidMesh;
    record.owns_direct_mesh_id = false;
  } else {
    releaseSharedMesh(record.mesh_key);
    record.mesh_key.clear();
  }

  record.mesh = renderer::kInvalidMesh;
  record.bounds_center = glm::vec3(0.0f);
  record.bounds_radius = 0.0f;
  record.bounds_valid = false;
}

void RenderSystem::releaseMaterialBinding(RenderRecord& record) {
  if (record.direct_material_id != renderer::kInvalidMaterial) {
    if (record.owns_direct_material_id) {
      device_.destroyMaterial(record.direct_material_id);
    }
    record.direct_material_id = renderer::kInvalidMaterial;
    record.owns_direct_material_id = false;
  } else {
    releaseSharedMaterialVariant(record.mesh_key, record.material_key);
    record.material_key.clear();
  }

  record.material = renderer::kInvalidMaterial;
  record.material_set = renderer::kInvalidMaterialSet;
}

void RenderSystem::bindMesh(const components::MeshComponent& mesh, RenderRecord& record) {
  if (mesh.mesh_id != renderer::kInvalidMesh) {
    record.mesh_key.clear();
    record.direct_mesh_id = mesh.mesh_id;
    record.owns_direct_mesh_id = mesh.owns_mesh_id;
    record.mesh = mesh.mesh_id;
    record.bounds_center = glm::vec3(0.0f);
    record.bounds_radius = 0.0f;
    record.bounds_valid = device_.getMeshBounds(record.mesh, record.bounds_center,
                                                record.bounds_radius);
    return;
  }

  record.direct_mesh_id = renderer::kInvalidMesh;
  record.owns_direct_mesh_id = false;
  record.mesh_key = mesh.mesh_key;
  acquireSharedMesh(mesh.mesh_key, record);
}

void RenderSystem::bindMaterial(const components::MeshComponent& mesh, RenderRecord& record) {
  record.material = renderer::kInvalidMaterial;
  record.material_set = renderer::kInvalidMaterialSet;

  if (mesh.material_id != renderer::kInvalidMaterial) {
    record.material_key.clear();
    record.direct_material_id = mesh.material_id;
    record.owns_direct_material_id = mesh.owns_material_id;
    record.material = mesh.material_id;
    return;
  }

  record.direct_material_id = renderer::kInvalidMaterial;
  record.owns_direct_material_id = false;
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
    shared.mesh = device_.createMeshFromFile(mesh_key);
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
    if (shared_it->second.mesh != renderer::kInvalidMesh) {
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
  if (material_library_ != nullptr &&
      material_library_->version() != last_material_library_version_) {
    for (auto& [key, record] : records_) {
      (void)key;
      if (record.direct_material_id == renderer::kInvalidMaterial &&
          !record.material_key.empty()) {
        releaseSharedMaterialVariant(record.mesh_key, record.material_key);
        record.material_set = renderer::kInvalidMaterialSet;
      }
    }
    last_material_library_version_ = material_library_->version();
  }

  static bool logged_start = false;
  if (!logged_start) {
    logged_start = true;
  }
  bool has_camera = false;
  renderer::CameraData primary_camera{};
  struct OffscreenPass {
    renderer::CameraData camera;
    renderer::RenderTargetId target = renderer::kDefaultRenderTarget;
  };
  std::vector<OffscreenPass> offscreen_passes;
  std::unordered_set<std::string> active_render_target_keys;
  world.forEach<components::CameraComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
    const auto& camera = world.get<components::CameraComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);
    const renderer::CameraData cam = toCameraData(camera, transform, interpolation_alpha);

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
        offscreen_passes.push_back(OffscreenPass{.camera = cam, .target = target_id});
      }
    }

    if (!has_camera && camera.is_primary) {
      primary_camera = cam;
      has_camera = true;
    }
    return true;
  });

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

  if (!has_camera) {
    if (!warned_no_camera_) {
      warned_no_camera_ = true;
    }
    device_.setCameraActive(false);
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

  world.forEach<components::MeshComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
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
      bindMesh(mesh, record);
      bindMaterial(mesh, record);
      it = records_.emplace(key, std::move(record)).first;
    } else {
      const bool mesh_binding_changed =
          it->second.direct_mesh_id != mesh.mesh_id ||
          it->second.owns_direct_mesh_id != mesh.owns_mesh_id ||
          (mesh.mesh_id == renderer::kInvalidMesh && it->second.mesh_key != mesh.mesh_key) ||
          (mesh.mesh_id != renderer::kInvalidMesh && !it->second.mesh_key.empty());

      if (mesh_binding_changed) {
        releaseMaterialBinding(it->second);
        releaseMeshBinding(it->second);
        bindMesh(mesh, it->second);
        bindMaterial(mesh, it->second);
      } else {
        const bool material_binding_changed =
            it->second.direct_material_id != mesh.material_id ||
            it->second.owns_direct_material_id != mesh.owns_material_id ||
            (mesh.material_id == renderer::kInvalidMaterial &&
             it->second.material_key != mesh.material_key) ||
            (mesh.material_id != renderer::kInvalidMaterial && !it->second.material_key.empty());
        if (material_binding_changed) {
          releaseMaterialBinding(it->second);
          bindMaterial(mesh, it->second);
        }
      }
    }

    const glm::mat4 world_matrix = toTransform(transform, interpolation_alpha);
    DrawItem item{};
    item.instance = static_cast<InstanceId>(key);
    item.mesh = it->second.mesh;
    item.material = it->second.material;
    item.material_set = it->second.material_set;
    item.transform = world_matrix;
    item.layer = 0;
    item.visible = visible;
    item.shadow_visible = visible && mesh.shadow_visible;
    device_.submit(item);
  });

  cleanupStaleRecords(world);

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

  for (const auto& pass : offscreen_passes) {
    device_.setCamera(pass.camera);
    device_.setCameraActive(true);
    device_.renderLayer(0, pass.target);
  }
  device_.setCamera(primary_camera);
  device_.setCameraActive(true);
}

}  // namespace karma::renderer
