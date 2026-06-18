#include "karma/simulation/animation/cpu_skinning_system.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/core/math/glm.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/morph_target.h"
#include "karma/world/components/skinned_mesh.h"
#include "karma/world/components/transform.h"
#include "karma/world/scene/scene.h"

namespace karma::animation {

namespace {

glm::mat4 toMatrix(const components::TransformComponent& transform) {
  glm::mat4 matrix(1.0f);
  matrix = glm::translate(matrix, math::toGlm(transform.getPosition()));
  matrix *= glm::mat4_cast(math::toGlm(transform.getRotation()));
  matrix = glm::scale(matrix, math::toGlm(transform.getScale()));
  return matrix;
}

glm::mat4 toLocalMatrix(const components::TransformComponent& transform) {
  glm::mat4 matrix(1.0f);
  matrix = glm::translate(matrix, math::toGlm(transform.localPosition()));
  matrix *= glm::mat4_cast(math::toGlm(transform.localRotation()));
  matrix = glm::scale(matrix, math::toGlm(transform.localScale()));
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
      if (world_.has<components::TransformComponent>(node.entity)) {
        const auto& transform = world_.get<components::TransformComponent>(node.entity);
        if (scene_.isAlive(node.parent)) {
          world *= toLocalMatrix(transform);
        } else {
          world = toLocalMatrix(transform);
        }
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

std::vector<float> effectiveMorphWeights(const components::MorphTargetComponent& morph) {
  std::vector<float> weights = morph.weights.empty() ? morph.base_weights : morph.weights;
  weights.resize(morph.bind_mesh.morph_targets.size(), 0.0f);
  return weights;
}

bool hasActiveMorphWeights(const std::vector<float>& weights) {
  return std::any_of(weights.begin(), weights.end(), [](float weight) {
    return std::abs(weight) > 0.000001f;
  });
}

bool prepareMorphMesh(components::MorphTargetComponent& morph, geometry::MeshData& out_mesh) {
  out_mesh = morph.bind_mesh;
  if (!morph.enabled || morph.bind_mesh.vertices.empty() || morph.bind_mesh.morph_targets.empty()) {
    morph.weights_dirty = false;
    return false;
  }

  const std::vector<float> weights = effectiveMorphWeights(morph);
  const bool active = hasActiveMorphWeights(weights);
  if (!active) {
    morph.weights_dirty = false;
    return false;
  }

  if (morph.weights_dirty || morph.deformed_mesh.vertices.size() != morph.bind_mesh.vertices.size()) {
    morph.deformed_mesh = morphMesh(morph.bind_mesh, weights);
    morph.weights_dirty = false;
  }
  out_mesh = morph.deformed_mesh;
  return true;
}

}  // namespace

void CpuSkinningSystem::update(ecs::World& world,
                               const scene::Scene& scene,
                               renderer::GraphicsDevice& device) {
  const std::vector<ecs::Entity> morph_entities =
      world.view<components::MorphTargetComponent, components::MeshComponent>();
  for (const ecs::Entity entity : morph_entities) {
    if (world.has<components::SkinnedMeshComponent>(entity)) {
      continue;
    }
    auto& morph = world.get<components::MorphTargetComponent>(entity);
    auto& mesh = world.get<components::MeshComponent>(entity);
    const renderer::MeshId renderer_mesh = device.findRuntimeMesh(mesh.mesh_key);
    if (renderer_mesh == renderer::kInvalidMesh || morph.bind_mesh.vertices.empty()) {
      morph.diagnostic = "Morph mesh key is not registered as a runtime mesh";
      continue;
    }

    const bool morph_was_dirty = morph.weights_dirty;
    geometry::MeshData deformed_mesh{};
    const bool active = prepareMorphMesh(morph, deformed_mesh);
    if (active) {
      if (morph_was_dirty || !morph.renderer_mesh_is_deformed) {
        device.updateMesh(renderer_mesh, deformed_mesh);
      }
      morph.renderer_mesh_is_deformed = true;
      morph.diagnostic.clear();
      continue;
    }

    if (morph.renderer_mesh_is_deformed) {
      device.updateMesh(renderer_mesh, morph.bind_mesh);
      morph.renderer_mesh_is_deformed = false;
    }
    morph.diagnostic.clear();
  }

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
        skin.bind_mesh.vertices.empty() ||
        skin.vertex_influences.size() != skin.bind_mesh.vertices.size()) {
      skin.palette_valid = false;
      continue;
    }

    geometry::MeshData deformed_bind_mesh{};
    geometry::MeshData* mesh_for_skinning = &skin.bind_mesh;
    bool morph_active = false;
    bool morph_was_dirty = false;
    if (world.has<components::MorphTargetComponent>(entity)) {
      auto& morph = world.get<components::MorphTargetComponent>(entity);
      morph_was_dirty = morph.weights_dirty;
      if (morph.bind_mesh.vertices.empty()) {
        morph.bind_mesh = skin.bind_mesh;
      }
      morph_active = prepareMorphMesh(morph, deformed_bind_mesh);
      if (morph_active) {
        mesh_for_skinning = &deformed_bind_mesh;
        morph.renderer_mesh_is_deformed = true;
      } else {
        morph.renderer_mesh_is_deformed = false;
      }
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

    const renderer::MeshId renderer_mesh = device.findRuntimeMesh(mesh.mesh_key);
    if (renderer_mesh == renderer::kInvalidMesh) {
      skin.palette_valid = false;
      skin.diagnostic = "Skinned mesh key is not registered as a runtime mesh";
      continue;
    }
    if (skin.skinning_path == components::SkinningPath::Gpu) {
      if (morph_active) {
        if (morph_was_dirty || skin.renderer_mesh_is_bind_pose) {
          device.updateMesh(renderer_mesh, *mesh_for_skinning);
          skin.renderer_mesh_is_bind_pose = false;
        }
      } else if (!skin.renderer_mesh_is_bind_pose) {
        skin.skinned_mesh = skin.bind_mesh;
        if (renderer_mesh != renderer::kInvalidMesh) {
          device.updateMesh(renderer_mesh, skin.bind_mesh);
        }
        skin.renderer_mesh_is_bind_pose = true;
      }
      continue;
    }

    skin.skinned_mesh = skinMesh(*mesh_for_skinning, skin.vertex_influences, skin.joint_palette);
    device.updateMesh(renderer_mesh, skin.skinned_mesh);
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

geometry::MeshData skinMesh(const geometry::MeshData& bind_mesh,
                            const std::vector<components::VertexSkinInfluence>& influences,
                            const std::vector<glm::mat4>& skin_matrices) {
  geometry::MeshData skinned_mesh = bind_mesh;
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

geometry::MeshData morphMesh(const geometry::MeshData& bind_mesh,
                             const std::vector<float>& weights) {
  geometry::MeshData morphed_mesh = bind_mesh;
  if (bind_mesh.vertices.empty() || bind_mesh.morph_targets.empty() || weights.empty()) {
    return morphed_mesh;
  }

  const bool has_normals = bind_mesh.normals.size() == bind_mesh.vertices.size();
  const bool has_tangents = bind_mesh.tangents.size() == bind_mesh.vertices.size();
  const size_t target_count = std::min(bind_mesh.morph_targets.size(), weights.size());
  for (size_t target_index = 0; target_index < target_count; ++target_index) {
    const float weight = weights[target_index];
    if (std::abs(weight) <= 0.000001f) {
      continue;
    }

    const geometry::MeshData::MorphTarget& target = bind_mesh.morph_targets[target_index];
    if (target.position_deltas.size() == bind_mesh.vertices.size()) {
      for (size_t vertex_index = 0; vertex_index < bind_mesh.vertices.size(); ++vertex_index) {
        morphed_mesh.vertices[vertex_index] += target.position_deltas[vertex_index] * weight;
      }
    }
    if (has_normals && target.normal_deltas.size() == bind_mesh.vertices.size()) {
      for (size_t vertex_index = 0; vertex_index < bind_mesh.vertices.size(); ++vertex_index) {
        morphed_mesh.normals[vertex_index] += target.normal_deltas[vertex_index] * weight;
      }
    }
    if (has_tangents && target.tangent_deltas.size() == bind_mesh.vertices.size()) {
      for (size_t vertex_index = 0; vertex_index < bind_mesh.vertices.size(); ++vertex_index) {
        const glm::vec3 tangent =
            glm::vec3(morphed_mesh.tangents[vertex_index]) +
            target.tangent_deltas[vertex_index] * weight;
        morphed_mesh.tangents[vertex_index] =
            glm::vec4(tangent, morphed_mesh.tangents[vertex_index].w);
      }
    }
  }

  if (has_normals) {
    for (glm::vec3& normal : morphed_mesh.normals) {
      const float len2 = glm::dot(normal, normal);
      if (len2 > 0.000001f) {
        normal /= std::sqrt(len2);
      }
    }
  }
  if (has_tangents) {
    for (glm::vec4& tangent4 : morphed_mesh.tangents) {
      glm::vec3 tangent = glm::vec3(tangent4);
      const float len2 = glm::dot(tangent, tangent);
      if (len2 > 0.000001f) {
        tangent /= std::sqrt(len2);
        tangent4 = glm::vec4(tangent, tangent4.w);
      }
    }
  }

  return morphed_mesh;
}

}  // namespace karma::animation
