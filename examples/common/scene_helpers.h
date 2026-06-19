#pragma once

#include <cstddef>
#include <string>

#include <glm/glm.hpp>

#include "karma/core/math/types.h"
#include "karma/rendering/renderer/material.h"
#include "karma/world/geometry/mesh_data.h"
#include "karma/world/components/camera.h"
#include "karma/world/components/light.h"
#include "karma/world/ecs/entity.h"

namespace karma::content {
class AssetRegistry;
struct GltfSceneAsset;
}

namespace karma::components {
class TransformComponent;
}

namespace karma::ecs {
class World;
}

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::demo::helpers {

struct GltfSceneAssetBounds {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
  bool valid = false;
};

struct GltfSceneAssetStats {
  std::size_t node_count = 0u;
  std::size_t primitive_count = 0u;
  std::size_t vertex_count = 0u;
  std::size_t triangle_count = 0u;
};

geometry::MeshData makeBoxMesh(const glm::vec3& half_extents);

GltfSceneAssetBounds computeGltfSceneAssetBounds(const content::AssetRegistry& assets,
                                                 const content::GltfSceneAsset& scene);

GltfSceneAssetStats summarizeGltfSceneAsset(const content::AssetRegistry& assets,
                                            const content::GltfSceneAsset& scene);

ecs::Entity spawnMesh(ecs::World& world,
                      std::string name,
                      std::string mesh_key,
                      std::string material_key,
                      const math::Vec3& position,
                      bool visible = true);

ecs::Entity spawnMeshAsset(ecs::World& world,
                           std::string name,
                           std::string mesh_key,
                           const math::Vec3& position);

ecs::Entity createDebugBoxMarker(ecs::World& world,
                                 renderer::GraphicsDevice* graphics,
                                 content::AssetRegistry* assets,
                                 std::string name,
                                 const math::Color& color,
                                 const math::Vec3& position,
                                 const glm::vec3& half_extents,
                                 bool visible = true);

ecs::Entity spawnCamera(ecs::World& world,
                        std::string name,
                        const math::Vec3& position,
                        const math::Quat& rotation,
                        const components::CameraComponent& camera,
                        bool add_audio_listener = true);

ecs::Entity spawnDirectionalLight(ecs::World& world,
                                  std::string name,
                                  const math::Vec3& position,
                                  const math::Quat& rotation,
                                  const components::LightComponent& light);

ecs::Entity spawnPointLight(ecs::World& world,
                            std::string name,
                            const math::Vec3& position,
                            const components::LightComponent& light);

ecs::Entity spawnEnvironment(ecs::World& world,
                             content::AssetRegistry* assets,
                             std::string name,
                             std::string environment_map,
                             float intensity,
                             bool draw_skybox);

}  // namespace karma::demo::helpers
