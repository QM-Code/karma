#include "karma/renderer/render_system.h"

#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

#include "karma/components/camera.h"
#include "karma/components/collider.h"
#include "karma/components/environment.h"
#include "karma/components/light.h"
#include "karma/components/mesh.h"

namespace karma::renderer {

namespace {
glm::vec3 toGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

glm::quat toGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

glm::vec3 transformPoint(const components::TransformComponent& transform,
                          const math::Vec3& local) {
  const glm::vec3 pos = toGlm(transform.getPosition());
  const glm::quat rot = toGlm(transform.getRotation());
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
                 const math::Color& color) {
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
    world_corners[i] = transformPoint(transform, corners[i]);
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
                    const math::Color& color) {
  const glm::vec3 world_center = transformPoint(transform, center);
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
                     const math::Color& color) {
  const glm::vec3 scale = toGlm(transform.getScale());
  const float r = radius * std::max(scale.x, scale.z);
  const float half_height = (height * 0.5f) * scale.y;
  const glm::quat rot = toGlm(transform.getRotation());
  const glm::mat3 basis = glm::mat3_cast(rot);
  const glm::vec3 up = basis * glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::vec3 right = basis * glm::vec3(1.0f, 0.0f, 0.0f);
  const glm::vec3 forward = basis * glm::vec3(0.0f, 0.0f, 1.0f);
  const glm::vec3 world_center = transformPoint(transform, center);
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
                                                  const components::TransformComponent& transform) {
  renderer::DirectionalLightData out{};
  out.color = light.color;
  out.intensity = light.intensity;
  const glm::quat rot = toGlm(transform.getRotation());
  const glm::mat3 basis = glm::mat3_cast(rot);
  out.direction = basis * glm::vec3(0.0f, 0.0f, -1.0f);
  out.position = toGlm(transform.getPosition());
  out.shadow_extent = light.shadow_extent;
  return out;
}

renderer::LightData toLightData(const components::LightComponent& light,
                                const components::TransformComponent& transform) {
  renderer::LightData out{};
  const glm::quat rot = toGlm(transform.getRotation());
  const glm::mat3 basis = glm::mat3_cast(rot);
  glm::vec3 dir = basis * glm::vec3(0.0f, 0.0f, -1.0f);
  if (glm::length(dir) < 1e-5f) {
    dir = glm::vec3(0.0f, -1.0f, 0.0f);
  } else {
    dir = glm::normalize(dir);
  }
  out.position = toGlm(transform.getPosition());
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

glm::mat4 toTransform(const components::TransformComponent& transform) {
  const glm::vec3 pos = toGlm(transform.getPosition());
  const glm::quat rot = toGlm(transform.getRotation());
  const glm::vec3 scale = toGlm(transform.getScale());
  glm::mat4 matrix(1.0f);
  matrix = glm::translate(matrix, pos);
  matrix *= glm::mat4_cast(rot);
  matrix = glm::scale(matrix, scale);
  return matrix;
}

struct FrustumPlanes {
  glm::vec4 planes[6];
};

FrustumPlanes extractFrustumPlanes(const glm::mat4& m) {
  const glm::vec4 row0{m[0][0], m[1][0], m[2][0], m[3][0]};
  const glm::vec4 row1{m[0][1], m[1][1], m[2][1], m[3][1]};
  const glm::vec4 row2{m[0][2], m[1][2], m[2][2], m[3][2]};
  const glm::vec4 row3{m[0][3], m[1][3], m[2][3], m[3][3]};

  FrustumPlanes frustum{};
  frustum.planes[0] = row3 + row0;
  frustum.planes[1] = row3 - row0;
  frustum.planes[2] = row3 + row1;
  frustum.planes[3] = row3 - row1;
  frustum.planes[4] = row3 + row2;
  frustum.planes[5] = row3 - row2;

  for (auto& plane : frustum.planes) {
    const float length = glm::length(glm::vec3(plane));
    if (length > 0.0f) {
      plane /= length;
    }
  }
  return frustum;
}

bool sphereInFrustum(const FrustumPlanes& frustum, const glm::vec3& center, float radius) {
  for (const auto& plane : frustum.planes) {
    const float distance = glm::dot(glm::vec3(plane), center) + plane.w;
    if (distance < -radius) {
      return false;
    }
  }
  return true;
}

renderer::CameraData toCameraData(const components::CameraComponent& camera,
                                  const components::TransformComponent& transform) {
  renderer::CameraData out{};
  out.position = toGlm(transform.getPosition());
  out.rotation = toGlm(transform.getRotation());
  out.perspective = camera.perspective;
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

glm::mat4 buildProjection(const renderer::CameraData& camera) {
  if (camera.perspective) {
    return glm::perspective(glm::radians(camera.fov_y_degrees),
                            camera.aspect,
                            camera.near_clip,
                            camera.far_clip);
  }
  return glm::ortho(camera.ortho_left,
                    camera.ortho_right,
                    camera.ortho_bottom,
                    camera.ortho_top,
                    camera.near_clip,
                    camera.far_clip);
}

glm::mat4 buildView(const renderer::CameraData& camera) {
  const glm::mat3 cam_basis = glm::mat3_cast(camera.rotation);
  const glm::vec3 forward = cam_basis * glm::vec3(0.0f, 0.0f, -1.0f);
  const glm::vec3 up = cam_basis * glm::vec3(0.0f, 1.0f, 0.0f);
  return glm::lookAt(camera.position, camera.position + forward, up);
}
}

void RenderSystem::releaseRecord(uint64_t key, RenderRecord& record) {
  device_.retireInstance(static_cast<InstanceId>(key));
  if (record.material != renderer::kInvalidMaterial) {
    device_.destroyMaterial(record.material);
    record.material = renderer::kInvalidMaterial;
  }
  releaseSharedMesh(record.mesh_key);
  record.mesh = renderer::kInvalidMesh;
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

void RenderSystem::update(ecs::World& world, scene::Scene& /*scene*/, float /*dt*/) {
  static bool logged_start = false;
  if (!logged_start) {
    logged_start = true;
  }
  bool has_camera = false;
  renderer::CameraData primary_camera{};
  glm::mat4 projection(1.0f);
  glm::mat4 view(1.0f);
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
    const renderer::CameraData cam = toCameraData(camera, transform);

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
      projection = buildProjection(primary_camera);
      view = buildView(primary_camera);
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
    lights.push_back(toLightData(light_component, transform));
    if (!has_light && light_component.type == components::LightComponent::Type::Directional) {
      light = toDirectionalLight(light_component, transform);
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

  const FrustumPlanes frustum = extractFrustumPlanes(projection * view);
  const bool apply_frustum_culling = offscreen_passes.empty();

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
      record.mesh_key = mesh.mesh_key;
      record.material_key = mesh.material_key;
      record.material = kInvalidMaterial;
      acquireSharedMesh(mesh.mesh_key, record);
      it = records_.emplace(key, std::move(record)).first;
    } else if (it->second.mesh_key != mesh.mesh_key) {
      if (it->second.material != kInvalidMaterial) {
        device_.destroyMaterial(it->second.material);
        it->second.material = kInvalidMaterial;
      }
      releaseSharedMesh(it->second.mesh_key);
      it->second.mesh_key = mesh.mesh_key;
      it->second.material_key = mesh.material_key;
      it->second.material = kInvalidMaterial;
      acquireSharedMesh(mesh.mesh_key, it->second);
    } else if (it->second.material_key != mesh.material_key) {
      if (it->second.material != kInvalidMaterial) {
        device_.destroyMaterial(it->second.material);
        it->second.material = kInvalidMaterial;
      }
      it->second.material_key = mesh.material_key;
    }

    const glm::mat4 world_matrix = toTransform(transform);
    bool in_frustum = true;
    if (apply_frustum_culling && it->second.bounds_valid) {
      const glm::vec3 world_center = glm::vec3(world_matrix * glm::vec4(it->second.bounds_center, 1.0f));
      const glm::vec3 scale = toGlm(transform.getScale());
      const float max_scale = std::max(scale.x, std::max(scale.y, scale.z));
      const float world_radius = it->second.bounds_radius * max_scale;
      if (!sphereInFrustum(frustum, world_center, world_radius)) {
        in_frustum = false;
      }
    }

    DrawItem item{};
    item.instance = static_cast<InstanceId>(key);
    item.mesh = it->second.mesh;
    item.material = it->second.material;
    item.transform = world_matrix;
    item.layer = 0;
    item.visible = visible && in_frustum;
    item.shadow_visible = visible;
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
    drawBoxWire(device_, transform, collider.center, collider.half_extents, debug_color);
  });

  world.forEach<components::TransformComponent, components::SphereColliderComponent>(
      [&](const ecs::Entity entity) {
    const auto& collider = world.get<components::SphereColliderComponent>(entity);
    if (!collider.debug_draw) {
      return;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    drawSphereWire(device_, transform, collider.center, collider.radius, debug_color);
  });

  world.forEach<components::TransformComponent, components::CapsuleColliderComponent>(
      [&](const ecs::Entity entity) {
    const auto& collider = world.get<components::CapsuleColliderComponent>(entity);
    if (!collider.debug_draw) {
      return;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    drawCapsuleWire(device_, transform, collider.center, collider.radius, collider.height, debug_color);
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
                   record_it->second.bounds_radius, debug_color);
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
