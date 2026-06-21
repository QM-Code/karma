#include "karma/visual.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/math.h"
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

float maxAbsPerpendicularScale(float a, float b) {
  return std::max({std::abs(a), std::abs(b), 1.0f});
}

glm::vec3 safeNormalize(const glm::vec3& value, const glm::vec3& fallback) {
  const float len_sq = glm::dot(value, value);
  if (len_sq <= 1.0e-8f) {
    return fallback;
  }
  return value * glm::inversesqrt(len_sq);
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
    const float length_scale = volume.scale_with_transform ? std::max(std::abs(scale.x), 1.0f)
                                                           : 1.0f;
    resolved.radius = std::max(volume.radius * radius_scale, kMinVolumeRadius);
    resolved.capsule_half_length =
        std::max(volume.capsule_half_length * length_scale, 0.0f);
    resolved.bounds_radius = resolved.capsule_half_length + resolved.radius;
  } else {
    const float radius_scale = volume.scale_with_transform ? maxAbsScale(scale) : 1.0f;
    resolved.radius = std::max(volume.radius * radius_scale, kMinVolumeRadius);
    resolved.capsule_half_length = 0.0f;
    resolved.bounds_radius = resolved.radius;
  }
  return resolved;
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

CameraFrame makeCameraFrame(const components::TransformComponent& camera_transform,
                            const components::CameraComponent& camera,
                            float aspect) {
  const math::Quat camera_rotation = camera_transform.getRotation();
  CameraFrame frame{};
  frame.position = toVolumeGlm(camera_transform.getPosition());
  frame.forward =
      safeNormalize(toVolumeGlm(math::rotateVec(camera_rotation, {0.0f, 0.0f, -1.0f})),
                    {0.0f, 0.0f, -1.0f});
  frame.right =
      safeNormalize(toVolumeGlm(math::rotateVec(camera_rotation, {1.0f, 0.0f, 0.0f})),
                    {1.0f, 0.0f, 0.0f});
  frame.up =
      safeNormalize(toVolumeGlm(math::rotateVec(camera_rotation, {0.0f, 1.0f, 0.0f})),
                    {0.0f, 1.0f, 0.0f});
  frame.tan_half_y = std::tan(glm::radians(camera.fov_y_degrees) * 0.5f);
  frame.tan_half_x = frame.tan_half_y * std::max(aspect, 1.0e-4f);
  frame.near_clip = std::max(camera.near_clip, 1.0e-3f);
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
  const float radius_x = radius / (center_z * frame.tan_half_x);
  const float radius_y = radius / (center_z * frame.tan_half_y);

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

float resolveVolumeDensity(const components::VolumetricComponent& volume, float radius) {
  if (volume.density > 0.0f) {
    return volume.density;
  }
  return computeVolumeDensity(radius, volume.center_opacity);
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

rendering::ResolvedMaterialDesc buildMaterialDesc(const components::VolumetricComponent& volume,
                                                 const ResolvedVolume& resolved) {
  rendering::ResolvedMaterialDesc desc{};
  desc.pipeline.name = "volumetric_solid";
  desc.surface.base_color = volume.color;
  desc.surface.emissive_color = volume.emissive_color;
  desc.surface.metallic = 0.0f;
  desc.surface.roughness = 1.0f;
  desc.surface.unlit = true;
  desc.surface.transparent = true;
  desc.surface.blend_mode = rendering::MaterialDesc::BlendMode::Alpha;
  desc.surface.double_sided = true;
  desc.surface.depth_test = false;
  desc.surface.depth_write = false;
  desc.params["volume_shape"] = volumeShapeId(volume.shape);
  desc.params["volume_center"] = resolved.center;
  desc.params["volume_axis_x"] = resolved.axis_x;
  desc.params["volume_axis_y"] = resolved.axis_y;
  desc.params["volume_axis_z"] = resolved.axis_z;
  desc.params["volume_radius"] = resolved.radius;
  desc.params["volume_capsule_half_length"] = resolved.capsule_half_length;
  desc.params["volume_density"] = resolveVolumeDensity(volume, resolved.radius);
  desc.params["volume_scattering"] = std::max(volume.scattering, 0.0f);
  desc.params["volume_anisotropy"] = std::clamp(volume.anisotropy, -0.95f, 0.95f);
  desc.params["volume_absorption"] = std::max(volume.absorption, 0.0f);
  desc.params["volume_distortion_strength"] = std::max(volume.distortion_strength, 0.0f);
  desc.params["volume_noise_strength"] = std::max(volume.noise_strength, 0.0f);
  return desc;
}

}  // namespace

VolumeSystem::VolumeSystem(rendering::GraphicsDevice* device) : device_(device) {}

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
    (void)key;
    destroyRuntimeState(state);
  }
  runtime_.clear();
  if (overlay_mesh_ != rendering::kInvalidMesh) {
    device_->destroyMesh(overlay_mesh_);
    overlay_mesh_ = rendering::kInvalidMesh;
  }
}

void VolumeSystem::destroyRuntimeState(RuntimeState& state) {
  if (device_ != nullptr && state.material != rendering::kInvalidMaterial) {
    device_->destroyMaterial(state.material);
    state.material = rendering::kInvalidMaterial;
  }
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
  const CameraFrame camera_frame =
      makeCameraFrame(interpolated_camera_transform, *camera_component, aspect);

  std::unordered_set<uint64_t> active_keys;
  const std::vector<world::Entity> entities =
      world.view<components::VolumetricComponent, components::TransformComponent>();
  for (const world::Entity entity : entities) {
    auto& volume = world.get<components::VolumetricComponent>(entity);
    auto& source_transform = world.get<components::TransformComponent>(entity);
    const uint64_t key = entityKey(entity);
    active_keys.insert(key);

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
                                   camera_component->fov_y_degrees,
                                   aspect,
                                   volume.overlay_depth,
                                   overlay_rect);
    if (device_ != nullptr) {
      if (state.material != rendering::kInvalidMaterial) {
        device_->destroyMaterial(state.material);
      }
      state.material = device_->createMaterial(buildMaterialDesc(volume, resolved));
    }
    if (device_ != nullptr && state.material != rendering::kInvalidMaterial) {
      rendering::DrawItem item{};
      item.instance = key;
      item.mesh = overlay_mesh_;
      item.material = state.material;
      item.transform = toGlmMat4(overlay_transform);
      item.visible = volume.visible && overlay_rect.visible;
      item.shadow_visible = false;
      device_->submit(item);
    }
  }

  for (auto it = runtime_.begin(); it != runtime_.end();) {
    if (active_keys.find(it->first) != active_keys.end()) {
      ++it;
      continue;
    }
    destroyRuntimeState(it->second);
    it = runtime_.erase(it);
  }
}

}  // namespace karma::visual::volumes
