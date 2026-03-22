#include "karma/volumes/volume_sphere_system.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include <glm/glm.hpp>

#include "karma/components/camera.h"
#include "karma/components/mesh.h"
#include "karma/components/transform.h"
#include "karma/components/volume_sphere.h"
#include "karma/math/quat.h"
#include "karma/math/vec3.h"
#include "karma/renderer/device.h"

namespace karma::volumes {

namespace {

constexpr float kFallbackAspect = 16.0f / 9.0f;

struct OverlayRectNdc {
  float min_x = -1.0f;
  float min_y = -1.0f;
  float max_x = 1.0f;
  float max_y = 1.0f;
  bool visible = true;
};

renderer::MeshData buildOverlayQuadMesh() {
  renderer::MeshData mesh{};
  mesh.vertices = {
      {-1.0f, -1.0f, 0.0f},
      {1.0f, -1.0f, 0.0f},
      {1.0f, 1.0f, 0.0f},
      {-1.0f, 1.0f, 0.0f},
  };
  mesh.normals = {
      {0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f},
  };
  mesh.uvs = {
      {0.0f, 0.0f},
      {1.0f, 0.0f},
      {1.0f, 1.0f},
      {0.0f, 1.0f},
  };
  mesh.tangents = {
      {1.0f, 0.0f, 0.0f, 1.0f},
      {1.0f, 0.0f, 0.0f, 1.0f},
      {1.0f, 0.0f, 0.0f, 1.0f},
      {1.0f, 0.0f, 0.0f, 1.0f},
  };
  mesh.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  return mesh;
}

float computeVolumeDensity(float radius, float center_opacity) {
  if (center_opacity >= 0.9999f) {
    return 1000.0f / std::max(radius * 2.0f, 1.0e-4f);
  }
  const float clamped_opacity = std::clamp(center_opacity, 0.001f, 0.999f);
  const float center_transmittance = 1.0f - clamped_opacity;
  return -std::log(center_transmittance) / std::max(radius * 2.0f, 1.0e-4f);
}

float maxAbsScale(const math::Vec3& scale) {
  return std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z), 1.0f});
}

float resolveVolumeRadius(const components::VolumeSphereComponent& sphere,
                          const components::TransformComponent& transform) {
  const float scale = sphere.scale_with_transform ? maxAbsScale(transform.getScale()) : 1.0f;
  return std::max(sphere.radius * scale, 1.0e-4f);
}

components::TransformComponent makeScreenOverlayTransform(
    const components::TransformComponent& camera_transform,
    float fov_y_degrees,
    float aspect,
    float depth,
    const OverlayRectNdc& rect) {
  const float half_height = std::tan(glm::radians(fov_y_degrees) * 0.5f) * depth;
  const float half_width = half_height * std::max(aspect, 1.0f);
  const float center_x = (rect.min_x + rect.max_x) * 0.5f;
  const float center_y = (rect.min_y + rect.max_y) * 0.5f;
  const float extent_x = std::max(rect.max_x - rect.min_x, 1.0e-4f) * 0.5f;
  const float extent_y = std::max(rect.max_y - rect.min_y, 1.0e-4f) * 0.5f;
  const math::Quat rotation = camera_transform.getRotation();
  const math::Vec3 camera_position = camera_transform.getPosition();
  const math::Vec3 forward = math::normalize(math::rotateVec(rotation, {0.0f, 0.0f, -1.0f}));
  const math::Vec3 right = math::normalize(math::rotateVec(rotation, {1.0f, 0.0f, 0.0f}));
  const math::Vec3 up = math::normalize(math::rotateVec(rotation, {0.0f, 1.0f, 0.0f}));

  components::TransformComponent transform{};
  transform.setPosition(
      {camera_position.x + forward.x * depth + right.x * (center_x * half_width) +
           up.x * (center_y * half_height),
       camera_position.y + forward.y * depth + right.y * (center_x * half_width) +
           up.y * (center_y * half_height),
       camera_position.z + forward.z * depth + right.z * (center_x * half_width) +
           up.z * (center_y * half_height)});
  transform.setRotation(rotation);
  transform.setScale({half_width * extent_x, half_height * extent_y, 1.0f});
  return transform;
}

OverlayRectNdc projectVolumeSphereOverlayRect(
    const components::TransformComponent& camera_transform,
    const components::CameraComponent& camera,
    float aspect,
    int framebuffer_width,
    int framebuffer_height,
    const math::Vec3& sphere_center,
    float sphere_radius) {
  OverlayRectNdc rect{};
  if (!camera.perspective || sphere_radius <= 0.0f) {
    rect.visible = sphere_radius > 0.0f;
    return rect;
  }

  const math::Quat camera_rotation = camera_transform.getRotation();
  const math::Vec3 camera_position = camera_transform.getPosition();
  const math::Vec3 forward =
      math::normalize(math::rotateVec(camera_rotation, {0.0f, 0.0f, -1.0f}));
  const math::Vec3 right = math::normalize(math::rotateVec(camera_rotation, {1.0f, 0.0f, 0.0f}));
  const math::Vec3 up = math::normalize(math::rotateVec(camera_rotation, {0.0f, 1.0f, 0.0f}));
  const math::Vec3 to_center{sphere_center.x - camera_position.x,
                             sphere_center.y - camera_position.y,
                             sphere_center.z - camera_position.z};
  if (math::lengthSquared(to_center) <= sphere_radius * sphere_radius) {
    return rect;
  }

  const float center_x = math::dot(to_center, right);
  const float center_y = math::dot(to_center, up);
  const float center_z = math::dot(to_center, forward);
  const float near_clip = std::max(camera.near_clip, 1.0e-3f);
  if (center_z <= sphere_radius + near_clip) {
    return rect;
  }

  const float tan_half_y = std::tan(glm::radians(camera.fov_y_degrees) * 0.5f);
  const float tan_half_x = tan_half_y * std::max(aspect, 1.0e-4f);
  if (tan_half_x <= 1.0e-5f || tan_half_y <= 1.0e-5f) {
    return rect;
  }

  float min_x = std::numeric_limits<float>::max();
  float min_y = std::numeric_limits<float>::max();
  float max_x = -std::numeric_limits<float>::max();
  float max_y = -std::numeric_limits<float>::max();
  bool any_projected = false;
  for (int sx = -1; sx <= 1; sx += 2) {
    for (int sy = -1; sy <= 1; sy += 2) {
      for (int sz = -1; sz <= 1; sz += 2) {
        const float sample_x = center_x + static_cast<float>(sx) * sphere_radius;
        const float sample_y = center_y + static_cast<float>(sy) * sphere_radius;
        const float sample_z = center_z + static_cast<float>(sz) * sphere_radius;
        if (sample_z <= near_clip) {
          return rect;
        }

        const float ndc_x = sample_x / (sample_z * tan_half_x);
        const float ndc_y = sample_y / (sample_z * tan_half_y);
        min_x = std::min(min_x, ndc_x);
        min_y = std::min(min_y, ndc_y);
        max_x = std::max(max_x, ndc_x);
        max_y = std::max(max_y, ndc_y);
        any_projected = true;
      }
    }
  }

  if (!any_projected || min_x >= 1.0f || max_x <= -1.0f || min_y >= 1.0f || max_y <= -1.0f) {
    rect.visible = false;
    return rect;
  }

  const float ndc_margin_x = framebuffer_width > 0 ? 4.0f / static_cast<float>(framebuffer_width)
                                                   : 0.01f;
  const float ndc_margin_y = framebuffer_height > 0
                                 ? 4.0f / static_cast<float>(framebuffer_height)
                                 : 0.01f;
  rect.min_x = std::clamp(min_x - ndc_margin_x, -1.0f, 1.0f);
  rect.min_y = std::clamp(min_y - ndc_margin_y, -1.0f, 1.0f);
  rect.max_x = std::clamp(max_x + ndc_margin_x, -1.0f, 1.0f);
  rect.max_y = std::clamp(max_y + ndc_margin_y, -1.0f, 1.0f);
  rect.visible = rect.min_x < rect.max_x && rect.min_y < rect.max_y;
  return rect;
}

renderer::MaterialDesc buildMaterialDesc(const components::VolumeSphereComponent& sphere,
                                         const components::TransformComponent& transform,
                                         float volume_radius) {
  renderer::MaterialDesc desc{};
  desc.base_color = sphere.color;
  desc.emissive_color = sphere.emissive_color;
  desc.metallic = 0.0f;
  desc.roughness = 1.0f;
  desc.unlit = true;
  desc.transparent = true;
  desc.blend_mode = renderer::MaterialDesc::BlendMode::Alpha;
  desc.double_sided = true;
  desc.depth_test = false;
  desc.depth_write = false;
  desc.shading_model = renderer::MaterialDesc::ShadingModel::VolumetricSphere;
  desc.volume_center = glm::vec3(transform.getPosition().x,
                                 transform.getPosition().y,
                                 transform.getPosition().z);
  desc.volume_radius = volume_radius;
  desc.volume_density = computeVolumeDensity(desc.volume_radius, sphere.center_opacity);
  desc.wave_distortion_strength = sphere.distortion_strength;
  desc.wave_noise_strength = sphere.noise_strength;
  return desc;
}

}  // namespace

VolumeSphereSystem::VolumeSphereSystem(renderer::GraphicsDevice* device) : device_(device) {}

VolumeSphereSystem::~VolumeSphereSystem() {
  destroySharedResources();
}

void VolumeSphereSystem::ensureSharedResources() {
  if (device_ == nullptr || overlay_mesh_ != renderer::kInvalidMesh) {
    return;
  }
  overlay_mesh_ = device_->createMesh(buildOverlayQuadMesh());
}

void VolumeSphereSystem::destroySharedResources() {
  if (device_ == nullptr) {
    return;
  }
  if (overlay_mesh_ != renderer::kInvalidMesh) {
    device_->destroyMesh(overlay_mesh_);
    overlay_mesh_ = renderer::kInvalidMesh;
  }
}

void VolumeSphereSystem::destroyRuntimeState(ecs::World& world, RuntimeState& state) {
  if (device_ != nullptr && state.material != renderer::kInvalidMaterial) {
    device_->destroyMaterial(state.material);
    state.material = renderer::kInvalidMaterial;
  }
  if (state.proxy.isValid() && world.isAlive(state.proxy)) {
    world.destroyEntity(state.proxy);
  }
  state.proxy = {};
}

VolumeSphereSystem::RuntimeState& VolumeSphereSystem::ensureRuntimeState(ecs::World& world,
                                                                         ecs::Entity source) {
  const uint64_t key = entityKey(source);
  auto it = runtime_.find(key);
  if (it != runtime_.end()) {
    return it->second;
  }

  RuntimeState state{};
  if (device_ != nullptr) {
    state.material = device_->createMaterial(renderer::MaterialDesc{});
  }
  state.proxy = world.createEntity();
  it = runtime_.emplace(key, std::move(state)).first;
  return it->second;
}

void VolumeSphereSystem::update(ecs::World& world, float dt, float interpolation_alpha) {
  (void)dt;

  ensureSharedResources();

  ecs::Entity primary_camera{};
  const components::CameraComponent* camera_component = nullptr;
  const components::TransformComponent* camera_transform = nullptr;
  world.forEach<components::CameraComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
        const auto& camera = world.get<components::CameraComponent>(entity);
        if (!primary_camera.isValid() && camera.is_primary) {
          primary_camera = entity;
          camera_component = &camera;
          camera_transform = &world.get<components::TransformComponent>(entity);
        }
        return true;
      });

  if (!primary_camera.isValid() || camera_component == nullptr || camera_transform == nullptr) {
    return;
  }

  int framebuffer_width = 0;
  int framebuffer_height = 0;
  if (device_ != nullptr) {
    device_->getFramebufferSize(framebuffer_width, framebuffer_height);
  }
  const float aspect =
      framebuffer_width > 0 && framebuffer_height > 0
          ? static_cast<float>(framebuffer_width) / static_cast<float>(framebuffer_height)
          : kFallbackAspect;
  const components::TransformComponent interpolated_camera_transform{
      camera_transform->getInterpolatedPosition(interpolation_alpha),
      camera_transform->getInterpolatedRotation(interpolation_alpha),
      camera_transform->getScale()};

  std::unordered_set<uint64_t> active_keys;
  const std::vector<ecs::Entity> entities =
      world.view<components::VolumeSphereComponent, components::TransformComponent>();
  for (const ecs::Entity entity : entities) {
    auto& sphere = world.get<components::VolumeSphereComponent>(entity);
    auto& source_transform = world.get<components::TransformComponent>(entity);
    const uint64_t key = entityKey(entity);
    active_keys.insert(key);

    RuntimeState& state = ensureRuntimeState(world, entity);
    if (!world.isAlive(state.proxy)) {
      state.proxy = world.createEntity();
    }
    if (!world.has<components::TransformComponent>(state.proxy)) {
      world.add(state.proxy, components::TransformComponent{});
    }
    if (!world.has<components::MeshComponent>(state.proxy)) {
      world.add(state.proxy,
                components::MeshComponent{
                    .mesh_id = overlay_mesh_,
                    .material_id = state.material,
                    .owns_material_id = false,
                    .visible = sphere.visible,
                    .shadow_visible = false,
                });
    }

    const components::TransformComponent world_transform{
        source_transform.getInterpolatedPosition(interpolation_alpha),
        source_transform.getInterpolatedRotation(interpolation_alpha),
        source_transform.getScale()};
    const float volume_radius = resolveVolumeRadius(sphere, world_transform);
    const OverlayRectNdc overlay_rect =
        projectVolumeSphereOverlayRect(interpolated_camera_transform,
                                       *camera_component,
                                       aspect,
                                       framebuffer_width,
                                       framebuffer_height,
                                       world_transform.getPosition(),
                                       volume_radius);
    components::TransformComponent overlay_transform =
        makeScreenOverlayTransform(interpolated_camera_transform,
                                   camera_component->fov_y_degrees,
                                   aspect,
                                   sphere.overlay_depth,
                                   overlay_rect);
    world.get<components::TransformComponent>(state.proxy) = overlay_transform;

    auto& mesh = world.get<components::MeshComponent>(state.proxy);
    mesh.mesh_id = overlay_mesh_;
    mesh.material_id = state.material;
    mesh.visible = sphere.visible && overlay_rect.visible;
    mesh.shadow_visible = false;

    if (device_ != nullptr && state.material != renderer::kInvalidMaterial) {
      device_->updateMaterial(state.material,
                              buildMaterialDesc(sphere, world_transform, volume_radius));
    }
  }

  for (auto it = runtime_.begin(); it != runtime_.end();) {
    if (active_keys.find(it->first) != active_keys.end()) {
      ++it;
      continue;
    }
    destroyRuntimeState(world, it->second);
    it = runtime_.erase(it);
  }
}

}  // namespace karma::volumes
