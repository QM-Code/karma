#pragma once

#include <cstdint>

#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_types.h"
#include "karma/world/components/nav_crowd.h"
#include "karma/world/components/nav_mesh.h"
#include "karma/world/components/nav_mesh_agent.h"
#include "karma/world/components/nav_tile_cache.h"
#include "karma/world/ecs/entity.h"

namespace karma::ecs {
class World;
}

namespace karma::navigation::detail {

uint64_t entityKey(ecs::Entity entity);
void incrementBuildVersion(components::NavMeshComponent& nav_mesh);

math::Vec3 navSpacePosition(const math::Vec3& world_position,
                            const components::NavMeshAgentComponent& agent);
math::Vec3 worldSpacePosition(const math::Vec3& nav_position,
                              const components::NavMeshAgentComponent& agent,
                              float fallback_y);
void clearStoredPath(components::NavMeshAgentComponent& agent);
bool hasActivePath(const components::NavMeshAgentComponent& agent);
void failPathRequest(components::NavMeshAgentComponent& agent, NavStatus status);

struct NavMeshSelection {
  ecs::Entity entity{};
  components::NavMeshComponent* component = nullptr;
};

struct TileCacheSelection {
  ecs::Entity entity{};
  components::NavMeshComponent* nav_mesh = nullptr;
  components::NavTileCacheComponent* tile_cache = nullptr;
};

struct CrowdSelection {
  ecs::Entity entity{};
  components::NavMeshComponent* nav_mesh = nullptr;
  components::NavCrowdComponent* crowd = nullptr;
};

bool navMeshUsable(const components::NavMeshComponent& nav_mesh);
bool tileCacheUsable(const components::NavMeshComponent& nav_mesh,
                     const components::NavTileCacheComponent& tile_cache);
bool crowdUsable(const components::NavMeshComponent& nav_mesh,
                 const components::NavCrowdComponent& crowd);

NavMeshSelection findNavMesh(ecs::World& world, ecs::Entity preferred);
CrowdSelection findCrowd(ecs::World& world, ecs::Entity preferred);
TileCacheSelection findTileCache(ecs::World& world, ecs::Entity preferred);

math::Vec3 addVec3(const math::Vec3& a, const math::Vec3& b);
math::Vec3 scaledByAbs(const math::Vec3& value, const math::Vec3& scale);
float horizontalScale(const math::Vec3& scale);

void invalidateObstacleRefsForCache(ecs::World& world, ecs::Entity nav_mesh_entity);
void invalidateCrowdAgentsForCrowd(ecs::World& world, ecs::Entity crowd_entity);

math::Vec3 crowdSpacePosition(const math::Vec3& world_position,
                              const components::NavCrowdAgentComponent& agent);
math::Vec3 crowdWorldPosition(const math::Vec3& crowd_position,
                              const components::NavCrowdAgentComponent& agent);
float horizontalDistance(const math::Vec3& a, const math::Vec3& b);

void rebuildNavMeshes(ecs::World& world);
void syncTileCaches(ecs::World& world, float dt);
void syncCrowds(ecs::World& world, float dt);
void moveAgents(ecs::World& world, float dt);

}  // namespace karma::navigation::detail
