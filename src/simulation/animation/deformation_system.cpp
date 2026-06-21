#include "karma/world.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/assets.h"
#include "karma/math.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/world.h"

namespace karma::world {

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
  SceneMatrixResolver(const world::World& world, const world::Scene& scene)
      : world_(world), scene_(scene), cache_(scene.nodes().size(), glm::mat4(1.0f)),
        computed_(scene.nodes().size(), 0u) {}

  glm::mat4 entityWorld(world::Entity entity, const glm::mat4& fallback) {
    if (!entity.isValid() || !world_.isAlive(entity)) {
      return fallback;
    }

    const world::NodeId node_id = scene_.findNode(entity);
    if (scene_.isAlive(node_id)) {
      return nodeWorld(node_id);
    }

    if (world_.has<components::TransformComponent>(entity)) {
      return toMatrix(world_.get<components::TransformComponent>(entity));
    }
    return fallback;
  }

 private:
  glm::mat4 nodeWorld(world::NodeId node_id) {
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

    const world::Node& node = scene_.get(node_id);
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

  const world::World& world_;
  const world::Scene& scene_;
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

std::vector<float> effectiveMorphWeights(const components::DeformableMeshComponent& deformation) {
  std::vector<float> weights = deformation.morph_weights.empty()
                                   ? deformation.base_morph_weights
                                   : deformation.morph_weights;
  weights.resize(deformation.bind_mesh.morph_targets.size(), 0.0f);
  return weights;
}

bool hasActiveMorphWeights(const std::vector<float>& weights) {
  return std::any_of(weights.begin(), weights.end(), [](float weight) {
    return std::abs(weight) > 0.000001f;
  });
}

bool prepareMorphMesh(components::DeformableMeshComponent& deformation,
                      world::MeshData& out_mesh) {
  out_mesh = deformation.bind_mesh;
  if (!deformation.enabled ||
      deformation.bind_mesh.vertices.empty() ||
      deformation.bind_mesh.morph_targets.empty()) {
    deformation.morph_weights_dirty = false;
    return false;
  }

  const std::vector<float> weights = effectiveMorphWeights(deformation);
  const bool active = hasActiveMorphWeights(weights);
  if (!active) {
    deformation.morph_weights_dirty = false;
    return false;
  }

  if (deformation.morph_weights_dirty ||
      deformation.cpu_deformed_mesh.vertices.size() != deformation.bind_mesh.vertices.size()) {
    deformation.cpu_deformed_mesh = morphMesh(deformation.bind_mesh, weights);
    deformation.morph_weights_dirty = false;
  }
  out_mesh = deformation.cpu_deformed_mesh;
  return true;
}

}  // namespace

void DeformationSystem::update(world::World& world,
                               const world::Scene& scene,
                               rendering::GraphicsDevice& device,
                               const assets::AssetRegistry* assets) {
  const std::vector<world::Entity> entities =
      world.view<components::DeformableMeshComponent, components::MeshComponent,
                 components::TransformComponent>();

  for (const world::Entity entity : entities) {
    auto& deformation = world.get<components::DeformableMeshComponent>(entity);
    auto& mesh = world.get<components::MeshComponent>(entity);
    if (deformation.path == components::DeformationPath::Gpu) {
      deformation.diagnostic.clear();
    }
    if (!deformation.enabled || deformation.bind_mesh.vertices.empty()) {
      deformation.palette_valid = false;
      continue;
    }

    world::MeshData deformed_bind_mesh{};
    world::MeshData* mesh_for_deformation = &deformation.bind_mesh;
    const bool morph_was_dirty = deformation.morph_weights_dirty;
    const bool morph_active = prepareMorphMesh(deformation, deformed_bind_mesh);
    if (morph_active) {
      mesh_for_deformation = &deformed_bind_mesh;
    }

    const glm::mat4 mesh_world = toMatrix(world.get<components::TransformComponent>(entity));
    const bool has_skin = deformation.skinned() &&
                          deformation.vertex_influences.size() ==
                              deformation.bind_mesh.vertices.size();
    if (has_skin) {
      SkinningPalette palette =
          buildSkinningPaletteFromScene(deformation, world, scene, mesh_world);
      deformation.joint_palette = std::move(palette.joint_matrices);
      deformation.palette_valid = palette.valid;
      if (!palette.valid) {
        deformation.diagnostic = std::move(palette.diagnostic);
      }
    } else {
      deformation.joint_palette.clear();
      deformation.palette_valid = false;
    }

    rendering::MeshId renderer_mesh = device.findRuntimeMesh(mesh.mesh_asset_key);
    if (renderer_mesh == rendering::kInvalidMesh && assets != nullptr) {
      if (const world::MeshData* mesh_asset = assets->findMeshAsset(mesh.mesh_asset_key)) {
        renderer_mesh = device.registerRuntimeMesh(mesh.mesh_asset_key, *mesh_asset);
      }
    }
    if (renderer_mesh == rendering::kInvalidMesh) {
      deformation.palette_valid = false;
      deformation.diagnostic = "Deformable mesh asset key is not registered";
      continue;
    }

    if (deformation.path == components::DeformationPath::Gpu) {
      if (deformation.renderer_mesh_is_cpu_deformed) {
        device.updateMesh(renderer_mesh, deformation.bind_mesh);
        deformation.renderer_mesh_is_cpu_deformed = false;
      }

      rendering::DeformationDesc desc{};
      desc.skinning_enabled = has_skin && deformation.palette_valid &&
                              !deformation.joint_palette.empty();
      desc.joint_palette = desc.skinning_enabled ? deformation.joint_palette
                                                 : std::vector<glm::mat4>{};
      desc.morphing_enabled = deformation.morphable() && morph_active;
      desc.morph_weights = desc.morphing_enabled ? effectiveMorphWeights(deformation)
                                                 : std::vector<float>{};
      if (desc.skinning_enabled || desc.morphing_enabled) {
        if (deformation.deformation == rendering::kInvalidDeformation) {
          deformation.deformation = device.createDeformation(desc);
        } else {
          device.updateDeformation(deformation.deformation, desc);
        }
      } else if (deformation.deformation != rendering::kInvalidDeformation) {
        device.destroyDeformation(deformation.deformation);
        deformation.deformation = rendering::kInvalidDeformation;
      }
      deformation.diagnostic.clear();
      continue;
    }

    if (deformation.deformation != rendering::kInvalidDeformation) {
      device.destroyDeformation(deformation.deformation);
      deformation.deformation = rendering::kInvalidDeformation;
    }

    if (has_skin && deformation.palette_valid) {
      deformation.cpu_deformed_mesh =
          skinMesh(*mesh_for_deformation,
                   deformation.vertex_influences,
                   deformation.joint_palette);
      device.updateMesh(renderer_mesh, deformation.cpu_deformed_mesh);
      deformation.renderer_mesh_is_cpu_deformed = true;
      deformation.diagnostic.clear();
      continue;
    }

    if (morph_active) {
      if (morph_was_dirty || !deformation.renderer_mesh_is_cpu_deformed) {
        device.updateMesh(renderer_mesh, *mesh_for_deformation);
      }
      deformation.renderer_mesh_is_cpu_deformed = true;
      deformation.diagnostic.clear();
      continue;
    }

    if (deformation.renderer_mesh_is_cpu_deformed) {
      device.updateMesh(renderer_mesh, deformation.bind_mesh);
      deformation.renderer_mesh_is_cpu_deformed = false;
    }
    deformation.diagnostic.clear();
  }
}

SkinningPalette buildSkinningPaletteFromWorld(
    const components::DeformableMeshComponent& deformation,
    const world::World& world,
    const glm::mat4& mesh_world) {
  glm::mat4 output_space_world = mesh_world;
  if (deformation.override_render_transform) {
    output_space_world = glm::mat4(1.0f);
    if (deformation.render_transform_entity.isValid() &&
        world.isAlive(deformation.render_transform_entity) &&
        world.has<components::TransformComponent>(deformation.render_transform_entity)) {
      output_space_world =
          toMatrix(world.get<components::TransformComponent>(deformation.render_transform_entity));
    }
  }

  std::vector<glm::mat4> joint_world_matrices;
  joint_world_matrices.reserve(deformation.joint_entities.size());
  for (const world::Entity joint : deformation.joint_entities) {
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
                              deformation.inverse_bind_matrices,
                              joint_world_matrices,
                              output_space_world,
                              deformation.skin_index);
}

SkinningPalette buildSkinningPaletteFromScene(
    const components::DeformableMeshComponent& deformation,
    const world::World& world,
    const world::Scene& scene,
    const glm::mat4& mesh_world) {
  SceneMatrixResolver resolver(world, scene);

  glm::mat4 output_space_world = mesh_world;
  if (deformation.override_render_transform) {
    output_space_world =
        resolver.entityWorld(deformation.render_transform_entity, glm::mat4(1.0f));
  }

  std::vector<glm::mat4> joint_world_matrices;
  joint_world_matrices.reserve(deformation.joint_entities.size());
  for (const world::Entity joint : deformation.joint_entities) {
    joint_world_matrices.push_back(resolver.entityWorld(joint, glm::mat4(1.0f)));
  }

  std::vector<uint32_t> joint_indices;
  joint_indices.reserve(joint_world_matrices.size());
  for (size_t i = 0; i < joint_world_matrices.size(); ++i) {
    joint_indices.push_back(static_cast<uint32_t>(i));
  }
  return buildSkinningPalette(joint_indices,
                              deformation.inverse_bind_matrices,
                              joint_world_matrices,
                              output_space_world,
                              deformation.skin_index);
}

world::MeshData skinMesh(const world::MeshData& bind_mesh,
                            const std::vector<components::VertexSkinInfluence>& influences,
                            const std::vector<glm::mat4>& skin_matrices) {
  world::MeshData skinned_mesh = bind_mesh;
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

world::MeshData morphMesh(const world::MeshData& bind_mesh,
                             const std::vector<float>& weights) {
  world::MeshData morphed_mesh = bind_mesh;
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

    const world::MeshData::MorphTarget& target = bind_mesh.morph_targets[target_index];
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

}  // namespace karma::world
