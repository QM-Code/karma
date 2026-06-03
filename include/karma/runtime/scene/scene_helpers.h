#pragma once

#include <string>

#include <glm/glm.hpp>

#include "karma/core/math/types.h"
#include "karma/rendering/renderer/ids.h"
#include "karma/rendering/renderer/material.h"
#include "karma/rendering/renderer/mesh_data.h"
#include "karma/world/components/camera.h"
#include "karma/world/components/light.h"
#include "karma/world/ecs/entity.h"

namespace karma::components {
class TransformComponent;
}

namespace karma::ecs {
class World;
}

namespace karma::renderer {
class GraphicsDevice;
}

namespace karma::runtime {

/// \ingroup karma_runtime
/// Creates CPU mesh data for an axis-aligned box.
renderer::MeshData makeBoxMesh(const glm::vec3& half_extents);

/// \ingroup karma_runtime
/// Creates a simple material on the graphics device.
renderer::MaterialId createMaterial(renderer::GraphicsDevice* graphics,
                                    const math::Color& color,
                                    bool unlit = false,
                                    float roughness = 0.85f,
                                    float metallic = 0.0f);

/// \ingroup karma_runtime
/// Spawns an entity using direct renderer mesh/material ids.
ecs::Entity spawnMesh(ecs::World& world,
                      std::string name,
                      renderer::MeshId mesh,
                      renderer::MaterialId material,
                      const math::Vec3& position,
                      bool visible = true);

/// \ingroup karma_runtime
/// Spawns an entity that resolves mesh/material keys through `RenderSystem`.
ecs::Entity spawnMeshAsset(ecs::World& world,
                           std::string name,
                           std::string mesh_key,
                           const math::Vec3& position);

/// \ingroup karma_runtime
/// Spawns a small box marker for debug visualization.
ecs::Entity createDebugBoxMarker(ecs::World& world,
                                 renderer::GraphicsDevice* graphics,
                                 std::string name,
                                 const math::Color& color,
                                 const math::Vec3& position,
                                 const glm::vec3& half_extents,
                                 bool visible = true);

/// \ingroup karma_runtime
/// Spawns a camera entity and optionally an audio listener.
ecs::Entity spawnCamera(ecs::World& world,
                        std::string name,
                        const math::Vec3& position,
                        const math::Quat& rotation,
                        const components::CameraComponent& camera,
                        bool add_audio_listener = true);

/// \ingroup karma_runtime
/// Spawns a directional light entity.
ecs::Entity spawnDirectionalLight(ecs::World& world,
                                  std::string name,
                                  const math::Vec3& position,
                                  const math::Quat& rotation,
                                  const components::LightComponent& light);

/// \ingroup karma_runtime
/// Spawns a point light entity.
ecs::Entity spawnPointLight(ecs::World& world,
                            std::string name,
                            const math::Vec3& position,
                            const components::LightComponent& light);

/// \ingroup karma_runtime
/// Spawns an environment component entity.
ecs::Entity spawnEnvironment(ecs::World& world,
                             std::string name,
                             std::string environment_map,
                             float intensity,
                             bool draw_skybox);

}  // namespace karma::runtime
