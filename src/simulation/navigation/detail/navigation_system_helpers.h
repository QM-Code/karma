#pragma once

#include <cstdint>

#include "karma/math.h"
#include "karma/navigation.h"
#include "karma/components.h"
#include "karma/world.h"

namespace karma::world {
class World;
}

namespace karma::assets {
class AssetRegistry;
}

namespace karma::navigation::detail {

uint64_t entityKey(world::Entity entity);
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
  world::Entity entity{};
  components::NavMeshComponent* component = nullptr;
};

struct TileCacheSelection {
  world::Entity entity{};
  components::NavMeshComponent* nav_mesh = nullptr;
  components::NavTileCacheComponent* tile_cache = nullptr;
};

struct CrowdSelection {
  world::Entity entity{};
  components::NavMeshComponent* nav_mesh = nullptr;
  components::NavCrowdComponent* crowd = nullptr;
};

bool navMeshUsable(const components::NavMeshComponent& nav_mesh);
bool tileCacheUsable(const components::NavMeshComponent& nav_mesh,
                     const components::NavTileCacheComponent& tile_cache);
bool crowdUsable(const components::NavMeshComponent& nav_mesh,
                 const components::NavCrowdComponent& crowd);

NavMeshSelection findNavMesh(world::World& world, world::Entity preferred);
CrowdSelection findCrowd(world::World& world, world::Entity preferred);
TileCacheSelection findTileCache(world::World& world, world::Entity preferred);

math::Vec3 addVec3(const math::Vec3& a, const math::Vec3& b);
math::Vec3 scaledByAbs(const math::Vec3& value, const math::Vec3& scale);
float horizontalScale(const math::Vec3& scale);

void invalidateObstacleRefsForCache(world::World& world, world::Entity nav_mesh_entity);
void invalidateCrowdAgentsForCrowd(world::World& world, world::Entity crowd_entity);

math::Vec3 crowdSpacePosition(const math::Vec3& world_position,
                              const components::NavCrowdAgentComponent& agent);
math::Vec3 crowdWorldPosition(const math::Vec3& crowd_position,
                              const components::NavCrowdAgentComponent& agent);
float horizontalDistance(const math::Vec3& a, const math::Vec3& b);

void rebuildNavMeshes(world::World& world,
                      const assets::AssetRegistry* assets,
                      NavigationSystemStats* stats = nullptr);
void syncTileCaches(world::World& world, float dt);
void syncCrowds(world::World& world, float dt);
void moveAgents(world::World& world, float dt);

}  // namespace karma::navigation::detail
