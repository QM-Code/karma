#include "extractors.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace karma::rendering::render_system {

namespace {

bool finiteVec3(const glm::vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finiteQuat(const glm::quat& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && std::isfinite(value.w);
}

float finiteOr(float value, float fallback) {
  return std::isfinite(value) ? value : fallback;
}

math::Color finiteColor(math::Color value, const math::Color& fallback) {
  value.r = finiteOr(value.r, fallback.r);
  value.g = finiteOr(value.g, fallback.g);
  value.b = finiteOr(value.b, fallback.b);
  value.a = finiteOr(value.a, fallback.a);
  return value;
}

glm::vec4 instanceParams(const std::array<float, 4>& params) {
  return {params[0], params[1], params[2], params[3]};
}

glm::mat4 meshInstanceTransform(const components::MeshInstance& instance) {
  glm::mat4 transform = glm::translate(glm::mat4(1.0f), math::toGlm(instance.position));
  transform *= glm::mat4_cast(math::toGlm(instance.rotation));
  return glm::scale(transform, math::toGlm(instance.scale));
}

glm::mat4 planarInstanceTransform(const components::PlanarMeshInstance& instance) {
  glm::mat4 transform = glm::translate(glm::mat4(1.0f), math::toGlm(instance.position));
  transform = glm::rotate(transform,
                          instance.yaw_radians,
                          glm::vec3(0.0f, 1.0f, 0.0f));
  return glm::scale(transform, math::toGlm(instance.scale));
}

bool finiteMatrix(const glm::mat4& matrix) {
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      if (!std::isfinite(matrix[column][row])) return false;
    }
  }
  return true;
}

bool extractPlanarTransform(const glm::mat4& transform,
                            PlanarInstanceData& out) {
  if (!finiteMatrix(transform)) return false;

  const glm::vec3 column_x(transform[0]);
  const glm::vec3 column_y(transform[1]);
  const glm::vec3 column_z(transform[2]);
  const float scale_hint = std::max(
      {1.0f, glm::length(column_x), glm::length(column_y), glm::length(column_z)});
  const float epsilon = 1.0e-4f * scale_hint;
  if (std::abs(transform[0][3]) > epsilon ||
      std::abs(transform[1][3]) > epsilon ||
      std::abs(transform[2][3]) > epsilon ||
      std::abs(transform[3][3] - 1.0f) > epsilon ||
      std::abs(column_x.y) > epsilon ||
      std::abs(column_y.x) > epsilon ||
      std::abs(column_y.z) > epsilon ||
      std::abs(column_z.y) > epsilon) {
    return false;
  }

  float yaw = 0.0f;
  float scale_x = glm::length(column_x);
  float scale_z = 0.0f;
  if (scale_x > epsilon) {
    const glm::vec3 x_axis = column_x / scale_x;
    yaw = std::atan2(-x_axis.z, x_axis.x);
    const glm::vec3 z_axis(std::sin(yaw), 0.0f, std::cos(yaw));
    scale_z = glm::dot(column_z, z_axis);
    if (glm::length(column_z - z_axis * scale_z) > epsilon) return false;
  } else {
    scale_x = 0.0f;
    const float z_length = glm::length(column_z);
    if (z_length > epsilon) {
      const glm::vec3 z_axis = column_z / z_length;
      yaw = std::atan2(z_axis.x, z_axis.z);
      scale_z = z_length;
    }
  }

  out.position_yaw = glm::vec4(glm::vec3(transform[3]), yaw);
  out.scale_pad = glm::vec4(scale_x, column_y.y, scale_z, 0.0f);
  return true;
}

float conservativeTransformScale(const glm::mat4& transform) {
  const glm::vec3 column_x(transform[0]);
  const glm::vec3 column_y(transform[1]);
  const glm::vec3 column_z(transform[2]);
  const float x2 = glm::dot(column_x, column_x);
  const float y2 = glm::dot(column_y, column_y);
  const float z2 = glm::dot(column_z, column_z);
  const float largest = std::sqrt(std::max({x2, y2, z2, 0.0f}));
  const float orthogonal_epsilon =
      1.0e-5f * std::max({1.0f, std::sqrt(x2 * y2), std::sqrt(x2 * z2),
                          std::sqrt(y2 * z2)});
  const bool orthogonal =
      std::abs(glm::dot(column_x, column_y)) <= orthogonal_epsilon &&
      std::abs(glm::dot(column_x, column_z)) <= orthogonal_epsilon &&
      std::abs(glm::dot(column_y, column_z)) <= orthogonal_epsilon;
  return orthogonal ? largest : std::sqrt(std::max(x2 + y2 + z2, 0.0f));
}

void mergeSphere(glm::vec3& center,
                 float& radius,
                 bool& valid,
                 const glm::vec3& next_center,
                 float next_radius) {
  if (next_radius <= 0.0f || !std::isfinite(next_radius) ||
      !finiteVec3(next_center)) {
    return;
  }
  if (!valid) {
    center = next_center;
    radius = next_radius;
    valid = true;
    return;
  }
  const glm::vec3 delta = next_center - center;
  const float distance = glm::length(delta);
  if (!std::isfinite(distance)) return;
  if (distance + next_radius <= radius) return;
  if (distance + radius <= next_radius) {
    center = next_center;
    radius = next_radius;
    return;
  }
  if (distance <= 1.0e-5f) {
    radius = std::max(radius, next_radius);
    return;
  }
  const float new_radius = (radius + distance + next_radius) * 0.5f;
  center += delta * ((new_radius - radius) / distance);
  radius = new_radius;
}

void mergeTransformedBounds(InstancedBounds& out,
                            const glm::mat4& transform,
                            const glm::vec3& mesh_bounds_center,
                            float mesh_bounds_radius,
                            bool mesh_bounds_valid) {
  if (!mesh_bounds_valid) return;
  const glm::vec3 center =
      glm::vec3(transform * glm::vec4(mesh_bounds_center, 1.0f));
  mergeSphere(out.center,
              out.radius,
              out.valid,
              center,
              mesh_bounds_radius * conservativeTransformScale(transform));
}

}  // namespace

glm::vec3 toGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

glm::quat toGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

glm::vec3 transformPoint(const components::TransformComponent& transform,
                         const math::Vec3& local,
                         float interpolation_alpha) {
  const glm::vec3 pos = ::karma::rendering::render_system::toGlm(transform.getInterpolatedPosition(interpolation_alpha));
  const glm::quat rot = ::karma::rendering::render_system::toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  const glm::vec3 scale = ::karma::rendering::render_system::toGlm(transform.getScale());
  const glm::vec3 scaled{local.x * scale.x, local.y * scale.y, local.z * scale.z};
  return pos + glm::mat3_cast(rot) * scaled;
}

glm::mat4 toTransform(const components::TransformComponent& transform,
                      float interpolation_alpha) {
  const glm::vec3 pos = ::karma::rendering::render_system::toGlm(transform.getInterpolatedPosition(interpolation_alpha));
  const glm::quat rot = ::karma::rendering::render_system::toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  const glm::vec3 scale = ::karma::rendering::render_system::toGlm(transform.getScale());
  glm::mat4 matrix(1.0f);
  matrix = glm::translate(matrix, pos);
  matrix *= glm::mat4_cast(rot);
  matrix = glm::scale(matrix, scale);
  return matrix;
}

ExtractedInstanceSet extractInstanceSet(
    const components::InstanceSetComponent& component,
    const glm::mat4& owner_world_transform) {
  ExtractedInstanceSet out{};
  if (component.gpu_layout == InstanceGpuLayout::PositionYawScaleParams) {
    std::vector<glm::mat4> composed_transforms;
    composed_transforms.reserve(component.planar_instances.size());
    out.planar_instances.reserve(component.planar_instances.size());
    bool compact = true;
    for (const components::PlanarMeshInstance& instance : component.planar_instances) {
      const glm::mat4 composed =
          owner_world_transform * planarInstanceTransform(instance);
      composed_transforms.push_back(composed);
      if (compact) {
        PlanarInstanceData extracted{};
        compact = extractPlanarTransform(composed, extracted);
        if (compact) {
          extracted.params = instanceParams(instance.params);
          out.planar_instances.push_back(extracted);
        } else {
          out.planar_instances.clear();
        }
      }
    }

    if (compact) {
      out.gpu_layout = InstanceGpuLayout::PositionYawScaleParams;
      return out;
    }

    out.gpu_layout = InstanceGpuLayout::Matrix4x4Params;
    out.instances.reserve(component.planar_instances.size());
    for (std::size_t index = 0; index < component.planar_instances.size(); ++index) {
      out.instances.push_back(InstanceData{
          .transform = composed_transforms[index],
          .params = instanceParams(component.planar_instances[index].params),
      });
    }
    return out;
  }

  out.gpu_layout = InstanceGpuLayout::Matrix4x4Params;
  out.instances.reserve(component.instances.size());
  for (const components::MeshInstance& instance : component.instances) {
    InstanceData extracted{
        .transform = owner_world_transform * meshInstanceTransform(instance),
        .params = instanceParams(instance.params),
    };
    out.instances.push_back(extracted);
  }
  return out;
}

InstancedBounds calculateInstancedBounds(
    const ExtractedInstanceSet& instance_set,
    const glm::mat4& batch_local_transform,
    const glm::vec3& mesh_bounds_center,
    float mesh_bounds_radius,
    bool mesh_bounds_valid) {
  InstancedBounds out{};
  if (instance_set.gpu_layout == InstanceGpuLayout::PositionYawScaleParams) {
    for (const PlanarInstanceData& instance : instance_set.planar_instances) {
      mergeTransformedBounds(out,
                             planarInstanceTransform(components::PlanarMeshInstance{
                                 .position = {instance.position_yaw.x,
                                              instance.position_yaw.y,
                                              instance.position_yaw.z},
                                 .yaw_radians = instance.position_yaw.w,
                                 .scale = {instance.scale_pad.x,
                                           instance.scale_pad.y,
                                           instance.scale_pad.z},
                             }) * batch_local_transform,
                             mesh_bounds_center,
                             mesh_bounds_radius,
                             mesh_bounds_valid);
    }
    return out;
  }
  for (const InstanceData& instance : instance_set.instances) {
    mergeTransformedBounds(out,
                           instance.transform * batch_local_transform,
                           mesh_bounds_center,
                           mesh_bounds_radius,
                           mesh_bounds_valid);
  }
  return out;
}

CameraData toCameraData(const components::CameraComponent& camera,
                        const components::TransformComponent& transform,
                        float interpolation_alpha) {
  CameraData out{};
  out.position = ::karma::rendering::render_system::toGlm(transform.getInterpolatedPosition(interpolation_alpha));
  out.rotation = ::karma::rendering::render_system::toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  if (!finiteVec3(out.position)) {
    out.position = glm::vec3(0.0f);
  }
  const float rotation_length_squared = glm::dot(out.rotation, out.rotation);
  if (!finiteQuat(out.rotation) || !std::isfinite(rotation_length_squared) ||
      rotation_length_squared <= 1.0e-12f) {
    out.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  } else {
    out.rotation = glm::normalize(out.rotation);
  }
  out.perspective = camera.perspective;
  out.render_shadows = camera.render_shadows;
  out.fov_y_degrees = std::clamp(finiteOr(camera.fov_y_degrees, 60.0f), 1.0f, 179.0f);
  out.aspect = 16.0f / 9.0f;
  out.near_clip = std::max(finiteOr(camera.near_clip, 0.1f), 1.0e-4f);
  out.far_clip = finiteOr(camera.far_clip, 1000.0f);
  if (out.far_clip <= out.near_clip) {
    out.far_clip = out.near_clip + 1.0f;
  }
  out.ortho_left = finiteOr(camera.ortho_left, -1.0f);
  out.ortho_right = finiteOr(camera.ortho_right, 1.0f);
  out.ortho_top = finiteOr(camera.ortho_top, 1.0f);
  out.ortho_bottom = finiteOr(camera.ortho_bottom, -1.0f);
  if (std::abs(out.ortho_right - out.ortho_left) <= 1.0e-5f) {
    out.ortho_left = -1.0f;
    out.ortho_right = 1.0f;
  }
  if (std::abs(out.ortho_top - out.ortho_bottom) <= 1.0e-5f) {
    out.ortho_top = 1.0f;
    out.ortho_bottom = -1.0f;
  }
  out.shader_override_vertex_path = camera.shader_override_vertex_path;
  out.shader_override_fragment_path = camera.shader_override_fragment_path;
  out.anti_aliasing = rendering::clampAntiAliasingSettings(camera.anti_aliasing);
  std::array<std::pair<std::string_view, const math::Color*>,
             kCameraShaderUserParamCapacity>
      sorted_params{};
  std::size_t sorted_param_count = 0u;
  for (const auto& [key, value] : camera.shader_user_params) {
    const auto insert_at = std::lower_bound(
        sorted_params.begin(),
        sorted_params.begin() + static_cast<std::ptrdiff_t>(sorted_param_count),
        key,
        [](const auto& entry, std::string_view candidate) {
          return entry.first < candidate;
        });
    const std::size_t index = static_cast<std::size_t>(insert_at - sorted_params.begin());
    if (index >= kCameraShaderUserParamCapacity) {
      continue;
    }
    const std::size_t move_end = std::min(
        sorted_param_count,
        static_cast<std::size_t>(kCameraShaderUserParamCapacity) - 1u);
    std::move_backward(sorted_params.begin() + static_cast<std::ptrdiff_t>(index),
                       sorted_params.begin() + static_cast<std::ptrdiff_t>(move_end),
                       sorted_params.begin() + static_cast<std::ptrdiff_t>(move_end + 1u));
    sorted_params[index] = {key, &value};
    sorted_param_count = std::min(sorted_param_count + 1u,
                                  static_cast<std::size_t>(kCameraShaderUserParamCapacity));
  }
  out.shader_user_param_count = 0u;
  for (std::size_t index = 0u; index < sorted_param_count; ++index) {
    const auto& [key, value] = sorted_params[index];
    auto& dst = out.shader_user_params[out.shader_user_param_count++];
    dst.key_hash = cameraShaderParamKeyHash(key);
    dst.value = finiteColor(*value, math::Color{});
  }
  return out;
}

DirectionalLightData toDirectionalLight(const components::LightComponent& light,
                                        const components::TransformComponent& transform,
                                        float interpolation_alpha) {
  DirectionalLightData out{};
  out.color = finiteColor(light.color, math::Color{});
  out.intensity = std::max(finiteOr(light.intensity, 0.0f), 0.0f);
  const glm::quat rot = ::karma::rendering::render_system::toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  const glm::mat3 basis = glm::mat3_cast(rot);
  out.direction = basis * glm::vec3(0.0f, 0.0f, -1.0f);
  if (!finiteVec3(out.direction) || glm::dot(out.direction, out.direction) <= 1.0e-12f) {
    out.direction = glm::vec3(0.0f, -1.0f, 0.0f);
  } else {
    out.direction = glm::normalize(out.direction);
  }
  out.position = ::karma::rendering::render_system::toGlm(transform.getInterpolatedPosition(interpolation_alpha));
  if (!finiteVec3(out.position)) {
    out.position = glm::vec3(0.0f);
  }
  out.shadow_extent = std::max(finiteOr(light.shadow_extent, 0.0f), 0.0f);
  out.casts_shadows = light.casts_shadows;
  out.mixed_bake_mask_bit = light.mixed_bake_mask_bit;
  return out;
}

LightData toLightData(const components::LightComponent& light,
                      const components::TransformComponent& transform,
                      float interpolation_alpha) {
  LightData out{};
  const glm::quat rot = ::karma::rendering::render_system::toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  const glm::mat3 basis = glm::mat3_cast(rot);
  glm::vec3 dir = basis * glm::vec3(0.0f, 0.0f, -1.0f);
  if (glm::length(dir) < 1e-5f) {
    dir = glm::vec3(0.0f, -1.0f, 0.0f);
  } else {
    dir = glm::normalize(dir);
  }
  out.position = ::karma::rendering::render_system::toGlm(transform.getInterpolatedPosition(interpolation_alpha));
  if (!finiteVec3(out.position)) {
    out.position = glm::vec3(0.0f);
  }
  out.direction = dir;
  out.color = finiteColor(light.color, math::Color{});
  out.intensity = std::max(finiteOr(light.intensity, 0.0f), 0.0f);
  out.range = std::max(finiteOr(light.range, 0.0f), 0.0f);
  out.casts_shadows = light.casts_shadows;
  out.mixed_bake_mask_bit = light.mixed_bake_mask_bit;

  const float inner_rad = glm::radians(
      std::clamp(finiteOr(light.inner_cone_degrees, 15.0f), 0.0f, 179.0f));
  const float outer_rad = glm::radians(
      std::clamp(finiteOr(light.outer_cone_degrees, 30.0f), 0.0f, 179.0f));
  float inner_cos = std::cos(inner_rad);
  float outer_cos = std::cos(outer_rad);
  if (inner_cos < outer_cos) {
    std::swap(inner_cos, outer_cos);
  }
  out.inner_cone_cos = inner_cos;
  out.outer_cone_cos = outer_cos;

  switch (light.type) {
    case components::LightComponent::Type::Directional:
      out.type = LightType::Directional;
      out.range = 0.0f;
      break;
    case components::LightComponent::Type::Point:
      out.type = LightType::Point;
      break;
    case components::LightComponent::Type::Spot:
      out.type = LightType::Spot;
      break;
  }
  return out;
}

}  // namespace karma::rendering::render_system
