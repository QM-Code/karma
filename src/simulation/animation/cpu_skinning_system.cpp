#include "karma/simulation/animation/cpu_skinning_system.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/world/components/mesh.h"
#include "karma/world/components/skinned_mesh.h"
#include "karma/world/components/transform.h"
#include "karma/world/scene/scene.h"

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

glm::mat4 toMatrix(const components::LocalTransformComponent& transform) {
  glm::mat4 matrix(1.0f);
  matrix = glm::translate(matrix, toGlm(transform.position));
  matrix *= glm::mat4_cast(toGlm(transform.rotation));
  matrix = glm::scale(matrix, toGlm(transform.scale));
  return matrix;
}

class SceneMatrixResolver {
 public:
  SceneMatrixResolver(const ecs::World& world, const scene::Scene& scene)
      : world_(world), scene_(scene), cache_(scene.nodes().size(), glm::mat4(1.0f)),
        computed_(scene.nodes().size(), 0u) {}

  glm::mat4 entityWorld(ecs::Entity entity, const glm::mat4& fallback) {
    if (!entity.isValid() || !world_.isAlive(entity)) {
      return fallback;
    }

    const scene::NodeId node_id = scene_.findNode(entity);
    if (scene_.isAlive(node_id)) {
      return nodeWorld(node_id);
    }

    if (world_.has<components::TransformComponent>(entity)) {
      return toMatrix(world_.get<components::TransformComponent>(entity));
    }
    return fallback;
  }

 private:
  glm::mat4 nodeWorld(scene::NodeId node_id) {
    if (!scene_.isAlive(node_id)) {
      return glm::mat4(1.0f);
    }
    if (node_id >= cache_.size()) {
      cache_.resize(static_cast<size_t>(node_id) + 1u, glm::mat4(1.0f));
      computed_.resize(static_cast<size_t>(node_id) + 1u, 0u);
    }
    if (computed_[node_id] != 0u) {
      return cache_[node_id];
    }

    const scene::Node& node = scene_.get(node_id);
    glm::mat4 world = scene_.isAlive(node.parent) ? nodeWorld(node.parent) : glm::mat4(1.0f);
    if (node.entity.isValid() && world_.isAlive(node.entity)) {
      if (world_.has<components::LocalTransformComponent>(node.entity)) {
        world *= toMatrix(world_.get<components::LocalTransformComponent>(node.entity));
      } else if (world_.has<components::TransformComponent>(node.entity)) {
        world = toMatrix(world_.get<components::TransformComponent>(node.entity));
      }
    }

    cache_[node_id] = world;
    computed_[node_id] = 1u;
    return world;
  }

  const ecs::World& world_;
  const scene::Scene& scene_;
  std::vector<glm::mat4> cache_;
  std::vector<uint8_t> computed_;
};

glm::vec3 transformNormal(const glm::mat4& matrix, const glm::vec3& normal) {
  const glm::vec3 transformed = glm::mat3(matrix) * normal;
  const float len2 = glm::dot(transformed, transformed);
  if (len2 <= 0.000001f) {
    return normal;
  }
  return transformed / std::sqrt(len2);
}

}  // namespace

void CpuSkinningSystem::update(ecs::World& world,
                               const scene::Scene& scene,
                               renderer::GraphicsDevice& device) {
  const std::vector<ecs::Entity> entities =
      world.view<components::SkinnedMeshComponent, components::MeshComponent,
                 components::TransformComponent>();

  for (const ecs::Entity entity : entities) {
    auto& skin = world.get<components::SkinnedMeshComponent>(entity);
    auto& mesh = world.get<components::MeshComponent>(entity);
    if (skin.skinning_path == components::SkinningPath::Gpu) {
      skin.diagnostic.clear();
    }
    if (!skin.enabled ||
        mesh.mesh_id == renderer::kInvalidMesh ||
        skin.bind_mesh.vertices.empty() ||
        skin.vertex_influences.size() != skin.bind_mesh.vertices.size()) {
      skin.palette_valid = false;
      continue;
    }

    const glm::mat4 mesh_world = toMatrix(world.get<components::TransformComponent>(entity));
    SkinningPalette palette = buildSkinningPaletteFromScene(skin, world, scene, mesh_world);
    skin.joint_palette = std::move(palette.joint_matrices);
    skin.palette_valid = palette.valid;
    if (!palette.valid) {
      skin.diagnostic = std::move(palette.diagnostic);
      continue;
    }
    if (skin.joint_palette.size() > components::kMaxSkinningJointsPerDraw) {
      skin.palette_valid = false;
      skin.skinning_path = components::SkinningPath::GpuUnavailableCpuFallback;
      skin.diagnostic = "Skin joint count exceeds renderer palette capacity";
    }

    if (skin.skinning_path == components::SkinningPath::Gpu) {
      if (!skin.renderer_mesh_is_bind_pose) {
        skin.skinned_mesh = skin.bind_mesh;
        device.updateMesh(mesh.mesh_id, skin.bind_mesh);
        skin.renderer_mesh_is_bind_pose = true;
      }
      continue;
    }

    skin.skinned_mesh = skinMesh(skin.bind_mesh, skin.vertex_influences, skin.joint_palette);
    device.updateMesh(mesh.mesh_id, skin.skinned_mesh);
    skin.renderer_mesh_is_bind_pose = false;
  }
}

SkinningPalette buildSkinningPaletteFromWorld(
    const components::SkinnedMeshComponent& skin,
    const ecs::World& world,
    const glm::mat4& mesh_world) {
  glm::mat4 output_space_world = mesh_world;
  if (skin.override_render_transform) {
    output_space_world = glm::mat4(1.0f);
    if (skin.render_transform_entity.isValid() &&
        world.isAlive(skin.render_transform_entity) &&
        world.has<components::TransformComponent>(skin.render_transform_entity)) {
      output_space_world =
          toMatrix(world.get<components::TransformComponent>(skin.render_transform_entity));
    }
  }

  std::vector<glm::mat4> joint_world_matrices;
  joint_world_matrices.reserve(skin.joint_entities.size());
  for (const ecs::Entity joint : skin.joint_entities) {
    glm::mat4 joint_world(1.0f);
    if (world.isAlive(joint) && world.has<components::TransformComponent>(joint)) {
      joint_world = toMatrix(world.get<components::TransformComponent>(joint));
    }
    joint_world_matrices.push_back(joint_world);
  }

  std::vector<uint32_t> joint_indices;
  joint_indices.reserve(joint_world_matrices.size());
  for (size_t i = 0; i < joint_world_matrices.size(); ++i) {
    joint_indices.push_back(static_cast<uint32_t>(i));
  }
  return buildSkinningPalette(joint_indices,
                              skin.inverse_bind_matrices,
                              joint_world_matrices,
                              output_space_world,
                              skin.skin_index);
}

SkinningPalette buildSkinningPaletteFromScene(
    const components::SkinnedMeshComponent& skin,
    const ecs::World& world,
    const scene::Scene& scene,
    const glm::mat4& mesh_world) {
  SceneMatrixResolver resolver(world, scene);

  glm::mat4 output_space_world = mesh_world;
  if (skin.override_render_transform) {
    output_space_world =
        resolver.entityWorld(skin.render_transform_entity, glm::mat4(1.0f));
  }

  std::vector<glm::mat4> joint_world_matrices;
  joint_world_matrices.reserve(skin.joint_entities.size());
  for (const ecs::Entity joint : skin.joint_entities) {
    joint_world_matrices.push_back(resolver.entityWorld(joint, glm::mat4(1.0f)));
  }

  std::vector<uint32_t> joint_indices;
  joint_indices.reserve(joint_world_matrices.size());
  for (size_t i = 0; i < joint_world_matrices.size(); ++i) {
    joint_indices.push_back(static_cast<uint32_t>(i));
  }
  return buildSkinningPalette(joint_indices,
                              skin.inverse_bind_matrices,
                              joint_world_matrices,
                              output_space_world,
                              skin.skin_index);
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
