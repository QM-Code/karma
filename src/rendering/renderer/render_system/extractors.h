#pragma once

#include <cstddef>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/rendering.h"
#include "karma/components.h"

namespace karma::rendering::render_system {

glm::vec3 toGlm(const math::Vec3& v);
glm::quat toGlm(const math::Quat& q);
glm::vec3 transformPoint(const components::TransformComponent& transform,
                         const math::Vec3& local,
                         float interpolation_alpha);
glm::mat4 toTransform(const components::TransformComponent& transform,
                      float interpolation_alpha);

/// Renderer-ready instance payload after applying an owning entity's world
/// transform. Planar source instances remain compact only when every composed
/// transform can be represented by the compact position/yaw/scale layout.
struct ExtractedInstanceSet {
  InstanceGpuLayout gpu_layout = InstanceGpuLayout::Matrix4x4Params;
  std::vector<InstanceData> instances;
  std::vector<PlanarInstanceData> planar_instances;

  std::size_t instanceCount() const {
    return gpu_layout == InstanceGpuLayout::PositionYawScaleParams
               ? planar_instances.size()
               : instances.size();
  }
};

ExtractedInstanceSet extractInstanceSet(
    const components::InstanceSetComponent& component,
    const glm::mat4& owner_world_transform);

struct InstancedBounds {
  glm::vec3 center{0.0f};
  float radius = 0.0f;
  bool valid = false;
};

InstancedBounds calculateInstancedBounds(
    const ExtractedInstanceSet& instance_set,
    const glm::mat4& batch_local_transform,
    const glm::vec3& mesh_bounds_center,
    float mesh_bounds_radius,
    bool mesh_bounds_valid);

CameraData toCameraData(const components::CameraComponent& camera,
                        const components::TransformComponent& transform,
                        float interpolation_alpha);
DirectionalLightData toDirectionalLight(const components::LightComponent& light,
                                        const components::TransformComponent& transform,
                                        float interpolation_alpha);
LightData toLightData(const components::LightComponent& light,
                      const components::TransformComponent& transform,
                      float interpolation_alpha);

}  // namespace karma::rendering::render_system
