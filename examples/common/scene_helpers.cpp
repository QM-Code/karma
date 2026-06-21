#include "scene_helpers.h"

#include <cstdint>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "karma/assets.h"
#include "karma/rendering.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/world.h"

namespace karma::demo::helpers {
namespace {

void appendVertex(world::MeshData& mesh,
                  const glm::vec3& position,
                  const glm::vec3& normal,
                  const glm::vec2& uv = {}) {
  mesh.vertices.push_back(position);
  mesh.normals.push_back(normal);
  mesh.uvs.push_back(uv);
  mesh.tangents.push_back({1.0f, 0.0f, 0.0f, 1.0f});
}

void appendQuad(world::MeshData& mesh,
                const glm::vec3& a,
                const glm::vec3& b,
                const glm::vec3& c,
                const glm::vec3& d,
                const glm::vec3& normal) {
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  appendVertex(mesh, a, normal, {0.0f, 0.0f});
  appendVertex(mesh, b, normal, {1.0f, 0.0f});
  appendVertex(mesh, c, normal, {1.0f, 1.0f});
  appendVertex(mesh, d, normal, {0.0f, 1.0f});
  mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
}

glm::vec3 toGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

glm::quat toGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

void expandBounds(GltfSceneAssetBounds& bounds, const glm::vec3& point) {
  if (!bounds.valid) {
    bounds.min = point;
    bounds.max = point;
    bounds.valid = true;
    return;
  }
  bounds.min = glm::min(bounds.min, point);
  bounds.max = glm::max(bounds.max, point);
}

}  // namespace

world::MeshData makeBoxMesh(const glm::vec3& half_extents) {
  const glm::vec3 min = -half_extents;
  const glm::vec3 max = half_extents;
  world::MeshData mesh;

  appendQuad(mesh, {min.x, max.y, min.z}, {max.x, max.y, min.z},
             {max.x, max.y, max.z}, {min.x, max.y, max.z}, {0.0f, 1.0f, 0.0f});
  appendQuad(mesh, {min.x, min.y, max.z}, {max.x, min.y, max.z},
             {max.x, min.y, min.z}, {min.x, min.y, min.z}, {0.0f, -1.0f, 0.0f});
  appendQuad(mesh, {min.x, min.y, min.z}, {max.x, min.y, min.z},
             {max.x, max.y, min.z}, {min.x, max.y, min.z}, {0.0f, 0.0f, -1.0f});
  appendQuad(mesh, {max.x, min.y, max.z}, {min.x, min.y, max.z},
             {min.x, max.y, max.z}, {max.x, max.y, max.z}, {0.0f, 0.0f, 1.0f});
  appendQuad(mesh, {min.x, min.y, max.z}, {min.x, min.y, min.z},
             {min.x, max.y, min.z}, {min.x, max.y, max.z}, {-1.0f, 0.0f, 0.0f});
  appendQuad(mesh, {max.x, min.y, min.z}, {max.x, min.y, max.z},
             {max.x, max.y, max.z}, {max.x, max.y, min.z}, {1.0f, 0.0f, 0.0f});

  return mesh;
}

GltfSceneAssetBounds computeGltfSceneAssetBounds(const assets::AssetRegistry& assets,
                                                 const assets::GltfSceneAsset& scene) {
  GltfSceneAssetBounds geometry_bounds{};
  GltfSceneAssetBounds fallback_bounds{};

  for (const assets::GltfSceneAssetNode& node : scene.nodes) {
    const glm::vec3 world_pos = toGlm(node.world_position);
    expandBounds(fallback_bounds, world_pos);

    const glm::vec3 world_scale = toGlm(node.world_scale);
    const glm::mat3 rotation = glm::mat3_cast(toGlm(node.world_rotation));
    for (const assets::GltfSceneAssetPrimitive& primitive : node.primitives) {
      const world::MeshData* mesh = assets.findMeshAsset(primitive.mesh_key);
      if (mesh == nullptr) {
        continue;
      }
      for (const glm::vec3& vertex : mesh->vertices) {
        expandBounds(geometry_bounds, world_pos + rotation * (vertex * world_scale));
      }
    }
  }

  return geometry_bounds.valid ? geometry_bounds : fallback_bounds;
}

GltfSceneAssetStats summarizeGltfSceneAsset(const assets::AssetRegistry& assets,
                                            const assets::GltfSceneAsset& scene) {
  GltfSceneAssetStats stats{};
  stats.node_count = scene.nodes.size();
  for (const assets::GltfSceneAssetNode& node : scene.nodes) {
    stats.primitive_count += node.primitives.size();
    for (const assets::GltfSceneAssetPrimitive& primitive : node.primitives) {
      const world::MeshData* mesh = assets.findMeshAsset(primitive.mesh_key);
      if (mesh == nullptr) {
        continue;
      }
      stats.vertex_count += mesh->vertices.size();
      stats.triangle_count += mesh->indices.size() / 3u;
    }
  }
  return stats;
}

world::Entity spawnMesh(world::World& world,
                      std::string name,
                      std::string mesh_key,
                      std::string material_key,
                      const math::Vec3& position,
                      bool visible) {
  const world::Entity entity = world.createEntity();
  world.setName(entity, std::move(name));
  components::TransformComponent transform;
  transform.setPosition(position);
  world.add(entity, transform);
  world.add(entity, components::MeshComponent{
                        .mesh_asset_key = std::move(mesh_key),
                        .materials = material_key.empty()
                            ? std::vector<components::MeshMaterialAssignment>{}
                            : std::vector<components::MeshMaterialAssignment>{
                                  components::MeshMaterialAssignment{
                                      .slot = 0,
                                      .material_key = std::move(material_key),
                                  }},
                        .visible = visible,
                        .shadow_visible = visible,
                    });
  return entity;
}

world::Entity spawnMeshAsset(world::World& world,
                           std::string name,
                           std::string mesh_key,
                           const math::Vec3& position) {
  const world::Entity entity = world.createEntity();
  world.setName(entity, std::move(name));
  components::TransformComponent transform;
  transform.setPosition(position);
  world.add(entity, transform);
  world.add(entity, components::MeshComponent{
                        .mesh_asset_key = std::move(mesh_key),
                    });
  return entity;
}

world::Entity createDebugBoxMarker(world::World& world,
                                 rendering::GraphicsDevice* graphics,
                                 assets::AssetRegistry* assets,
                                 std::string name,
                                 const math::Color& color,
                                 const math::Vec3& position,
                                 const glm::vec3& half_extents,
                                 bool visible) {
  const world::MeshData marker_mesh = makeBoxMesh(half_extents);
  const std::string mesh_key = "runtime/debug_box/" + name + "/mesh";
  const std::string material_key = "runtime/debug_box/" + name + "/material";
  if (graphics != nullptr && assets != nullptr) {
    assets->registerMeshAsset(mesh_key, marker_mesh);
  }
  if (assets != nullptr) {
    rendering::MaterialDesc material;
    material.base_color = color;
    material.emissive_color = color;
    material.unlit = true;
    material.roughness = 0.85f;
    assets->registerMaterialAsset(material_key, material);
  }
  return spawnMesh(world, std::move(name), mesh_key, material_key, position, visible);
}

world::Entity spawnCamera(world::World& world,
                        std::string name,
                        const math::Vec3& position,
                        const math::Quat& rotation,
                        const components::CameraComponent& camera,
                        bool add_audio_listener) {
  const world::Entity entity = world.createEntity();
  world.setName(entity, std::move(name));
  components::TransformComponent transform;
  transform.setPosition(position);
  transform.setRotation(rotation);
  world.add(entity, transform);
  world.add(entity, camera);
  if (add_audio_listener) {
    world.add(entity, components::AudioListenerComponent{});
  }
  return entity;
}

world::Entity spawnDirectionalLight(world::World& world,
                                  std::string name,
                                  const math::Vec3& position,
                                  const math::Quat& rotation,
                                  const components::LightComponent& light) {
  const world::Entity entity = world.createEntity();
  world.setName(entity, std::move(name));
  components::TransformComponent transform;
  transform.setPosition(position);
  transform.setRotation(rotation);
  world.add(entity, transform);
  world.add(entity, light);
  return entity;
}

world::Entity spawnPointLight(world::World& world,
                            std::string name,
                            const math::Vec3& position,
                            const components::LightComponent& light) {
  const world::Entity entity = world.createEntity();
  world.setName(entity, std::move(name));
  components::TransformComponent transform;
  transform.setPosition(position);
  world.add(entity, transform);
  world.add(entity, light);
  return entity;
}

world::Entity spawnEnvironment(world::World& world,
                             assets::AssetRegistry* assets,
                             std::string name,
                             std::string environment_map,
                             float intensity,
                             bool draw_skybox) {
  (void)assets;
  const world::Entity entity = world.createEntity();
  world.setName(entity, std::move(name));
  world.add(entity, components::EnvironmentComponent{
                        .environment_map_asset_key = std::move(environment_map),
                        .intensity = intensity,
                        .draw_skybox = draw_skybox,
                    });
  return entity;
}

}  // namespace karma::demo::helpers
