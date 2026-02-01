#include "karma/renderer/render_system.h"

#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <limits>

#include "karma/components/camera.h"
#include "karma/components/environment.h"
#include "karma/components/light.h"

namespace karma::renderer {

namespace {
glm::vec3 toGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

glm::quat toGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

renderer::DirectionalLightData toDirectionalLight(const components::LightComponent& light,
                                                  const components::TransformComponent& transform) {
  renderer::DirectionalLightData out{};
  out.color = light.color;
  out.intensity = light.intensity;
  const glm::quat rot = toGlm(transform.rotation());
  const glm::mat3 basis = glm::mat3_cast(rot);
  out.direction = basis * glm::vec3(0.0f, 0.0f, -1.0f);
  out.position = toGlm(transform.position());
  out.shadow_extent = light.shadow_extent;
  return out;
}

glm::mat4 toTransform(const components::TransformComponent& transform) {
  const glm::vec3 pos = toGlm(transform.position());
  const glm::quat rot = toGlm(transform.rotation());
  const glm::vec3 scale = toGlm(transform.scale());
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

bool computeMeshBounds(const std::string& path, glm::vec3& out_center, float& out_radius) {
  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(path,
                                           aiProcess_Triangulate |
                                           aiProcess_JoinIdenticalVertices |
                                           aiProcess_PreTransformVertices);
  if (!scene || !scene->mRootNode) {
    spdlog::warn("Karma: Failed to compute bounds for '{}': {}", path, importer.GetErrorString());
    return false;
  }

  glm::vec3 min_v{std::numeric_limits<float>::max()};
  glm::vec3 max_v{std::numeric_limits<float>::lowest()};
  bool any = false;
  for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
    const aiMesh* mesh = scene->mMeshes[i];
    if (!mesh) {
      continue;
    }
    for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
      const aiVector3D& vert = mesh->mVertices[v];
      min_v.x = std::min(min_v.x, vert.x);
      min_v.y = std::min(min_v.y, vert.y);
      min_v.z = std::min(min_v.z, vert.z);
      max_v.x = std::max(max_v.x, vert.x);
      max_v.y = std::max(max_v.y, vert.y);
      max_v.z = std::max(max_v.z, vert.z);
      any = true;
    }
  }

  if (!any) {
    return false;
  }

  out_center = (min_v + max_v) * 0.5f;
  const glm::vec3 extents = max_v - min_v;
  out_radius = 0.5f * glm::length(extents);
  return true;
}
}

void RenderSystem::update(ecs::World& world, scene::Scene& /*scene*/, float /*dt*/) {
  static bool logged_start = false;
  if (!logged_start) {
    spdlog::warn("Karma: RenderSystem update running.");
    logged_start = true;
  }
  bool has_camera = false;
  glm::mat4 projection(1.0f);
  glm::mat4 view(1.0f);
  for (const ecs::Entity entity :
       world.view<components::CameraComponent, components::TransformComponent>()) {
    const auto& camera = world.get<components::CameraComponent>(entity);
    if (!camera.is_primary) {
      continue;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    CameraData cam{};
    cam.position = toGlm(transform.position());
    cam.rotation = toGlm(transform.rotation());
    cam.perspective = true;
    cam.fov_y_degrees = camera.fov_y_degrees;
    cam.aspect = 16.0f / 9.0f;
    cam.near_clip = camera.near_clip;
    cam.far_clip = camera.far_clip;
    device_.setCamera(cam);
    projection = glm::perspective(glm::radians(cam.fov_y_degrees),
                                  cam.aspect,
                                  cam.near_clip,
                                  cam.far_clip);
    const glm::mat3 cam_basis = glm::mat3_cast(cam.rotation);
    const glm::vec3 forward = cam_basis * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 up = cam_basis * glm::vec3(0.0f, 1.0f, 0.0f);
    view = glm::lookAt(cam.position, cam.position + forward, up);
    has_camera = true;
    break;
  }

  if (!has_camera) {
    if (!warned_no_camera_) {
      spdlog::warn("Karma: No primary camera found; rendering a blank frame.");
      warned_no_camera_ = true;
    }
    device_.setCameraActive(false);
    return;
  }
  warned_no_camera_ = false;
  device_.setCameraActive(true);

  renderer::DirectionalLightData light{};
  bool has_light = false;
  static bool warned_missing_light_transform = false;
  if (!warned_missing_light_transform) {
    for (const ecs::Entity entity : world.view<components::LightComponent>()) {
      if (!world.has<components::TransformComponent>(entity)) {
        spdlog::warn("Karma: LightComponent entity={} missing TransformComponent.", entityKey(entity));
        warned_missing_light_transform = true;
        break;
      }
    }
  }
  for (const ecs::Entity entity :
       world.view<components::LightComponent, components::TransformComponent>()) {
    const auto& light_component = world.get<components::LightComponent>(entity);
    if (light_component.type != components::LightComponent::Type::Directional) {
      continue;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    light = toDirectionalLight(light_component, transform);
    has_light = true;
    break;
  }
  if (!has_light) {
    light.direction = glm::vec3(0.3f, 1.0f, 0.2f);
    light.color = math::Color{1.0f, 1.0f, 1.0f, 1.0f};
    light.intensity = 1.0f;
  }
  device_.setDirectionalLight(light);

  bool env_found = false;
  for (const ecs::Entity entity : world.view<components::EnvironmentComponent>()) {
    const auto& env = world.get<components::EnvironmentComponent>(entity);
    if (!env.enabled) {
      continue;
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
    break;
  }
  if (!env_found &&
      (!last_env_path_.empty() || last_env_intensity_ >= 0.0f || last_env_draw_skybox_)) {
    device_.setEnvironmentMap({}, 0.0f, false);
    last_env_path_.clear();
    last_env_intensity_ = -1.0f;
    last_env_draw_skybox_ = false;
  }

  const FrustumPlanes frustum = extractFrustumPlanes(projection * view);

  for (const ecs::Entity entity :
       world.view<components::MeshComponent, components::TransformComponent>()) {
    const auto& mesh = world.get<components::MeshComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);

    bool visible = mesh.visible;
    if (world.has<components::VisibilityComponent>(entity)) {
      visible = visible && world.get<components::VisibilityComponent>(entity).visible;
    }

    const uint64_t key = entityKey(entity);
    auto it = records_.find(key);
    if (it == records_.end()) {
      const bool exists = !mesh.mesh_key.empty() && std::filesystem::exists(mesh.mesh_key);
      spdlog::warn("Karma: RenderSystem create record entity={} mesh='{}' exists={} material='{}'",
                   key,
                   mesh.mesh_key,
                   exists,
                   mesh.material_key);
      RenderRecord record;
      record.mesh_key = mesh.mesh_key;
      record.material_key = mesh.material_key;
      record.mesh = device_.createMeshFromFile(mesh.mesh_key);
      record.material = kInvalidMaterial;
      auto bounds_it = bounds_cache_.find(mesh.mesh_key);
      if (bounds_it == bounds_cache_.end()) {
        MeshBounds bounds{};
        bounds.valid = computeMeshBounds(mesh.mesh_key, bounds.center, bounds.radius);
        bounds_it = bounds_cache_.emplace(mesh.mesh_key, bounds).first;
      }
      record.bounds_valid = bounds_it->second.valid;
      record.bounds_center = bounds_it->second.center;
      record.bounds_radius = bounds_it->second.radius;
      it = records_.emplace(key, std::move(record)).first;
      spdlog::warn("Karma: RenderSystem created mesh id={} for entity={}", it->second.mesh, key);
    } else if (it->second.mesh_key != mesh.mesh_key) {
      const bool exists = !mesh.mesh_key.empty() && std::filesystem::exists(mesh.mesh_key);
      spdlog::warn("Karma: RenderSystem mesh changed entity={} mesh='{}' exists={}",
                   key,
                   mesh.mesh_key,
                   exists);
      it->second.mesh_key = mesh.mesh_key;
      it->second.mesh = device_.createMeshFromFile(mesh.mesh_key);
      auto bounds_it = bounds_cache_.find(mesh.mesh_key);
      if (bounds_it == bounds_cache_.end()) {
        MeshBounds bounds{};
        bounds.valid = computeMeshBounds(mesh.mesh_key, bounds.center, bounds.radius);
        bounds_it = bounds_cache_.emplace(mesh.mesh_key, bounds).first;
      }
      it->second.bounds_valid = bounds_it->second.valid;
      it->second.bounds_center = bounds_it->second.center;
      it->second.bounds_radius = bounds_it->second.radius;
      spdlog::warn("Karma: RenderSystem updated mesh id={} for entity={}", it->second.mesh, key);
    }

    const glm::mat4 world_matrix = toTransform(transform);
    bool in_frustum = true;
    if (it->second.bounds_valid) {
      const glm::vec3 world_center = glm::vec3(world_matrix * glm::vec4(it->second.bounds_center, 1.0f));
      const glm::vec3 scale = toGlm(transform.scale());
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
  }
}

}  // namespace karma::renderer
