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
