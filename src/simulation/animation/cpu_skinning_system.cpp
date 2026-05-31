#include "karma/simulation/animation/cpu_skinning_system.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/world/components/mesh.h"
#include "karma/world/components/skinned_mesh.h"
#include "karma/world/components/transform.h"

namespace karma::animation {

namespace {

glm::vec3 toGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

glm::quat toGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

glm::mat4 toMatrix(const components::TransformComponent& transform) {
  glm::mat4 matrix(1.0f);
  matrix = glm::translate(matrix, toGlm(transform.getPosition()));
  matrix *= glm::mat4_cast(toGlm(transform.getRotation()));
  matrix = glm::scale(matrix, toGlm(transform.getScale()));
  return matrix;
}

glm::vec3 transformNormal(const glm::mat4& matrix, const glm::vec3& normal) {
  const glm::vec3 transformed = glm::mat3(matrix) * normal;
  const float len2 = glm::dot(transformed, transformed);
  if (len2 <= 0.000001f) {
    return normal;
  }
  return transformed / std::sqrt(len2);
}

}  // namespace

void CpuSkinningSystem::update(ecs::World& world, renderer::GraphicsDevice& device) {
  const std::vector<ecs::Entity> entities =
      world.view<components::SkinnedMeshComponent, components::MeshComponent,
                 components::TransformComponent>();

  for (const ecs::Entity entity : entities) {
    auto& skin = world.get<components::SkinnedMeshComponent>(entity);
    auto& mesh = world.get<components::MeshComponent>(entity);
    if (!skin.enabled ||
        mesh.mesh_id == renderer::kInvalidMesh ||
        skin.bind_mesh.vertices.empty() ||
        skin.vertex_influences.size() != skin.bind_mesh.vertices.size()) {
      continue;
    }

    const glm::mat4 mesh_world = toMatrix(world.get<components::TransformComponent>(entity));
    const glm::mat4 mesh_world_inverse = glm::inverse(mesh_world);

    std::vector<glm::mat4> skin_matrices;
    skin_matrices.reserve(skin.joint_entities.size());
    for (size_t i = 0; i < skin.joint_entities.size(); ++i) {
      const ecs::Entity joint = skin.joint_entities[i];
      glm::mat4 joint_world(1.0f);
      if (world.isAlive(joint) && world.has<components::TransformComponent>(joint)) {
        joint_world = toMatrix(world.get<components::TransformComponent>(joint));
      }
      const glm::mat4 inverse_bind =
          i < skin.inverse_bind_matrices.size() ? skin.inverse_bind_matrices[i] : glm::mat4(1.0f);
      skin_matrices.push_back(mesh_world_inverse * joint_world * inverse_bind);
    }

    skin.skinned_mesh = skinMesh(skin.bind_mesh, skin.vertex_influences, skin_matrices);
    device.updateMesh(mesh.mesh_id, skin.skinned_mesh);
  }
}

renderer::MeshData skinMesh(const renderer::MeshData& bind_mesh,
                            const std::vector<components::VertexSkinInfluence>& influences,
                            const std::vector<glm::mat4>& skin_matrices) {
  renderer::MeshData skinned_mesh = bind_mesh;
  if (bind_mesh.vertices.empty() || influences.size() != bind_mesh.vertices.size()) {
    return skinned_mesh;
  }

  const bool has_normals = bind_mesh.normals.size() == bind_mesh.vertices.size();
  const bool has_tangents = bind_mesh.tangents.size() == bind_mesh.vertices.size();
  for (size_t vertex_index = 0; vertex_index < bind_mesh.vertices.size(); ++vertex_index) {
    const auto& influence = influences[vertex_index];
    glm::mat4 skin_matrix(0.0f);
    for (int slot = 0; slot < 4; ++slot) {
      const uint32_t joint_index = influence.joints[slot];
      const float weight = influence.weights[slot];
      if (weight <= 0.0f || joint_index >= skin_matrices.size()) {
        continue;
      }
      skin_matrix += skin_matrices[joint_index] * weight;
    }

    const float total_weight =
        influence.weights.x + influence.weights.y + influence.weights.z + influence.weights.w;
    if (total_weight <= 0.0f) {
      continue;
    }

    const glm::vec4 position = skin_matrix * glm::vec4(bind_mesh.vertices[vertex_index], 1.0f);
    skinned_mesh.vertices[vertex_index] = glm::vec3(position);
    if (has_normals) {
      skinned_mesh.normals[vertex_index] = transformNormal(skin_matrix, bind_mesh.normals[vertex_index]);
    }
    if (has_tangents) {
      const glm::vec3 tangent = transformNormal(skin_matrix, glm::vec3(bind_mesh.tangents[vertex_index]));
      skinned_mesh.tangents[vertex_index] = glm::vec4(tangent, bind_mesh.tangents[vertex_index].w);
    }
  }

  return skinned_mesh;
}

}  // namespace karma::animation
