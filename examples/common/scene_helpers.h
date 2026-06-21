#pragma once

#include <cstddef>
#include <string>

#include <glm/glm.hpp>

#include "karma/math.h"
#include "karma/rendering.h"
#include "karma/world.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/world.h"

namespace karma::assets {
class AssetRegistry;
struct GltfSceneAsset;
}

namespace karma::components {
class TransformComponent;
}

namespace karma::world {
class World;
}

namespace karma::rendering {
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

world::MeshData makeBoxMesh(const glm::vec3& half_extents);

GltfSceneAssetBounds computeGltfSceneAssetBounds(const assets::AssetRegistry& assets,
                                                 const assets::GltfSceneAsset& scene);

GltfSceneAssetStats summarizeGltfSceneAsset(const assets::AssetRegistry& assets,
                                            const assets::GltfSceneAsset& scene);

world::Entity spawnMesh(world::World& world,
                      std::string name,
                      std::string mesh_key,
                      std::string material_key,
                      const math::Vec3& position,
                      bool visible = true);

world::Entity spawnMeshAsset(world::World& world,
                           std::string name,
                           std::string mesh_key,
                           const math::Vec3& position);

world::Entity createDebugBoxMarker(world::World& world,
                                 rendering::GraphicsDevice* graphics,
                                 assets::AssetRegistry* assets,
                                 std::string name,
                                 const math::Color& color,
                                 const math::Vec3& position,
                                 const glm::vec3& half_extents,
                                 bool visible = true);

world::Entity spawnCamera(world::World& world,
                        std::string name,
                        const math::Vec3& position,
                        const math::Quat& rotation,
                        const components::CameraComponent& camera,
                        bool add_audio_listener = true);

world::Entity spawnDirectionalLight(world::World& world,
                                  std::string name,
                                  const math::Vec3& position,
                                  const math::Quat& rotation,
                                  const components::LightComponent& light);

world::Entity spawnPointLight(world::World& world,
                            std::string name,
                            const math::Vec3& position,
                            const components::LightComponent& light);

world::Entity spawnEnvironment(world::World& world,
                             assets::AssetRegistry* assets,
                             std::string name,
                             std::string environment_map,
                             float intensity,
                             bool draw_skybox);

}  // namespace karma::demo::helpers
