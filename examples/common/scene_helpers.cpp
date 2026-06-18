#include "scene_helpers.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "karma/rendering/renderer/device.h"
#include "karma/rendering/renderer/material_library.h"
#include "karma/world/components/audio_listener.h"
#include "karma/world/components/environment.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"

namespace karma::demo::helpers {
namespace {

void appendVertex(geometry::MeshData& mesh,
                  const glm::vec3& position,
                  const glm::vec3& normal,
                  const glm::vec2& uv = {}) {
  mesh.vertices.push_back(position);
  mesh.normals.push_back(normal);
  mesh.uvs.push_back(uv);
  mesh.tangents.push_back({1.0f, 0.0f, 0.0f, 1.0f});
}

void appendQuad(geometry::MeshData& mesh,
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

}  // namespace

geometry::MeshData makeBoxMesh(const glm::vec3& half_extents) {
  const glm::vec3 min = -half_extents;
  const glm::vec3 max = half_extents;
  geometry::MeshData mesh;

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

ecs::Entity spawnMesh(ecs::World& world,
                      std::string name,
                      std::string mesh_key,
                      std::string material_key,
                      const math::Vec3& position,
                      bool visible) {
  const ecs::Entity entity = world.createEntity();
  world.setName(entity, std::move(name));
  components::TransformComponent transform;
  transform.setPosition(position);
  world.add(entity, transform);
  world.add(entity, components::MeshComponent{
                        .mesh_key = std::move(mesh_key),
                        .materials = material_key.empty()
                            ? std::vector<components::MeshMaterialBinding>{}
                            : std::vector<components::MeshMaterialBinding>{
                                  components::MeshMaterialBinding{
                                      .slot = 0,
                                      .material_key = std::move(material_key),
                                  }},
                        .visible = visible,
                        .shadow_visible = visible,
                    });
  return entity;
}

ecs::Entity spawnMeshAsset(ecs::World& world,
                           std::string name,
                           std::string mesh_key,
                           const math::Vec3& position) {
  const ecs::Entity entity = world.createEntity();
  world.setName(entity, std::move(name));
  components::TransformComponent transform;
  transform.setPosition(position);
  world.add(entity, transform);
  world.add(entity, components::MeshComponent{
                        .mesh_key = std::move(mesh_key),
                    });
  return entity;
}

ecs::Entity createDebugBoxMarker(ecs::World& world,
                                 renderer::GraphicsDevice* graphics,
                                 renderer::MaterialLibrary* materials,
                                 std::string name,
                                 const math::Color& color,
                                 const math::Vec3& position,
                                 const glm::vec3& half_extents,
                                 bool visible) {
  const geometry::MeshData marker_mesh = makeBoxMesh(half_extents);
  const std::string mesh_key = "runtime/debug_box/" + name + "/mesh";
  const std::string material_key = "runtime/debug_box/" + name + "/material";
  if (graphics != nullptr) {
    graphics->registerRuntimeMesh(mesh_key, marker_mesh);
  }
  if (materials != nullptr) {
    renderer::MaterialDesc material;
    material.base_color = color;
    material.emissive_color = color;
    material.unlit = true;
    material.roughness = 0.85f;
    materials->registerMaterialDesc(material_key, material);
  }
  return spawnMesh(world, std::move(name), mesh_key, material_key, position, visible);
}

ecs::Entity spawnCamera(ecs::World& world,
                        std::string name,
                        const math::Vec3& position,
                        const math::Quat& rotation,
                        const components::CameraComponent& camera,
                        bool add_audio_listener) {
  const ecs::Entity entity = world.createEntity();
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

ecs::Entity spawnDirectionalLight(ecs::World& world,
                                  std::string name,
                                  const math::Vec3& position,
                                  const math::Quat& rotation,
                                  const components::LightComponent& light) {
  const ecs::Entity entity = world.createEntity();
  world.setName(entity, std::move(name));
  components::TransformComponent transform;
  transform.setPosition(position);
  transform.setRotation(rotation);
  world.add(entity, transform);
  world.add(entity, light);
  return entity;
}

ecs::Entity spawnPointLight(ecs::World& world,
                            std::string name,
                            const math::Vec3& position,
                            const components::LightComponent& light) {
  const ecs::Entity entity = world.createEntity();
  world.setName(entity, std::move(name));
  components::TransformComponent transform;
  transform.setPosition(position);
  world.add(entity, transform);
  world.add(entity, light);
  return entity;
}

ecs::Entity spawnEnvironment(ecs::World& world,
                             std::string name,
                             std::string environment_map,
                             float intensity,
                             bool draw_skybox) {
  const ecs::Entity entity = world.createEntity();
  world.setName(entity, std::move(name));
  world.add(entity, components::EnvironmentComponent{
                        .environment_map = std::move(environment_map),
                        .intensity = intensity,
                        .draw_skybox = draw_skybox,
                    });
  return entity;
}

}  // namespace karma::demo::helpers
