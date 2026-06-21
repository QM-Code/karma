#include "../backend.hpp"

#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace karma::rendering::backend {

namespace {

struct alignas(16) InstanceGpuData {
  float col0[4];
  float col1[4];
  float col2[4];
  float col3[4];
  float params[4];
};

template <typename T, bool KeepStrongReferences = false>
T* getMappedData(Diligent::MapHelper<T, KeepStrongReferences>& map) {
  return static_cast<T*>(map);
}

bool uploadInstanceData(Diligent::IDeviceContext* context,
                        Diligent::IBuffer* buffer,
                        const InstanceGpuData* instances,
                        size_t instance_count) {
  if (!context || !buffer || !instances || instance_count == 0) {
    return false;
  }

  Diligent::PVoid mapped_data = nullptr;
  context->MapBuffer(buffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD, mapped_data);
  if (mapped_data != nullptr) {
    std::memcpy(mapped_data, instances, instance_count * sizeof(InstanceGpuData));
  }
  context->UnmapBuffer(buffer, Diligent::MAP_WRITE);
  return mapped_data != nullptr;
}

InstanceGpuData packInstanceTransform(const glm::mat4& transform,
                                      const glm::vec4& params = glm::vec4(0.0f)) {
  InstanceGpuData out{};
  const float* ptr = glm::value_ptr(transform);
  std::memcpy(out.col0, ptr, sizeof(out.col0));
  std::memcpy(out.col1, ptr + 4, sizeof(out.col1));
  std::memcpy(out.col2, ptr + 8, sizeof(out.col2));
  std::memcpy(out.col3, ptr + 12, sizeof(out.col3));
  const float* param_ptr = glm::value_ptr(params);
  std::memcpy(out.params, param_ptr, sizeof(out.params));
  return out;
}

struct ShadowBatchKey {
  rendering::MeshId mesh = rendering::kInvalidMesh;
  Diligent::Uint32 index_offset = 0;
  Diligent::Uint32 index_count = 0;
  bool indexed = false;
  bool deformed = false;

  bool operator==(const ShadowBatchKey& other) const {
    return mesh == other.mesh &&
           index_offset == other.index_offset &&
           index_count == other.index_count &&
           indexed == other.indexed &&
           deformed == other.deformed;
  }
};

struct ShadowBatchKeyHash {
  size_t operator()(const ShadowBatchKey& key) const noexcept {
    size_t h = static_cast<size_t>(key.mesh);
    h ^= static_cast<size_t>(key.index_offset) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= static_cast<size_t>(key.index_count) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= static_cast<size_t>(key.indexed ? 1u : 0u) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= static_cast<size_t>(key.deformed ? 1u : 0u) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

struct ShadowBatch {
  ShadowBatchKey key{};
  std::vector<InstanceGpuData> transforms;
  std::vector<glm::vec4> bounds_spheres;
};

struct DeformedShadowDraw {
  ShadowBatchKey key{};
  InstanceGpuData transform{};
  glm::vec4 bounds_sphere{0.0f};
  rendering::DeformationId deformation = rendering::kInvalidDeformation;
};

#if defined(NDEBUG)
constexpr auto kHotPathDrawFlags = Diligent::DRAW_FLAG_NONE;
#else
constexpr auto kHotPathDrawFlags = Diligent::DRAW_FLAG_VERIFY_ALL;
#endif

glm::mat4 buildLightView(const rendering::DirectionalLightData& light) {
  glm::vec3 dir = light.direction;
  if (glm::length(dir) < 1e-4f) {
    dir = glm::vec3(0.3f, -1.0f, 0.2f);
  }
  const glm::vec3 z = glm::normalize(-dir);
  glm::vec3 x;
  const float min_cmp = std::min({std::abs(z.x), std::abs(z.y), std::abs(z.z)});
  if (min_cmp == std::abs(z.x)) {
    x = glm::vec3(1.0f, 0.0f, 0.0f);
  } else if (min_cmp == std::abs(z.y)) {
    x = glm::vec3(0.0f, 1.0f, 0.0f);
  } else {
    x = glm::vec3(0.0f, 0.0f, 1.0f);
  }
  glm::vec3 y = glm::normalize(glm::cross(z, x));
  x = glm::normalize(glm::cross(y, z));

  glm::mat4 view(1.0f);
  view[0][0] = x.x;
  view[1][0] = x.y;
  view[2][0] = x.z;
  view[0][1] = y.x;
  view[1][1] = y.y;
  view[2][1] = y.z;
  view[0][2] = z.x;
  view[1][2] = z.y;
  view[2][2] = z.z;
  return view;
}

bool directionChangedBeyondThreshold(const glm::vec3& a,
                                     const glm::vec3& b,
                                     float max_angle_deg) {
  if (glm::length(a) <= 1e-4f || glm::length(b) <= 1e-4f) {
    return true;
  }
  const glm::vec3 an = glm::normalize(a);
  const glm::vec3 bn = glm::normalize(b);
  const float dot_v = std::clamp(glm::dot(an, bn), -1.0f, 1.0f);
  const float cos_threshold = std::cos(glm::radians(std::max(max_angle_deg, 0.0f)));
  return dot_v < cos_threshold;
}

float maxTransformScale(const glm::mat4& transform) {
  const float sx = glm::length(glm::vec3(transform[0]));
  const float sy = glm::length(glm::vec3(transform[1]));
  const float sz = glm::length(glm::vec3(transform[2]));
  return std::max(sx, std::max(sy, sz));
}

glm::vec4 transformBoundingSphere(const glm::mat4& world,
                                  const glm::vec3& local_center,
                                  float local_radius) {
  if (local_radius <= 0.0f) {
    return glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
  }
  const glm::vec3 center = glm::vec3(world * glm::vec4(local_center, 1.0f));
  const float radius = local_radius * maxTransformScale(world);
  return glm::vec4(center, radius);
}

glm::mat4 planarInstanceTransform(const rendering::PlanarInstanceData& instance) {
  const glm::vec3 position(instance.position_yaw);
  const float yaw = instance.position_yaw.w;
  const glm::vec3 scale(instance.scale_pad);
  glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
  transform = glm::rotate(transform, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
  transform = glm::scale(transform, scale);
  return transform;
}

bool sphereIntersectsClipVolume(const glm::mat4& clip_from_world,
                                const glm::vec4& sphere,
                                bool is_gl_ndc) {
  if (sphere.w <= 0.0f) {
    return true;
  }
  const glm::vec4 clip = clip_from_world * glm::vec4(sphere.x, sphere.y, sphere.z, 1.0f);
  if (std::abs(clip.w) <= 1e-5f) {
    return true;
  }
  const float sx = glm::length(glm::vec3(clip_from_world[0]));
  const float sy = glm::length(glm::vec3(clip_from_world[1]));
  const float sz = glm::length(glm::vec3(clip_from_world[2]));
  const float r = sphere.w * std::max(sx, std::max(sy, sz));

  if (clip.x < -clip.w - r || clip.x > clip.w + r) {
    return false;
  }
  if (clip.y < -clip.w - r || clip.y > clip.w + r) {
    return false;
  }
  if (is_gl_ndc) {
    if (clip.z < -clip.w - r || clip.z > clip.w + r) {
      return false;
    }
  } else {
    if (clip.z < -r || clip.z > clip.w + r) {
      return false;
    }
  }
  return true;
}

const std::array<glm::vec3, 6> kPointShadowFaceDirs{
    glm::vec3(1.0f, 0.0f, 0.0f),
    glm::vec3(-1.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
};

const std::array<glm::vec3, 6> kPointShadowFaceUps{
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
};

}  // namespace

void DiligentBackend::renderShadowLayer(rendering::LayerId layer,
                                        float aspect,
                                        const glm::mat4& depth_fix,
                                        const glm::vec3& camera_position,
                                        const glm::vec3& cam_forward,
                                        const glm::vec3& cam_up,
                                        const glm::vec3& cam_right,
                                        bool is_gl,
                                        float fixed_bias,
                                        float shadow_texel_param,
                                        float point_shadow_texel_size,
                                        const std::vector<size_t>& local_light_source_indices,
                                        ShadowLayerState& out_state) {
  struct PointShadowSelection {
    size_t local_light_index = 0;
    float distance_sq = 0.0f;
  };

  std::vector<PointShadowSelection> point_shadow_candidates;
  point_shadow_candidates.reserve(local_light_source_indices.size());
  for (size_t local_idx = 0; local_idx < local_light_source_indices.size(); ++local_idx) {
    const size_t source_index = local_light_source_indices[local_idx];
    if (source_index >= lights_.size()) {
      continue;
    }
    const rendering::LightData& source_light = lights_[source_index];
    if (source_light.type != rendering::LightType::Point || !source_light.casts_shadows) {
      continue;
    }
    const glm::vec3 to_camera = source_light.position - camera_position;
    point_shadow_candidates.push_back(
        PointShadowSelection{.local_light_index = local_idx,
                             .distance_sq = glm::dot(to_camera, to_camera)});
  }
  std::sort(point_shadow_candidates.begin(),
            point_shadow_candidates.end(),
            [](const PointShadowSelection& a, const PointShadowSelection& b) {
              return a.distance_sq < b.distance_sq;
            });

  const Diligent::Uint32 point_shadow_light_limit = static_cast<Diligent::Uint32>(
      std::clamp(point_shadow_max_lights_, 1, kMaxPointShadowLights));
  for (const auto& candidate : point_shadow_candidates) {
    if (out_state.point_shadow_light_count >= point_shadow_light_limit) {
      break;
    }
    if (candidate.local_light_index >= local_light_source_indices.size()) {
      continue;
    }
    const size_t source_index = local_light_source_indices[candidate.local_light_index];
    if (source_index >= lights_.size()) {
      continue;
    }
    out_state.point_shadow_lights[out_state.point_shadow_light_count] = lights_[source_index];
    out_state.point_shadow_light_source_indices[out_state.point_shadow_light_count] = source_index;
    out_state.point_shadow_local_light_indices[out_state.point_shadow_light_count] =
        candidate.local_light_index;
    out_state.point_shadow_light_count += 1;
  }

  glm::vec3 shadow_light_dir = directional_light_.direction;
  if (glm::length(shadow_light_dir) < 1e-4f) {
    shadow_light_dir = glm::vec3(0.3f, -1.0f, 0.2f);
  }
  shadow_light_dir = glm::normalize(shadow_light_dir);
  if (shadow_light_dir.y > 0.0f) {
    shadow_light_dir = -shadow_light_dir;
  }
  rendering::DirectionalLightData shadow_light = directional_light_;
  shadow_light.direction = shadow_light_dir;
  const glm::mat4 stable_light_view = buildLightView(shadow_light);

  const float shadow_map_extent =
      shadow_map_tex_ ? static_cast<float>(shadow_map_tex_->GetDesc().Width)
                      : static_cast<float>(shadow_map_size_);
  const float safe_shadow_map_extent = std::max(shadow_map_extent, 1.0f);
  const float point_shadow_map_extent =
      point_shadow_map_tex_ ? static_cast<float>(point_shadow_map_tex_->GetDesc().Width)
                            : static_cast<float>(point_shadow_map_size_);
  const float safe_point_shadow_map_extent = std::max(point_shadow_map_extent, 1.0f);
  const auto& ndc = device_->GetDeviceInfo().GetNDCAttribs();
  const glm::mat4 uv_scale = glm::scale(glm::mat4(1.0f),
                                        glm::vec3(0.5f, ndc.YtoVScale, ndc.ZtoDepthScale));
  const glm::mat4 uv_bias = glm::translate(glm::mat4(1.0f),
                                           glm::vec3(0.5f, 0.5f, ndc.GetZtoDepthBias()));

  const float shadow_near = std::max(camera_.near_clip, 0.05f);
  float shadow_far =
      directional_light_.shadow_extent > 0.0f ? directional_light_.shadow_extent : 80.0f;
  shadow_far = std::max(shadow_far, shadow_near + 1.0f);
  if (camera_.perspective) {
    shadow_far = std::min(shadow_far, std::max(camera_.far_clip, shadow_near + 1.0f));
  }
  const float split_lambda = std::clamp(shadow_split_lambda_, 0.0f, 1.0f);

  float prev_split = shadow_near;
  for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
    const float p = static_cast<float>(cascade + 1) / static_cast<float>(kShadowCascadeCount);
    const float uniform_split = shadow_near + (shadow_far - shadow_near) * p;
    float split = uniform_split;
    if (camera_.perspective) {
      const float log_split = shadow_near * std::pow(shadow_far / shadow_near, p);
      split = glm::mix(uniform_split, log_split, split_lambda);
    }
    split = std::clamp(split, shadow_near + 0.001f, shadow_far);
    out_state.cascade_splits[cascade] = split;
    prev_split = split;
  }
  (void)prev_split;

  auto build_slice_corners = [&](float slice_near, float slice_far) {
    std::array<glm::vec3, 8> corners{};
    if (camera_.perspective) {
      const float fov_rad = glm::radians(camera_.fov_y_degrees);
      const float tan_half_fov = std::tan(fov_rad * 0.5f);
      const float near_h = tan_half_fov * slice_near;
      const float near_w = near_h * aspect;
      const float far_h = tan_half_fov * slice_far;
      const float far_w = far_h * aspect;
      const glm::vec3 near_center = camera_position + cam_forward * slice_near;
      const glm::vec3 far_center = camera_position + cam_forward * slice_far;
      corners = {
          near_center + cam_up * near_h - cam_right * near_w,
          near_center + cam_up * near_h + cam_right * near_w,
          near_center - cam_up * near_h - cam_right * near_w,
          near_center - cam_up * near_h + cam_right * near_w,
          far_center + cam_up * far_h - cam_right * far_w,
          far_center + cam_up * far_h + cam_right * far_w,
          far_center - cam_up * far_h - cam_right * far_w,
          far_center - cam_up * far_h + cam_right * far_w,
      };
    } else {
      const glm::vec3 near_center = camera_position + cam_forward * slice_near;
      const glm::vec3 far_center = camera_position + cam_forward * slice_far;
      corners = {
          near_center + cam_up * camera_.ortho_top + cam_right * camera_.ortho_left,
          near_center + cam_up * camera_.ortho_top + cam_right * camera_.ortho_right,
          near_center + cam_up * camera_.ortho_bottom + cam_right * camera_.ortho_left,
          near_center + cam_up * camera_.ortho_bottom + cam_right * camera_.ortho_right,
          far_center + cam_up * camera_.ortho_top + cam_right * camera_.ortho_left,
          far_center + cam_up * camera_.ortho_top + cam_right * camera_.ortho_right,
          far_center + cam_up * camera_.ortho_bottom + cam_right * camera_.ortho_left,
          far_center + cam_up * camera_.ortho_bottom + cam_right * camera_.ortho_right,
      };
    }
    return corners;
  };

  out_state.cascade_light_view_proj = cached_cascade_light_view_proj_;
  out_state.cascade_shadow_uv_proj = cached_cascade_shadow_uv_proj_;
  out_state.cascade_world_texel = cached_cascade_world_texel_;
  out_state.point_shadow_uv_proj = cached_point_shadow_uv_proj_;
  out_state.point_shadow_ready = point_shadow_cache_initialized_;

  float directional_shadow_position_threshold = directional_shadow_position_threshold_;
  if (directional_shadow_cache_valid_ && cached_cascade_world_texel_[0] > 0.0f) {
    directional_shadow_position_threshold = std::max(
        directional_shadow_position_threshold, cached_cascade_world_texel_[0] * 1.5f);
  }

  bool directional_shadow_needs_update = !directional_shadow_cache_valid_;
  if (!directional_shadow_needs_update) {
    const float camera_delta = glm::length(camera_position - cached_shadow_camera_position_);
    directional_shadow_needs_update =
        camera_delta > directional_shadow_position_threshold ||
        std::abs(aspect - cached_shadow_camera_aspect_) > 1e-4f ||
        std::abs(camera_.fov_y_degrees - cached_shadow_camera_fov_y_degrees_) > 1e-3f ||
        std::abs(camera_.near_clip - cached_shadow_camera_near_) > 1e-4f ||
        std::abs(camera_.far_clip - cached_shadow_camera_far_) > 1e-3f ||
        camera_.perspective != cached_shadow_camera_perspective_ ||
        directionChangedBeyondThreshold(cam_forward,
                                        cached_shadow_camera_forward_,
                                        directional_shadow_angle_threshold_deg_) ||
        directionChangedBeyondThreshold(shadow_light_dir,
                                        cached_shadow_light_direction_,
                                        directional_shadow_angle_threshold_deg_);
  }
  if (!directional_shadow_needs_update) {
    for (const auto& entry : instances_) {
      const auto& instance = entry.second;
      if (instance.layer == layer &&
          instance.shadow_visible &&
          instance.deformation != rendering::kInvalidDeformation) {
        directional_shadow_needs_update = true;
        break;
      }
    }
  }
  if (!directional_shadow_needs_update) {
    for (const auto& entry : instanced_records_) {
      const auto& record = entry.second;
      if (record.layer == layer && record.shadow_visible && record.instanceCount() > 0u) {
        directional_shadow_needs_update = true;
        break;
      }
    }
  }
  if (!directional_shadow_needs_update && directional_shadow_cache_valid_) {
    out_state.cascade_splits = cached_cascade_splits_;
  } else {
    float slice_prev_split = shadow_near;
    for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
      const float split_near = slice_prev_split;
      const float split_far = out_state.cascade_splits[cascade];
      slice_prev_split = split_far;
      if (split_far <= split_near + 1e-4f) {
        continue;
      }

      const auto frustum_corners = build_slice_corners(split_near, split_far);
      glm::vec3 frustum_center{0.0f};
      for (const glm::vec3& corner : frustum_corners) {
        frustum_center += corner;
      }
      frustum_center /= static_cast<float>(frustum_corners.size());

      float radius_ws = 0.0f;
      for (const glm::vec3& corner : frustum_corners) {
        radius_ws = std::max(radius_ws, glm::length(corner - frustum_center));
      }
      radius_ws = std::ceil(radius_ws * 16.0f) / 16.0f;
      radius_ws = std::max(radius_ws + 2.0f, 1.0f);

      const glm::mat4 light_view = stable_light_view;

      glm::vec3 center_ls = glm::vec3(light_view * glm::vec4(frustum_center, 1.0f));
      const float units_per_texel = (2.0f * radius_ws) / safe_shadow_map_extent;
      if (units_per_texel > 0.0f) {
        center_ls.x = std::floor(center_ls.x / units_per_texel + 0.5f) * units_per_texel;
        center_ls.y = std::floor(center_ls.y / units_per_texel + 0.5f) * units_per_texel;
      }

      glm::vec3 light_min{
          center_ls.x - radius_ws,
          center_ls.y - radius_ws,
          std::numeric_limits<float>::max()};
      glm::vec3 light_max{
          center_ls.x + radius_ws,
          center_ls.y + radius_ws,
          std::numeric_limits<float>::lowest()};
      for (const glm::vec3& corner : frustum_corners) {
        const glm::vec3 corner_ls = glm::vec3(light_view * glm::vec4(corner, 1.0f));
        light_min.z = std::min(light_min.z, corner_ls.z);
        light_max.z = std::max(light_max.z, corner_ls.z);
      }
      const float depth_span = std::max(light_max.z - light_min.z, 1.0f);
      const float depth_padding = std::max(5.0f, depth_span * 0.2f);
      const float caster_padding_toward_light = std::max(depth_padding, radius_ws * 2.0f);
      light_min.z -= depth_padding;
      light_max.z += caster_padding_toward_light;
      const glm::vec3 extent = light_max - light_min;

      const float scale_x = (extent.x > 0.0f) ? (2.0f / extent.x) : 1.0f;
      const float scale_y = (extent.y > 0.0f) ? (2.0f / extent.y) : 1.0f;
      const float near_z = light_max.z;
      const float far_z = light_min.z;
      const float z_denom = far_z - near_z;
      float scale_z = 1.0f;
      float bias_z = 0.0f;
      if (std::abs(z_denom) > 1e-6f) {
        if (is_gl) {
          scale_z = 2.0f / z_denom;
          bias_z = -(far_z + near_z) / z_denom;
        } else {
          scale_z = 1.0f / z_denom;
          bias_z = -near_z * scale_z;
        }
      } else {
        scale_z = is_gl ? 2.0f : 1.0f;
        bias_z = is_gl ? -1.0f : 0.0f;
      }
      const float bias_x = -light_min.x * scale_x - 1.0f;
      const float bias_y = -light_min.y * scale_y - 1.0f;

      const glm::mat4 scale_mat =
          glm::scale(glm::mat4(1.0f), glm::vec3(scale_x, scale_y, scale_z));
      const glm::mat4 bias_mat =
          glm::translate(glm::mat4(1.0f), glm::vec3(bias_x, bias_y, bias_z));
      const glm::mat4 shadow_proj = bias_mat * scale_mat;
      const glm::mat4 light_view_proj = shadow_proj * light_view;
      const glm::mat4 shadow_uv_proj = uv_bias * uv_scale * light_view_proj;

      out_state.cascade_light_view_proj[cascade] = light_view_proj;
      out_state.cascade_shadow_uv_proj[cascade] = shadow_uv_proj;
      out_state.cascade_world_texel[cascade] =
          std::max(std::max(extent.x, extent.y), 0.0f) / safe_shadow_map_extent;
    }
  }

  auto ensure_instance_buffer = [&](size_t instance_count) {
    if (instance_count == 0) {
      return false;
    }
    if (instance_vb_ && instance_vb_capacity_ >= instance_count) {
      return true;
    }
    const size_t new_capacity =
        std::max(instance_count,
                 instance_vb_capacity_ > 0 ? instance_vb_capacity_ * 2 : static_cast<size_t>(128));
    Diligent::BufferDesc ib_desc{};
    ib_desc.Name = "Karma Instance Buffer";
    ib_desc.Usage = Diligent::USAGE_DYNAMIC;
    ib_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    ib_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    ib_desc.Size = static_cast<Diligent::Uint32>(new_capacity * sizeof(InstanceGpuData));
    instance_vb_.Release();
    device_->CreateBuffer(ib_desc, nullptr, &instance_vb_);
    if (!instance_vb_) {
      return false;
    }
    instance_vb_capacity_ = new_capacity;
    return true;
  };

  auto is_valid_indexed_draw = [&](const MeshRecord& mesh,
                                   Diligent::Uint32 first_index,
                                   Diligent::Uint32 index_count) {
    if (!mesh.index_buffer || mesh.index_count == 0 || index_count == 0) {
      return false;
    }
    const uint64_t first = static_cast<uint64_t>(first_index);
    const uint64_t count = static_cast<uint64_t>(index_count);
    const uint64_t total = static_cast<uint64_t>(mesh.index_count);
    if (first >= total) {
      return false;
    }
    return first + count <= total;
  };

  bool has_shadow_dsv = false;
  for (const auto& dsv : shadow_map_dsv_cascades_) {
    if (dsv) {
      has_shadow_dsv = true;
      break;
    }
  }
  bool has_point_shadow_dsv = false;
  for (const auto& dsv : point_shadow_map_dsv_faces_) {
    if (dsv) {
      has_point_shadow_dsv = true;
      break;
    }
  }
  const bool camera_renders_shadows = camera_.render_shadows;
  const bool directional_light_casts_shadows = directional_light_.casts_shadows;
  const bool can_render_directional_shadows =
      camera_renders_shadows && directional_light_casts_shadows && shadow_pipeline_state_ &&
      has_shadow_dsv;
  const bool can_render_point_shadows =
      camera_renders_shadows && shadow_pipeline_state_ && point_shadow_map_srv_ &&
      has_point_shadow_dsv && out_state.point_shadow_light_count > 0;
  if (!directional_shadow_needs_update && can_render_directional_shadows) {
    for (const auto& entry : instances_) {
      const auto& instance = entry.second;
      if (instance.layer != layer || !instance.shadow_visible || !instance.transform_changed) {
        continue;
      }
      if (meshes_.find(instance.mesh) == meshes_.end()) {
        continue;
      }
      directional_shadow_needs_update = true;
      break;
    }
  }

  auto mark_point_shadow_slot_dirty = [&](Diligent::Uint32 slot, bool invalidate_slot) {
    if (slot >= static_cast<Diligent::Uint32>(kMaxPointShadowLights)) {
      return;
    }
    if (invalidate_slot) {
      point_shadow_slot_valid_[slot] = false;
    }
    const Diligent::Uint32 face_base = slot * static_cast<Diligent::Uint32>(kPointShadowFaceCount);
    for (Diligent::Uint32 face = 0; face < static_cast<Diligent::Uint32>(kPointShadowFaceCount);
         ++face) {
      point_shadow_face_dirty_[face_base + face] = 1u;
    }
  };

  bool point_shadow_force_full_refresh = false;
  bool point_shadow_light_transform_changed = false;
  for (Diligent::Uint32 slot = 0; slot < out_state.point_shadow_light_count; ++slot) {
    if (slot >= static_cast<Diligent::Uint32>(kMaxPointShadowLights)) {
      break;
    }
    const int32_t source_index =
        static_cast<int32_t>(out_state.point_shadow_light_source_indices[slot]);
    const rendering::LightData& light = out_state.point_shadow_lights[slot];
    const bool slot_identity_changed = point_shadow_slot_source_index_[slot] != source_index;
    const bool slot_position_changed =
        glm::length(light.position - point_shadow_slot_position_[slot]) >
        point_shadow_position_threshold_;
    const bool slot_range_changed =
        std::abs(light.range - point_shadow_slot_range_[slot]) > point_shadow_range_threshold_;
    if (!point_shadow_cache_initialized_ || slot_identity_changed) {
      mark_point_shadow_slot_dirty(slot, true);
      point_shadow_force_full_refresh = point_shadow_force_full_refresh || slot_identity_changed;
    } else if (slot_position_changed || slot_range_changed) {
      mark_point_shadow_slot_dirty(slot, false);
      point_shadow_light_transform_changed = true;
    }
    point_shadow_slot_source_index_[slot] = source_index;
    point_shadow_slot_position_[slot] = light.position;
    point_shadow_slot_range_[slot] = light.range;
  }

  bool moving_shadow_caster_affects_point_shadow = false;
  if (can_render_point_shadows) {
    for (const auto& entry : instances_) {
      const auto& instance = entry.second;
      if (instance.layer != layer || !instance.shadow_visible || !instance.transform_changed) {
        continue;
      }
      const auto mesh_it = meshes_.find(instance.mesh);
      if (mesh_it == meshes_.end()) {
        continue;
      }
      const auto& mesh = mesh_it->second;
      if (mesh.bounds_radius <= 0.0f) {
        continue;
      }
      const glm::vec4 bounds_sphere =
          transformBoundingSphere(instance.transform, mesh.bounds_center, mesh.bounds_radius);
      if (bounds_sphere.w <= 0.0f) {
        continue;
      }
      const glm::vec3 caster_center{bounds_sphere.x, bounds_sphere.y, bounds_sphere.z};
      for (Diligent::Uint32 slot = 0; slot < out_state.point_shadow_light_count; ++slot) {
        const rendering::LightData& point_light = out_state.point_shadow_lights[slot];
        const float influence_radius = std::max(point_light.range, 0.0f) + bounds_sphere.w;
        if (influence_radius <= 0.0f) {
          continue;
        }
        const glm::vec3 to_caster = caster_center - point_light.position;
        if (glm::dot(to_caster, to_caster) <= influence_radius * influence_radius) {
          mark_point_shadow_slot_dirty(slot, false);
          moving_shadow_caster_affects_point_shadow = true;
        }
      }
    }
    if (!moving_shadow_caster_affects_point_shadow) {
      for (const auto& entry : instanced_records_) {
        const auto& record = entry.second;
        if (record.layer == layer && record.shadow_visible && record.instanceCount() > 0u) {
          moving_shadow_caster_affects_point_shadow = true;
          break;
        }
      }
    }
  }
  point_shadow_force_full_refresh =
      point_shadow_force_full_refresh || moving_shadow_caster_affects_point_shadow;

  for (Diligent::Uint32 slot = out_state.point_shadow_light_count;
       slot < static_cast<Diligent::Uint32>(kMaxPointShadowLights);
       ++slot) {
    point_shadow_slot_source_index_[slot] = -1;
    point_shadow_slot_valid_[slot] = false;
  }
  if (out_state.point_shadow_light_count == 0) {
    point_shadow_cache_initialized_ = false;
    point_shadow_face_dirty_.fill(1u);
  }

  std::array<uint8_t, kPointShadowMatrixCount> point_shadow_faces_to_update{};
  Diligent::Uint32 point_shadow_face_update_count = 0;
  if (can_render_point_shadows) {
    if (!point_shadow_cache_initialized_ || point_shadow_force_full_refresh) {
      for (Diligent::Uint32 slot = 0; slot < out_state.point_shadow_light_count; ++slot) {
        const Diligent::Uint32 face_base =
            slot * static_cast<Diligent::Uint32>(kPointShadowFaceCount);
        for (Diligent::Uint32 face = 0;
             face < static_cast<Diligent::Uint32>(kPointShadowFaceCount);
             ++face) {
          const Diligent::Uint32 matrix_idx = face_base + face;
          point_shadow_faces_to_update[matrix_idx] = 1u;
          point_shadow_face_update_count += 1;
        }
      }
    } else {
      const Diligent::Uint32 active_face_count =
          out_state.point_shadow_light_count * static_cast<Diligent::Uint32>(kPointShadowFaceCount);
      const Diligent::Uint32 update_budget =
          point_shadow_light_transform_changed ? active_face_count
                                               : std::max(point_shadow_faces_per_frame_budget_, 1u);
      Diligent::Uint32 visited = 0;
      while (point_shadow_face_update_count < update_budget && visited < active_face_count) {
        if (active_face_count == 0) {
          break;
        }
        const Diligent::Uint32 matrix_idx = point_shadow_face_cursor_ % active_face_count;
        point_shadow_face_cursor_ = (point_shadow_face_cursor_ + 1u) % active_face_count;
        visited += 1;
        if (point_shadow_face_dirty_[matrix_idx] == 0u) {
          continue;
        }
        point_shadow_faces_to_update[matrix_idx] = 1u;
        point_shadow_face_update_count += 1;
      }
    }
  }

  const bool render_directional_shadows =
      can_render_directional_shadows && directional_shadow_needs_update;
  const bool render_point_shadows =
      can_render_point_shadows && point_shadow_face_update_count > 0;

  thread_local std::vector<ShadowBatch> shadow_batches;
  thread_local std::vector<DeformedShadowDraw> deformed_shadow_draws;
  thread_local std::unordered_map<ShadowBatchKey, size_t, ShadowBatchKeyHash> shadow_batch_lookup;
  shadow_batches.clear();
  deformed_shadow_draws.clear();
  shadow_batch_lookup.clear();
  if (render_directional_shadows || render_point_shadows) {
    shadow_batches.reserve(instances_.size() + instanced_records_.size());
    deformed_shadow_draws.reserve(instances_.size() / 4 + 1);
    shadow_batch_lookup.reserve(instances_.size() + instanced_records_.size());
    auto append_shadow_batch = [&](const ShadowBatchKey& key,
                                   const glm::mat4& transform,
                                   const glm::vec4& params,
                                   const glm::vec4& bounds_sphere,
                                   rendering::DeformationId deformation) {
      if (key.deformed) {
        deformed_shadow_draws.push_back(DeformedShadowDraw{
            .key = key,
            .transform = packInstanceTransform(transform, params),
            .bounds_sphere = bounds_sphere,
            .deformation = deformation,
        });
        return;
      }
      auto it = shadow_batch_lookup.find(key);
      if (it == shadow_batch_lookup.end()) {
        const size_t idx = shadow_batches.size();
        shadow_batches.push_back(ShadowBatch{.key = key});
        shadow_batch_lookup.emplace(key, idx);
        it = shadow_batch_lookup.find(key);
      }
      shadow_batches[it->second].transforms.push_back(packInstanceTransform(transform, params));
      shadow_batches[it->second].bounds_spheres.push_back(bounds_sphere);
    };

    for (const auto& entry : instances_) {
      const auto& instance = entry.second;
      if (instance.layer != layer || !instance.shadow_visible) {
        continue;
      }
      auto mesh_it = meshes_.find(instance.mesh);
      if (mesh_it == meshes_.end()) {
        continue;
      }
      const auto& mesh = mesh_it->second;
      if (!mesh.vertex_buffer) {
        continue;
      }
      const glm::vec4 world_bounds_sphere =
          transformBoundingSphere(instance.transform, mesh.bounds_center, mesh.bounds_radius);

      const bool indexed_mesh = mesh.index_buffer && mesh.index_count > 0;
      if (!mesh.submeshes.empty()) {
        for (const auto& submesh : mesh.submeshes) {
          const ShadowBatchKey key{
              .mesh = instance.mesh,
              .index_offset = submesh.index_offset,
              .index_count = submesh.index_count,
              .indexed = indexed_mesh && submesh.index_count > 0,
              .deformed = instance.deformation != rendering::kInvalidDeformation,
          };
          append_shadow_batch(key,
                              instance.transform,
                              instance.params,
                              world_bounds_sphere,
                              instance.deformation);
        }
      } else {
        const ShadowBatchKey key{
            .mesh = instance.mesh,
            .index_offset = 0,
            .index_count = mesh.index_count,
            .indexed = indexed_mesh,
            .deformed = instance.deformation != rendering::kInvalidDeformation,
        };
        append_shadow_batch(key,
                            instance.transform,
                            instance.params,
                            world_bounds_sphere,
                            instance.deformation);
      }
    }
    for (const auto& entry : instanced_records_) {
      const auto& record = entry.second;
      if (record.layer != layer || !record.shadow_visible || record.instanceCount() == 0u) {
        continue;
      }
      auto mesh_it = meshes_.find(record.mesh);
      if (mesh_it == meshes_.end()) {
        continue;
      }
      const auto& mesh = mesh_it->second;
      if (!mesh.vertex_buffer) {
        continue;
      }
      const bool indexed_mesh = mesh.index_buffer && mesh.index_count > 0;
      auto append_instanced_shadow = [&](const glm::mat4& transform, const glm::vec4& params) {
        const glm::vec4 world_bounds_sphere =
            transformBoundingSphere(transform, mesh.bounds_center, mesh.bounds_radius);
        if (!mesh.submeshes.empty()) {
          for (const auto& submesh : mesh.submeshes) {
            const ShadowBatchKey key{
                .mesh = record.mesh,
                .index_offset = submesh.index_offset,
                .index_count = submesh.index_count,
                .indexed = indexed_mesh && submesh.index_count > 0,
                .deformed = false,
            };
            append_shadow_batch(key,
                                transform,
                                params,
                                world_bounds_sphere,
                                rendering::kInvalidDeformation);
          }
        } else {
          const ShadowBatchKey key{
              .mesh = record.mesh,
              .index_offset = 0,
              .index_count = mesh.index_count,
              .indexed = indexed_mesh,
              .deformed = false,
          };
          append_shadow_batch(key,
                              transform,
                              params,
                              world_bounds_sphere,
                              rendering::kInvalidDeformation);
        }
      };
      if (record.gpu_layout == rendering::InstanceGpuLayout::PositionYawScaleParams) {
        for (const rendering::PlanarInstanceData& instance : record.planar_instances) {
          append_instanced_shadow(planarInstanceTransform(instance), instance.params);
        }
      } else {
        for (const rendering::InstanceData& instance : record.instances) {
          append_instanced_shadow(instance.transform, instance.params);
        }
      }
    }
    std::sort(shadow_batches.begin(),
              shadow_batches.end(),
              [](const ShadowBatch& a, const ShadowBatch& b) {
                if (a.key.mesh != b.key.mesh) {
                  return a.key.mesh < b.key.mesh;
                }
                if (a.key.index_offset != b.key.index_offset) {
                  return a.key.index_offset < b.key.index_offset;
                }
                if (a.key.index_count != b.key.index_count) {
                  return a.key.index_count < b.key.index_count;
                }
                return static_cast<uint32_t>(a.key.indexed) < static_cast<uint32_t>(b.key.indexed);
              });
  }

  thread_local std::vector<InstanceGpuData> filtered_shadow_transforms;
  filtered_shadow_transforms.clear();
  auto draw_shadow_batches = [&](const DrawConstants& pass_constants, auto&& sphere_visible) {
    Diligent::IBuffer* bound_mesh_vb = nullptr;
    Diligent::IBuffer* bound_instance_vb = nullptr;
    Diligent::IBuffer* bound_index_buffer = nullptr;
    {
      Diligent::MapHelper<DrawConstants> mapped(
          context_, constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
      auto* mapped_constants = getMappedData(mapped);
      if (mapped_constants == nullptr) {
        return;
      }
      *mapped_constants = pass_constants;
    }
    for (const auto& batch : shadow_batches) {
      if (batch.transforms.empty()) {
        continue;
      }
      filtered_shadow_transforms.clear();
      filtered_shadow_transforms.reserve(batch.transforms.size());
      const size_t bounds_count = batch.bounds_spheres.size();
      for (size_t i = 0; i < batch.transforms.size(); ++i) {
        if (i < bounds_count && !sphere_visible(batch.bounds_spheres[i])) {
          continue;
        }
        filtered_shadow_transforms.push_back(batch.transforms[i]);
      }
      if (filtered_shadow_transforms.empty()) {
        continue;
      }
      auto mesh_it = meshes_.find(batch.key.mesh);
      if (mesh_it == meshes_.end()) {
        continue;
      }
      const auto& mesh = mesh_it->second;
      if (!mesh.vertex_buffer) {
        continue;
      }
      if (shadow_srb_) {
        if (!bindDeformationResources(shadow_srb_,
                                      mesh,
                                      rendering::kInvalidDeformation)) {
          continue;
        }
        context_->CommitShaderResources(shadow_srb_,
                                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      } else if (!updateDeformationConstants(mesh, rendering::kInvalidDeformation)) {
        continue;
      }
      if (!ensure_instance_buffer(filtered_shadow_transforms.size())) {
        continue;
      }
      if (!uploadInstanceData(context_,
                              instance_vb_,
                              filtered_shadow_transforms.data(),
                              filtered_shadow_transforms.size())) {
        continue;
      }

      Diligent::IBuffer* mesh_vb = mesh.vertex_buffer.RawPtr();
      Diligent::IBuffer* instance_vb = instance_vb_.RawPtr();
      if (mesh_vb != bound_mesh_vb || instance_vb != bound_instance_vb) {
        Diligent::IBuffer* vbs[] = {mesh.vertex_buffer, instance_vb_};
        Diligent::Uint64 offsets[] = {0, 0};
        context_->SetVertexBuffers(0,
                                   2,
                                   vbs,
                                   offsets,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                   Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        bound_mesh_vb = mesh_vb;
        bound_instance_vb = instance_vb;
      }

      if (batch.key.indexed) {
        if (!is_valid_indexed_draw(mesh, batch.key.index_offset, batch.key.index_count)) {
          continue;
        }
        Diligent::IBuffer* index_buffer = mesh.index_buffer.RawPtr();
        if (index_buffer != bound_index_buffer) {
          context_->SetIndexBuffer(mesh.index_buffer,
                                   0,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
          bound_index_buffer = index_buffer;
        }
        Diligent::DrawIndexedAttribs indexed{};
        indexed.IndexType = Diligent::VT_UINT32;
        indexed.NumIndices = batch.key.index_count;
        indexed.FirstIndexLocation = batch.key.index_offset;
        indexed.NumInstances = static_cast<Diligent::Uint32>(filtered_shadow_transforms.size());
        indexed.Flags = kHotPathDrawFlags;
        context_->DrawIndexed(indexed);
      } else {
        Diligent::DrawAttribs draw_attrs{};
        draw_attrs.NumVertices = mesh.vertex_count;
        draw_attrs.NumInstances = static_cast<Diligent::Uint32>(filtered_shadow_transforms.size());
        draw_attrs.Flags = kHotPathDrawFlags;
        context_->Draw(draw_attrs);
      }
    }

    for (const auto& draw : deformed_shadow_draws) {
      if (!sphere_visible(draw.bounds_sphere)) {
        continue;
      }
      auto mesh_it = meshes_.find(draw.key.mesh);
      if (mesh_it == meshes_.end()) {
        continue;
      }
      const auto& mesh = mesh_it->second;
      if (!mesh.vertex_buffer) {
        continue;
      }
      if (shadow_srb_) {
        if (!bindDeformationResources(shadow_srb_, mesh, draw.deformation)) {
          continue;
        }
        context_->CommitShaderResources(shadow_srb_,
                                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      } else if (!updateDeformationConstants(mesh, draw.deformation)) {
        continue;
      }
      if (!ensure_instance_buffer(1)) {
        continue;
      }
      if (!uploadInstanceData(context_, instance_vb_, &draw.transform, 1)) {
        continue;
      }

      Diligent::IBuffer* mesh_vb = mesh.vertex_buffer.RawPtr();
      Diligent::IBuffer* instance_vb = instance_vb_.RawPtr();
      if (mesh_vb != bound_mesh_vb || instance_vb != bound_instance_vb) {
        Diligent::IBuffer* vbs[] = {mesh.vertex_buffer, instance_vb_};
        Diligent::Uint64 offsets[] = {0, 0};
        context_->SetVertexBuffers(0,
                                   2,
                                   vbs,
                                   offsets,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                   Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        bound_mesh_vb = mesh_vb;
        bound_instance_vb = instance_vb;
      }

      if (draw.key.indexed) {
        if (!is_valid_indexed_draw(mesh, draw.key.index_offset, draw.key.index_count)) {
          continue;
        }
        Diligent::IBuffer* index_buffer = mesh.index_buffer.RawPtr();
        if (index_buffer != bound_index_buffer) {
          context_->SetIndexBuffer(mesh.index_buffer,
                                   0,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
          bound_index_buffer = index_buffer;
        }
        Diligent::DrawIndexedAttribs indexed{};
        indexed.IndexType = Diligent::VT_UINT32;
        indexed.NumIndices = draw.key.index_count;
        indexed.FirstIndexLocation = draw.key.index_offset;
        indexed.NumInstances = 1;
        indexed.Flags = kHotPathDrawFlags;
        context_->DrawIndexed(indexed);
      } else {
        Diligent::DrawAttribs draw_attrs{};
        draw_attrs.NumVertices = mesh.vertex_count;
        draw_attrs.NumInstances = 1;
        draw_attrs.Flags = kHotPathDrawFlags;
        context_->Draw(draw_attrs);
      }
    }
  };

  if (render_directional_shadows) {
    Diligent::Viewport shadow_viewport{};
    shadow_viewport.TopLeftX = 0.0f;
    shadow_viewport.TopLeftY = 0.0f;
    shadow_viewport.Width = static_cast<float>(shadow_map_size_);
    shadow_viewport.Height = static_cast<float>(shadow_map_size_);
    shadow_viewport.MinDepth = 0.0f;
    shadow_viewport.MaxDepth = 1.0f;
    context_->SetViewports(1,
                           &shadow_viewport,
                           static_cast<Diligent::Uint32>(shadow_map_size_),
                           static_cast<Diligent::Uint32>(shadow_map_size_));
    context_->SetPipelineState(shadow_pipeline_state_);
    if (shadow_srb_) {
      context_->CommitShaderResources(shadow_srb_,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    for (int cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
      Diligent::ITextureView* cascade_dsv =
          shadow_map_dsv_cascades_[cascade] ? shadow_map_dsv_cascades_[cascade].RawPtr()
                                            : shadow_map_dsv_.RawPtr();
      if (!cascade_dsv) {
        continue;
      }

      context_->SetRenderTargets(0,
                                 nullptr,
                                 cascade_dsv,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      context_->ClearDepthStencil(cascade_dsv,
                                  Diligent::CLEAR_DEPTH_FLAG,
                                  1.0f,
                                  0,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

      DrawConstants shadow_constants{};
      copyMat4(shadow_constants.mvp, out_state.cascade_light_view_proj[cascade]);
      copyMat4(shadow_constants.light_view_proj, out_state.cascade_light_view_proj[cascade]);
      copyMat4(shadow_constants.shadow_uv_proj, out_state.cascade_shadow_uv_proj[cascade]);
      for (int idx = 0; idx < kShadowCascadeCount; ++idx) {
        copyMat4(shadow_constants.shadow_cascade_uv_proj[idx],
                 out_state.cascade_shadow_uv_proj[idx]);
        shadow_constants.shadow_cascade_splits[idx] = out_state.cascade_splits[idx];
        shadow_constants.shadow_cascade_world_texel[idx] = out_state.cascade_world_texel[idx];
      }
      shadow_constants.shadow_cascade_params[0] = 0.08f;
      shadow_constants.shadow_cascade_params[1] = 0.0f;
      shadow_constants.shadow_cascade_params[2] = 0.0f;
      shadow_constants.shadow_cascade_params[3] = 0.0f;
      shadow_constants.shadow_params[0] = 0.0f;
      shadow_constants.shadow_params[1] = fixed_bias;
      shadow_constants.shadow_params[2] = static_cast<float>(shadow_pcf_radius_);
      shadow_constants.shadow_params[3] = shadow_texel_param;
      shadow_constants.point_shadow_params[0] = 0.0f;
      shadow_constants.point_shadow_params[1] = point_shadow_texel_size;
      shadow_constants.point_shadow_params[2] = static_cast<float>(point_shadow_map_size_);
      shadow_constants.point_shadow_params[3] = 0.0f;
      shadow_constants.local_light_params[0] = local_light_distance_damping_;
      shadow_constants.local_light_params[1] = local_light_range_exponent_;
      shadow_constants.local_light_params[2] = ao_affects_local_lights_ ? 1.0f : 0.0f;
      shadow_constants.local_light_params[3] = local_light_directional_shadow_lift_;
      shadow_constants.point_shadow_tuning[0] = point_shadow_constant_bias_;
      shadow_constants.point_shadow_tuning[1] = point_shadow_slope_bias_scale_;
      shadow_constants.point_shadow_tuning[2] = point_shadow_normal_bias_scale_;
      shadow_constants.point_shadow_tuning[3] = point_shadow_receiver_bias_scale_;
      shadow_constants.shadow_bias_params[0] = shadow_receiver_bias_scale_;
      shadow_constants.shadow_bias_params[1] = shadow_normal_bias_scale_;
      shadow_constants.shadow_bias_params[2] = out_state.cascade_world_texel[cascade];
      shadow_constants.shadow_bias_params[3] = 0.0f;
      shadow_constants.forward_plus_params[0] = 0.0f;
      shadow_constants.forward_plus_params[1] = 0.0f;
      shadow_constants.forward_plus_params[2] = 0.0f;
      shadow_constants.forward_plus_params[3] = 0.0f;
      shadow_constants.camera_forward[0] = cam_forward.x;
      shadow_constants.camera_forward[1] = cam_forward.y;
      shadow_constants.camera_forward[2] = cam_forward.z;
      shadow_constants.camera_forward[3] = 0.0f;
      const glm::mat4 cascade_cull_matrix = out_state.cascade_light_view_proj[cascade];
      draw_shadow_batches(shadow_constants,
                          [&](const glm::vec4& sphere) {
                            return sphereIntersectsClipVolume(cascade_cull_matrix, sphere, is_gl);
                          });
    }

    if (shadow_map_tex_) {
      Diligent::StateTransitionDesc barrier{};
      barrier.pResource = shadow_map_tex_;
      barrier.OldState = Diligent::RESOURCE_STATE_DEPTH_WRITE;
      barrier.NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
      barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
      context_->TransitionResourceStates(1, &barrier);
    }

    cached_cascade_light_view_proj_ = out_state.cascade_light_view_proj;
    cached_cascade_shadow_uv_proj_ = out_state.cascade_shadow_uv_proj;
    cached_cascade_world_texel_ = out_state.cascade_world_texel;
    cached_cascade_splits_ = out_state.cascade_splits;
    cached_shadow_camera_position_ = camera_position;
    cached_shadow_camera_forward_ = cam_forward;
    cached_shadow_light_direction_ = shadow_light_dir;
    cached_shadow_camera_aspect_ = aspect;
    cached_shadow_camera_fov_y_degrees_ = camera_.fov_y_degrees;
    cached_shadow_camera_near_ = camera_.near_clip;
    cached_shadow_camera_far_ = camera_.far_clip;
    cached_shadow_camera_perspective_ = camera_.perspective;
    directional_shadow_cache_valid_ = true;
  }

  if (render_point_shadows) {
    Diligent::Viewport shadow_viewport{};
    shadow_viewport.TopLeftX = 0.0f;
    shadow_viewport.TopLeftY = 0.0f;
    shadow_viewport.Width = static_cast<float>(point_shadow_map_size_);
    shadow_viewport.Height = static_cast<float>(point_shadow_map_size_);
    shadow_viewport.MinDepth = 0.0f;
    shadow_viewport.MaxDepth = 1.0f;
    context_->SetViewports(1,
                           &shadow_viewport,
                           static_cast<Diligent::Uint32>(point_shadow_map_size_),
                           static_cast<Diligent::Uint32>(point_shadow_map_size_));
    context_->SetPipelineState(shadow_pipeline_state_);
    if (shadow_srb_) {
      context_->CommitShaderResources(shadow_srb_,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    const Diligent::Uint32 active_face_count =
        out_state.point_shadow_light_count * static_cast<Diligent::Uint32>(kPointShadowFaceCount);
    for (Diligent::Uint32 matrix_idx = 0; matrix_idx < active_face_count; ++matrix_idx) {
      if (point_shadow_faces_to_update[matrix_idx] == 0u) {
        continue;
      }
      const Diligent::Uint32 point_idx =
          matrix_idx / static_cast<Diligent::Uint32>(kPointShadowFaceCount);
      const int face =
          static_cast<int>(matrix_idx % static_cast<Diligent::Uint32>(kPointShadowFaceCount));
      if (point_idx >= out_state.point_shadow_light_count) {
        continue;
      }

      const rendering::LightData& point_light = out_state.point_shadow_lights[point_idx];
      const float range_ws = std::max(point_light.range, 0.1f);
      const float near_plane = std::max(range_ws * 0.02f, 0.05f);
      const float far_plane = std::max(range_ws, near_plane + 0.1f);
      const glm::mat4 face_proj =
          glm::perspective(glm::radians(92.0f), 1.0f, near_plane, far_plane);
      const glm::mat4 face_view = glm::lookAt(point_light.position,
                                              point_light.position + kPointShadowFaceDirs[face],
                                              kPointShadowFaceUps[face]);
      const glm::mat4 face_light_view_proj = depth_fix * face_proj * face_view;
      out_state.point_shadow_uv_proj[matrix_idx] = uv_bias * uv_scale * face_light_view_proj;
      cached_point_shadow_uv_proj_[matrix_idx] = out_state.point_shadow_uv_proj[matrix_idx];

      Diligent::ITextureView* face_dsv =
          point_shadow_map_dsv_faces_[matrix_idx] ? point_shadow_map_dsv_faces_[matrix_idx].RawPtr()
                                                  : nullptr;
      if (!face_dsv) {
        continue;
      }
      context_->SetRenderTargets(0,
                                 nullptr,
                                 face_dsv,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      context_->ClearDepthStencil(face_dsv,
                                  Diligent::CLEAR_DEPTH_FLAG,
                                  1.0f,
                                  0,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

      DrawConstants shadow_constants{};
      copyMat4(shadow_constants.mvp, face_light_view_proj);
      copyMat4(shadow_constants.light_view_proj, face_light_view_proj);
      copyMat4(shadow_constants.shadow_uv_proj, out_state.point_shadow_uv_proj[matrix_idx]);
      for (int idx = 0; idx < kShadowCascadeCount; ++idx) {
        copyMat4(shadow_constants.shadow_cascade_uv_proj[idx],
                 out_state.cascade_shadow_uv_proj[idx]);
        shadow_constants.shadow_cascade_splits[idx] = out_state.cascade_splits[idx];
        shadow_constants.shadow_cascade_world_texel[idx] = out_state.cascade_world_texel[idx];
      }
      shadow_constants.shadow_cascade_params[0] = 0.08f;
      shadow_constants.shadow_cascade_params[1] = 0.0f;
      shadow_constants.shadow_cascade_params[2] = 0.0f;
      shadow_constants.shadow_cascade_params[3] = 0.0f;
      shadow_constants.shadow_params[0] = 0.0f;
      shadow_constants.shadow_params[1] = fixed_bias;
      shadow_constants.shadow_params[2] = static_cast<float>(shadow_pcf_radius_);
      shadow_constants.shadow_params[3] = point_shadow_texel_size;
      shadow_constants.point_shadow_params[0] = 0.0f;
      shadow_constants.point_shadow_params[1] = point_shadow_texel_size;
      shadow_constants.point_shadow_params[2] = static_cast<float>(point_shadow_map_size_);
      shadow_constants.point_shadow_params[3] = static_cast<float>(out_state.point_shadow_light_count);
      shadow_constants.local_light_params[0] = local_light_distance_damping_;
      shadow_constants.local_light_params[1] = local_light_range_exponent_;
      shadow_constants.local_light_params[2] = ao_affects_local_lights_ ? 1.0f : 0.0f;
      shadow_constants.local_light_params[3] = local_light_directional_shadow_lift_;
      shadow_constants.point_shadow_tuning[0] = point_shadow_constant_bias_;
      shadow_constants.point_shadow_tuning[1] = point_shadow_slope_bias_scale_;
      shadow_constants.point_shadow_tuning[2] = point_shadow_normal_bias_scale_;
      shadow_constants.point_shadow_tuning[3] = point_shadow_receiver_bias_scale_;
      shadow_constants.shadow_bias_params[0] = shadow_receiver_bias_scale_;
      shadow_constants.shadow_bias_params[1] = shadow_normal_bias_scale_;
      shadow_constants.shadow_bias_params[2] = range_ws / safe_point_shadow_map_extent;
      shadow_constants.shadow_bias_params[3] = 0.0f;
      shadow_constants.forward_plus_params[0] = 0.0f;
      shadow_constants.forward_plus_params[1] = 0.0f;
      shadow_constants.forward_plus_params[2] = 0.0f;
      shadow_constants.forward_plus_params[3] = 0.0f;
      shadow_constants.camera_forward[0] = cam_forward.x;
      shadow_constants.camera_forward[1] = cam_forward.y;
      shadow_constants.camera_forward[2] = cam_forward.z;
      shadow_constants.camera_forward[3] = 0.0f;
      const glm::mat4 point_cull_matrix = face_light_view_proj;
      draw_shadow_batches(shadow_constants,
                          [&](const glm::vec4& sphere) {
                            if (sphere.w > 0.0f) {
                              const glm::vec3 delta = glm::vec3(sphere) - point_light.position;
                              const float max_dist = range_ws + sphere.w;
                              if (glm::dot(delta, delta) > max_dist * max_dist) {
                                return false;
                              }
                            }
                            return sphereIntersectsClipVolume(point_cull_matrix, sphere, is_gl);
                          });
      point_shadow_face_dirty_[matrix_idx] = 0u;
    }

    if (point_shadow_map_tex_) {
      Diligent::StateTransitionDesc barrier{};
      barrier.pResource = point_shadow_map_tex_;
      barrier.OldState = Diligent::RESOURCE_STATE_DEPTH_WRITE;
      barrier.NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
      barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
      context_->TransitionResourceStates(1, &barrier);
      out_state.point_shadow_ready = true;
    }
  }

  bool any_point_shadow_slot_valid = false;
  for (Diligent::Uint32 slot = 0; slot < out_state.point_shadow_light_count; ++slot) {
    bool slot_fully_updated = true;
    const Diligent::Uint32 face_base = slot * static_cast<Diligent::Uint32>(kPointShadowFaceCount);
    for (Diligent::Uint32 face = 0;
         face < static_cast<Diligent::Uint32>(kPointShadowFaceCount);
         ++face) {
      if (point_shadow_face_dirty_[face_base + face] != 0u) {
        slot_fully_updated = false;
        break;
      }
    }
    if (slot_fully_updated) {
      point_shadow_slot_valid_[slot] = true;
    }
    any_point_shadow_slot_valid = any_point_shadow_slot_valid || point_shadow_slot_valid_[slot];
  }
  point_shadow_cache_initialized_ = point_shadow_cache_initialized_ || any_point_shadow_slot_valid;
  out_state.point_shadow_ready = out_state.point_shadow_ready && any_point_shadow_slot_valid;
}

}  // namespace karma::rendering::backend
