#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/rendering/renderer/camera.h"
#include "karma/rendering/renderer/lights.h"
#include "karma/world/components/camera.h"
#include "karma/world/components/light.h"
#include "karma/world/components/transform.h"

namespace karma::renderer::render_system {

glm::vec3 toGlm(const math::Vec3& v);
glm::quat toGlm(const math::Quat& q);
glm::vec3 transformPoint(const components::TransformComponent& transform,
                         const math::Vec3& local,
                         float interpolation_alpha);
glm::mat4 toTransform(const components::TransformComponent& transform,
                      float interpolation_alpha);
CameraData toCameraData(const components::CameraComponent& camera,
                        const components::TransformComponent& transform,
                        float interpolation_alpha);
DirectionalLightData toDirectionalLight(const components::LightComponent& light,
                                        const components::TransformComponent& transform,
                                        float interpolation_alpha);
LightData toLightData(const components::LightComponent& light,
                      const components::TransformComponent& transform,
                      float interpolation_alpha);

}  // namespace karma::renderer::render_system
