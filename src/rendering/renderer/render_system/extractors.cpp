#include "extractors.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace karma::renderer::render_system {

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

glm::mat4 toTransform(const components::TransformComponent& transform,
                      float interpolation_alpha) {
  const glm::vec3 pos = toGlm(transform.getInterpolatedPosition(interpolation_alpha));
  const glm::quat rot = toGlm(transform.getInterpolatedRotation(interpolation_alpha));
  const glm::vec3 scale = toGlm(transform.getScale());
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
    if (out.shader_user_param_count >= kCameraShaderUserParamCapacity) {
      break;
    }
    auto& dst = out.shader_user_params[out.shader_user_param_count++];
    dst.key_hash = cameraShaderParamKeyHash(key);
    dst.value = value;
  }
  return out;
}

DirectionalLightData toDirectionalLight(const components::LightComponent& light,
                                        const components::TransformComponent& transform,
                                        float interpolation_alpha) {
  DirectionalLightData out{};
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

LightData toLightData(const components::LightComponent& light,
                      const components::TransformComponent& transform,
                      float interpolation_alpha) {
  LightData out{};
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

}  // namespace karma::renderer::render_system
