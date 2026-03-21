#include "karma/volumes/volume_sphere_system.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include <glm/glm.hpp>

#include "karma/components/camera.h"
#include "karma/components/mesh.h"
#include "karma/components/transform.h"
#include "karma/components/volume_sphere.h"
#include "karma/math/quat.h"
#include "karma/renderer/device.h"

namespace karma::volumes {

namespace {

constexpr float kFallbackAspect = 16.0f / 9.0f;

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

components::TransformComponent makeScreenOverlayTransform(
    const components::TransformComponent& camera_transform,
    float fov_y_degrees,
    float aspect,
    float depth) {
  const float half_height = std::tan(glm::radians(fov_y_degrees) * 0.5f) * depth;
  const float half_width = half_height * std::max(aspect, 1.0f);
  const math::Quat rotation = camera_transform.getRotation();
  const math::Vec3 camera_position = camera_transform.getPosition();
  const math::Vec3 raw_forward = math::rotateVec(rotation, {0.0f, 0.0f, -1.0f});
  const float forward_length =
      std::sqrt(raw_forward.x * raw_forward.x + raw_forward.y * raw_forward.y +
                raw_forward.z * raw_forward.z);
  const math::Vec3 forward = forward_length > 1.0e-5f
                                 ? math::Vec3{raw_forward.x / forward_length,
                                              raw_forward.y / forward_length,
                                              raw_forward.z / forward_length}
                                 : math::Vec3{0.0f, 0.0f, -1.0f};

  components::TransformComponent transform{};
  transform.setPosition(
      {camera_position.x + forward.x * depth,
       camera_position.y + forward.y * depth,
       camera_position.z + forward.z * depth});
  transform.setRotation(rotation);
  transform.setScale({half_width, half_height, 1.0f});
  return transform;
}

float maxAbsScale(const math::Vec3& scale) {
  return std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z), 1.0f});
}

renderer::MaterialDesc buildMaterialDesc(const components::VolumeSphereComponent& sphere,
                                         const components::TransformComponent& transform) {
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
  const float scale = sphere.scale_with_transform ? maxAbsScale(transform.getScale()) : 1.0f;
  desc.volume_radius = std::max(sphere.radius * scale, 1.0e-4f);
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

    components::TransformComponent overlay_transform =
        makeScreenOverlayTransform(*camera_transform,
                                   camera_component->fov_y_degrees,
                                   aspect,
                                   sphere.overlay_depth);
    world.get<components::TransformComponent>(state.proxy) = overlay_transform;

    auto& mesh = world.get<components::MeshComponent>(state.proxy);
    mesh.mesh_id = overlay_mesh_;
    mesh.material_id = state.material;
    mesh.visible = sphere.visible;
    mesh.shadow_visible = false;

    if (device_ != nullptr && state.material != renderer::kInvalidMaterial) {
      const components::TransformComponent world_transform{
          source_transform.getInterpolatedPosition(interpolation_alpha),
          source_transform.getInterpolatedRotation(interpolation_alpha),
          source_transform.getScale()};
      device_->updateMaterial(state.material, buildMaterialDesc(sphere, world_transform));
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
