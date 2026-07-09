#include "karma/visual.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/math.h"
#include "karma/rendering.h"

namespace karma::visual::volumes {

namespace {

constexpr float kFallbackAspect = 16.0f / 9.0f;
constexpr float kMinVolumeRadius = 1.0e-4f;

struct OverlayRectNdc {
  float min_x = -1.0f;
  float min_y = -1.0f;
  float max_x = 1.0f;
  float max_y = 1.0f;
  bool visible = true;
};

struct CameraFrame {
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::vec3 forward{0.0f, 0.0f, -1.0f};
  glm::vec3 right{1.0f, 0.0f, 0.0f};
  glm::vec3 up{0.0f, 1.0f, 0.0f};
  float tan_half_x = 1.0f;
  float tan_half_y = 1.0f;
  float near_clip = 0.001f;
  bool perspective = true;
};

struct ResolvedVolume {
  glm::vec3 center{0.0f, 0.0f, 0.0f};
  glm::vec3 axis_x{1.0f, 0.0f, 0.0f};
  glm::vec3 axis_y{0.0f, 1.0f, 0.0f};
  glm::vec3 axis_z{0.0f, 0.0f, 1.0f};
  float radius = 1.0f;
  float capsule_half_length = 0.0f;
  float bounds_radius = 1.0f;
};

world::MeshData buildOverlayQuadMesh() {
  world::MeshData mesh{};
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

float maxAbsScale(const math::Vec3& scale) {
  const auto finite_abs = [](float value) {
    return std::isfinite(value) ? std::abs(value) : 0.0f;
  };
  return std::max({finite_abs(scale.x), finite_abs(scale.y), finite_abs(scale.z)});
}

float maxAbsPerpendicularScale(float a, float b) {
  const float abs_a = std::isfinite(a) ? std::abs(a) : 0.0f;
  const float abs_b = std::isfinite(b) ? std::abs(b) : 0.0f;
  return std::max(abs_a, abs_b);
}

glm::vec3 safeNormalize(const glm::vec3& value, const glm::vec3& fallback) {
  const float len_sq = glm::dot(value, value);
  if (!std::isfinite(len_sq) || len_sq <= 1.0e-8f) {
    return fallback;
  }
  return value * glm::inversesqrt(len_sq);
}

float finiteNonNegative(float value) {
  return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
}

glm::vec3 toVolumeGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

glm::quat toVolumeGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

glm::mat4 toGlmMat4(const components::TransformComponent& transform) {
  glm::mat4 matrix(1.0f);
  matrix = glm::translate(matrix, toVolumeGlm(transform.getPosition()));
  matrix *= glm::mat4_cast(toVolumeGlm(transform.getRotation()));
  matrix = glm::scale(matrix, toVolumeGlm(transform.getScale()));
  return matrix;
}

ResolvedVolume resolveVolume(const components::VolumetricComponent& volume,
                             const components::TransformComponent& transform) {
  ResolvedVolume resolved{};
  resolved.center = toVolumeGlm(transform.getPosition());
  if (!std::isfinite(resolved.center.x) || !std::isfinite(resolved.center.y) ||
      !std::isfinite(resolved.center.z)) {
    resolved.center = glm::vec3(0.0f);
  }
  resolved.axis_x =
      safeNormalize(toVolumeGlm(math::rotateVec(transform.getRotation(), {1.0f, 0.0f, 0.0f})),
                    {1.0f, 0.0f, 0.0f});
  resolved.axis_y =
      safeNormalize(toVolumeGlm(math::rotateVec(transform.getRotation(), {0.0f, 1.0f, 0.0f})),
                    {0.0f, 1.0f, 0.0f});
  resolved.axis_z =
      safeNormalize(toVolumeGlm(math::rotateVec(transform.getRotation(), {0.0f, 0.0f, 1.0f})),
                    {0.0f, 0.0f, 1.0f});

  const math::Vec3 scale = transform.getScale();
  if (volume.shape == components::VolumetricShape::Capsule) {
    const float radius_scale = volume.scale_with_transform
                                   ? maxAbsPerpendicularScale(scale.y, scale.z)
                                   : 1.0f;
    const float length_scale = volume.scale_with_transform ? finiteNonNegative(std::abs(scale.x))
                                                           : 1.0f;
    resolved.radius = std::max(finiteNonNegative(volume.radius) * radius_scale,
                               kMinVolumeRadius);
    resolved.capsule_half_length =
        finiteNonNegative(volume.capsule_half_length) * length_scale;
    resolved.bounds_radius = resolved.capsule_half_length + resolved.radius;
  } else {
    const float radius_scale = volume.scale_with_transform ? maxAbsScale(scale) : 1.0f;
    resolved.radius = std::max(finiteNonNegative(volume.radius) * radius_scale,
                               kMinVolumeRadius);
    resolved.capsule_half_length = 0.0f;
    resolved.bounds_radius = resolved.radius;
  }
  return resolved;
}

components::TransformComponent makeScreenOverlayTransform(
    const components::TransformComponent& camera_transform,
    const components::CameraComponent& camera,
    float aspect,
    float depth,
    const OverlayRectNdc& rect) {
  float half_width = 1.0f;
  float half_height = 1.0f;
  float view_center_x = 0.0f;
  float view_center_y = 0.0f;
  if (camera.perspective) {
    const float fov = std::clamp(
        std::isfinite(camera.fov_y_degrees) ? camera.fov_y_degrees : 60.0f,
        1.0f,
        179.0f);
    half_height = std::tan(glm::radians(fov) * 0.5f) * depth;
    half_width = half_height * std::max(aspect, 1.0e-4f);
  } else {
    const float left = std::isfinite(camera.ortho_left) ? camera.ortho_left : -1.0f;
    const float right = std::isfinite(camera.ortho_right) ? camera.ortho_right : 1.0f;
    const float bottom = std::isfinite(camera.ortho_bottom) ? camera.ortho_bottom : -1.0f;
    const float top = std::isfinite(camera.ortho_top) ? camera.ortho_top : 1.0f;
    half_width = std::max(std::abs(right - left) * 0.5f, 1.0e-4f);
    half_height = std::max(std::abs(top - bottom) * 0.5f, 1.0e-4f);
    view_center_x = (left + right) * 0.5f;
    view_center_y = (bottom + top) * 0.5f;
  }
  const float center_x = (rect.min_x + rect.max_x) * 0.5f;
  const float center_y = (rect.min_y + rect.max_y) * 0.5f;
  const float extent_x = std::max(rect.max_x - rect.min_x, 1.0e-4f) * 0.5f;
  const float extent_y = std::max(rect.max_y - rect.min_y, 1.0e-4f) * 0.5f;
  const math::Quat rotation = math::normalize(camera_transform.getRotation());
  const math::Vec3 camera_position =
      math::isFinite(camera_transform.getPosition()) ? camera_transform.getPosition()
                                                     : math::Vec3{};
  const math::Vec3 forward = math::normalize(math::rotateVec(rotation, {0.0f, 0.0f, -1.0f}));
  const math::Vec3 right = math::normalize(math::rotateVec(rotation, {1.0f, 0.0f, 0.0f}));
  const math::Vec3 up = math::normalize(math::rotateVec(rotation, {0.0f, 1.0f, 0.0f}));

  components::TransformComponent transform{};
  transform.setPosition(
      {camera_position.x + forward.x * depth +
           right.x * (view_center_x + center_x * half_width) +
           up.x * (view_center_y + center_y * half_height),
       camera_position.y + forward.y * depth +
           right.y * (view_center_x + center_x * half_width) +
           up.y * (view_center_y + center_y * half_height),
       camera_position.z + forward.z * depth +
           right.z * (view_center_x + center_x * half_width) +
           up.z * (view_center_y + center_y * half_height)});
  transform.setRotation(rotation);
  transform.setScale({half_width * extent_x, half_height * extent_y, 1.0f});
  return transform;
}

CameraFrame makeCameraFrame(const components::TransformComponent& camera_transform,
                            const components::CameraComponent& camera,
                            float aspect) {
  const math::Quat camera_rotation = math::normalize(camera_transform.getRotation());
  CameraFrame frame{};
  frame.position = toVolumeGlm(camera_transform.getPosition());
  if (!std::isfinite(frame.position.x) || !std::isfinite(frame.position.y) ||
      !std::isfinite(frame.position.z)) {
    frame.position = glm::vec3(0.0f);
  }
  frame.forward =
      safeNormalize(toVolumeGlm(math::rotateVec(camera_rotation, {0.0f, 0.0f, -1.0f})),
                    {0.0f, 0.0f, -1.0f});
  frame.right =
      safeNormalize(toVolumeGlm(math::rotateVec(camera_rotation, {1.0f, 0.0f, 0.0f})),
                    {1.0f, 0.0f, 0.0f});
  frame.up =
      safeNormalize(toVolumeGlm(math::rotateVec(camera_rotation, {0.0f, 1.0f, 0.0f})),
                    {0.0f, 1.0f, 0.0f});
  const float fov = std::clamp(
      std::isfinite(camera.fov_y_degrees) ? camera.fov_y_degrees : 60.0f,
      1.0f,
      179.0f);
  frame.tan_half_y = std::tan(glm::radians(fov) * 0.5f);
  frame.tan_half_x = frame.tan_half_y * std::max(aspect, 1.0e-4f);
  frame.near_clip = std::max(
      std::isfinite(camera.near_clip) ? camera.near_clip : 0.1f, 1.0e-3f);
  frame.perspective = camera.perspective && frame.tan_half_x > 1.0e-5f &&
                      frame.tan_half_y > 1.0e-5f;
  return frame;
}

float cameraSpaceDepth(const CameraFrame& frame, const glm::vec3& point) {
  return glm::dot(point - frame.position, frame.forward);
}

bool cameraInsideVolume(const CameraFrame& frame,
                        const ResolvedVolume& resolved,
                        components::VolumetricShape shape) {
  const glm::vec3 offset = frame.position - resolved.center;
  glm::vec3 nearest_delta = offset;
  if (shape == components::VolumetricShape::Capsule) {
    const float axis_pos =
        std::clamp(glm::dot(offset, resolved.axis_x),
                   -resolved.capsule_half_length,
                   resolved.capsule_half_length);
    nearest_delta = offset - resolved.axis_x * axis_pos;
  }
  return glm::dot(nearest_delta, nearest_delta) <= resolved.radius * resolved.radius;
}

void includeProjectedSphere(OverlayRectNdc& rect,
                            bool& any_projected,
                            const CameraFrame& frame,
                            const glm::vec3& center,
                            float radius) {
  const glm::vec3 to_center = center - frame.position;
  const float center_z = glm::dot(to_center, frame.forward);
  if (center_z <= frame.near_clip) {
    return;
  }

  const float center_x = glm::dot(to_center, frame.right);
  const float center_y = glm::dot(to_center, frame.up);
  const float ndc_x = center_x / (center_z * frame.tan_half_x);
  const float ndc_y = center_y / (center_z * frame.tan_half_y);
  const float projected_depth =
      std::sqrt(std::max(center_z * center_z - radius * radius,
                         frame.near_clip * frame.near_clip));
  const float conservative_radius = radius * 1.12f;
  const float radius_x = conservative_radius / (projected_depth * frame.tan_half_x);
  const float radius_y = conservative_radius / (projected_depth * frame.tan_half_y);

  rect.min_x = std::min(rect.min_x, ndc_x - radius_x);
  rect.min_y = std::min(rect.min_y, ndc_y - radius_y);
  rect.max_x = std::max(rect.max_x, ndc_x + radius_x);
  rect.max_y = std::max(rect.max_y, ndc_y + radius_y);
  any_projected = true;
}

OverlayRectNdc projectVolumeOverlayRect(const CameraFrame& frame,
                                        const ResolvedVolume& resolved,
                                        components::VolumetricShape shape,
                                        int framebuffer_width,
                                        int framebuffer_height) {
  OverlayRectNdc rect{};
  if (!frame.perspective || resolved.radius <= 0.0f) {
    rect.visible = resolved.radius > 0.0f;
    return rect;
  }

  if (cameraInsideVolume(frame, resolved, shape)) {
    return rect;
  }

  rect.min_x = std::numeric_limits<float>::max();
  rect.min_y = std::numeric_limits<float>::max();
  rect.max_x = -std::numeric_limits<float>::max();
  rect.max_y = -std::numeric_limits<float>::max();

  bool any_projected = false;
  if (shape == components::VolumetricShape::Capsule) {
    const glm::vec3 endpoint_a =
        resolved.center - resolved.axis_x * resolved.capsule_half_length;
    const glm::vec3 endpoint_b =
        resolved.center + resolved.axis_x * resolved.capsule_half_length;
    const float depth_a = cameraSpaceDepth(frame, endpoint_a);
    const float depth_b = cameraSpaceDepth(frame, endpoint_b);
    if (std::max(depth_a, depth_b) <= -resolved.radius) {
      rect.visible = false;
      return rect;
    }
    if (std::min(depth_a, depth_b) <= frame.near_clip + resolved.radius) {
      return OverlayRectNdc{};
    }
    includeProjectedSphere(rect, any_projected, frame, endpoint_a, resolved.radius);
    includeProjectedSphere(rect, any_projected, frame, endpoint_b, resolved.radius);
  } else {
    const float depth = cameraSpaceDepth(frame, resolved.center);
    if (depth <= -resolved.radius) {
      rect.visible = false;
      return rect;
    }
    if (depth <= frame.near_clip + resolved.radius) {
      return OverlayRectNdc{};
    }
    includeProjectedSphere(rect, any_projected, frame, resolved.center, resolved.radius);
  }

  if (!any_projected || rect.min_x >= 1.0f || rect.max_x <= -1.0f || rect.min_y >= 1.0f ||
      rect.max_y <= -1.0f) {
    rect.visible = false;
    return rect;
  }

  const float ndc_margin_x = framebuffer_width > 0 ? 4.0f / static_cast<float>(framebuffer_width)
                                                   : 0.01f;
  const float ndc_margin_y = framebuffer_height > 0
                                 ? 4.0f / static_cast<float>(framebuffer_height)
                                 : 0.01f;
  rect.min_x = std::clamp(rect.min_x - ndc_margin_x, -1.0f, 1.0f);
  rect.min_y = std::clamp(rect.min_y - ndc_margin_y, -1.0f, 1.0f);
  rect.max_x = std::clamp(rect.max_x + ndc_margin_x, -1.0f, 1.0f);
  rect.max_y = std::clamp(rect.max_y + ndc_margin_y, -1.0f, 1.0f);
  rect.visible = rect.min_x < rect.max_x && rect.min_y < rect.max_y;
  return rect;
}

uint32_t volumeShapeId(components::VolumetricShape shape) {
  switch (shape) {
    case components::VolumetricShape::Capsule:
      return 1u;
    case components::VolumetricShape::Sphere:
      return 0u;
  }
  return 0u;
}

rendering::InstanceId volumeSlotInstance(uint64_t key, uint32_t slot_id) {
  uint64_t instance = key ^ 0x9e3779b97f4a7c15ull;
  instance ^= static_cast<uint64_t>(slot_id) + 0x9e3779b97f4a7c15ull +
              (instance << 6u) + (instance >> 2u);
  return instance == rendering::kInvalidInstance ? instance - 1u : instance;
}

rendering::VolumeDrawParams buildVolumeDrawParams(
    const components::VolumetricComponent& volume,
    const ResolvedVolume& resolved,
    uint32_t slot_id) {
  rendering::VolumeDrawParams params{};
  params.center = resolved.center;
  params.axis_x = resolved.axis_x;
  params.axis_y = resolved.axis_y;
  params.axis_z = resolved.axis_z;
  params.radius = resolved.radius;
  params.capsule_half_length = resolved.capsule_half_length;
  params.shape = volumeShapeId(volume.shape);
  params.slot = slot_id;
  params.overlay_depth = finiteNonNegative(volume.overlay_depth);
  params.surface_double_sided = volume.surface_double_sided;
  return params;
}

void retireVolumeSlotInstances(rendering::GraphicsDevice* device, uint64_t key) {
  if (device == nullptr) {
    return;
  }
  device->retireInstance(volumeSlotInstance(key, 0u));
  device->retireInstance(volumeSlotInstance(key, 1u));
}

}  // namespace

VolumeSystem::VolumeSystem(rendering::GraphicsDevice* device,
                           const assets::AssetRegistry* assets)
    : device_(device), assets_(assets) {}

VolumeSystem::~VolumeSystem() {
  destroySharedResources();
}

void VolumeSystem::ensureSharedResources() {
  if (device_ == nullptr || overlay_mesh_ != rendering::kInvalidMesh) {
    return;
  }
  overlay_mesh_ = device_->createMesh(buildOverlayQuadMesh());
}

void VolumeSystem::destroySharedResources() {
  if (device_ == nullptr) {
    return;
  }
  for (auto& [key, state] : runtime_) {
    destroyRuntimeState(state);
    retireVolumeSlotInstances(device_, key);
  }
  runtime_.clear();
  if (overlay_mesh_ != rendering::kInvalidMesh) {
    device_->destroyMesh(overlay_mesh_);
    overlay_mesh_ = rendering::kInvalidMesh;
  }
}

void VolumeSystem::destroyRuntimeState(RuntimeState& state) {
  if (device_ != nullptr && state.interior_material != rendering::kInvalidMaterial) {
    device_->destroyMaterial(state.interior_material);
    state.interior_material = rendering::kInvalidMaterial;
  }
  if (device_ != nullptr && state.surface_material != rendering::kInvalidMaterial) {
    device_->destroyMaterial(state.surface_material);
    state.surface_material = rendering::kInvalidMaterial;
  }
  state.interior_material_key.clear();
  state.surface_material_key.clear();
  state.material_registry_version = 0u;
}

VolumeSystem::RuntimeState& VolumeSystem::ensureRuntimeState(world::Entity source) {
  const uint64_t key = entityKey(source);
  auto it = runtime_.find(key);
  if (it != runtime_.end()) {
    return it->second;
  }

  RuntimeState state{};
  it = runtime_.emplace(key, std::move(state)).first;
  return it->second;
}

void VolumeSystem::update(world::World& world, float dt, float interpolation_alpha) {
  (void)dt;

  ensureSharedResources();

  const std::vector<world::Entity> entities =
      world.view<components::VolumetricComponent, components::TransformComponent>();
  std::unordered_set<uint64_t> active_keys;
  active_keys.reserve(entities.size());
  for (const world::Entity entity : entities) {
    active_keys.insert(entityKey(entity));
  }
  auto cleanup_stale_states = [&] {
    for (auto it = runtime_.begin(); it != runtime_.end();) {
      if (active_keys.contains(it->first)) {
        ++it;
        continue;
      }
      destroyRuntimeState(it->second);
      retireVolumeSlotInstances(device_, it->first);
      it = runtime_.erase(it);
    }
  };

  world::Entity primary_camera{};
  const components::CameraComponent* camera_component = nullptr;
  const components::TransformComponent* camera_transform = nullptr;
  world.forEach<components::CameraComponent, components::TransformComponent>(
      [&](const world::Entity entity) {
        const auto& camera = world.get<components::CameraComponent>(entity);
        if (!primary_camera.isValid() && camera.is_primary) {
          primary_camera = entity;
          camera_component = &camera;
          camera_transform = &world.get<components::TransformComponent>(entity);
          return false;
        }
        return true;
      });

  if (!primary_camera.isValid() || camera_component == nullptr || camera_transform == nullptr) {
    cleanup_stale_states();
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
  const CameraFrame camera_frame =
      makeCameraFrame(interpolated_camera_transform, *camera_component, aspect);

  for (const world::Entity entity : entities) {
    auto& volume = world.get<components::VolumetricComponent>(entity);
    auto& source_transform = world.get<components::TransformComponent>(entity);
    const uint64_t key = entityKey(entity);
    RuntimeState& state = ensureRuntimeState(entity);

    const components::TransformComponent world_transform{
        source_transform.getInterpolatedPosition(interpolation_alpha),
        source_transform.getInterpolatedRotation(interpolation_alpha),
        source_transform.getScale()};
    const ResolvedVolume resolved = resolveVolume(volume, world_transform);
    const OverlayRectNdc overlay_rect =
        projectVolumeOverlayRect(camera_frame,
                                 resolved,
                                 volume.shape,
                                 framebuffer_width,
                                 framebuffer_height);
    components::TransformComponent overlay_transform =
        makeScreenOverlayTransform(interpolated_camera_transform,
                                   *camera_component,
                                   aspect,
                                   std::max(finiteNonNegative(volume.overlay_depth),
                                            camera_frame.near_clip + 1.0e-3f),
                                   overlay_rect);
    auto sync_material_slot = [&](const std::string& key,
                                  uint32_t slot_id,
                                  rendering::MaterialId& material,
                                  std::string& cached_key) {
      const uint64_t registry_version = assets_ != nullptr ? assets_->version() : 0u;
      if (key.empty()) {
        if (material != rendering::kInvalidMaterial && device_ != nullptr) {
          device_->destroyMaterial(material);
        }
        material = rendering::kInvalidMaterial;
        cached_key.clear();
        if (device_ != nullptr) {
          device_->retireInstance(volumeSlotInstance(entityKey(entity), slot_id));
        }
        return;
      }
      if (material != rendering::kInvalidMaterial &&
          cached_key == key &&
          state.material_registry_version == registry_version) {
        return;
      }
      if (material != rendering::kInvalidMaterial && device_ != nullptr) {
        device_->destroyMaterial(material);
        material = rendering::kInvalidMaterial;
      }
      cached_key = key;
      if (assets_ == nullptr || device_ == nullptr) {
        spdlog::error("VolumetricComponent material '{}' cannot be resolved without an AssetRegistry",
                      key);
        return;
      }
      std::optional<rendering::ResolvedMaterialDesc> resolved_material =
          assets_->resolveMaterial(key);
      if (!resolved_material.has_value()) {
        spdlog::error("VolumetricComponent material '{}' is not registered", key);
        return;
      }
      material = device_->createMaterial(*resolved_material);
      if (material == rendering::kInvalidMaterial) {
        spdlog::error("VolumetricComponent material '{}' failed to create", key);
      }
    };

    sync_material_slot(volume.interior_material_key,
                       0u,
                       state.interior_material,
                       state.interior_material_key);
    sync_material_slot(volume.surface_material_key,
                       1u,
                       state.surface_material,
                       state.surface_material_key);
    state.material_registry_version = assets_ != nullptr ? assets_->version() : 0u;

    auto submit_volume_slot = [&](rendering::MaterialId material, uint32_t slot_id) {
      if (device_ == nullptr || overlay_mesh_ == rendering::kInvalidMesh ||
          material == rendering::kInvalidMaterial) {
        return;
      }
      rendering::DrawItem item{};
      item.instance = volumeSlotInstance(key, slot_id);
      item.mesh = overlay_mesh_;
      item.material = material;
      item.transform = toGlmMat4(overlay_transform);
      item.visible = volume.visible && overlay_rect.visible;
      item.shadow_visible = false;
      item.volume_params = buildVolumeDrawParams(volume, resolved, slot_id);
      item.has_volume_params = true;
      item.requires_scene_sample = true;
      item.post_particle_scene_sample = true;
      device_->submit(item);
    };
    submit_volume_slot(state.interior_material, 0u);
    submit_volume_slot(state.surface_material, 1u);
  }

  cleanup_stale_states();
}

}  // namespace karma::visual::volumes
