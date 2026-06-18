#pragma once

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

#include "karma/world/components/nav_mesh.h"
#include "karma/world/components/nav_mesh_agent.h"
#include "karma/world/components/nav_crowd.h"
#include "karma/world/components/nav_tile_cache.h"
#include "karma/world/components/collider.h"
#include "karma/world/components/character_controller.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"
#include "karma/content/importers/mesh_import.h"
#include "karma/simulation/navigation/nav_geometry.h"
#include "karma/simulation/navigation/nav_crowd.h"
#include "karma/simulation/navigation/nav_mesh.h"
#include "karma/simulation/navigation/nav_query.h"
#include "karma/simulation/navigation/nav_tile_cache.h"
#include "karma/simulation/navigation/navigation_system.h"
#include "karma/content/importers/glb_scene_import.h"
#include "karma/content/navigation/nav_tile_cache.h"

#ifdef NDEBUG
#undef assert
#define assert(expr)                                                            \
  do {                                                                          \
    if (!(expr)) {                                                              \
      std::cerr << "assertion failed: " #expr << " at " << __FILE__ << ":"     \
                << __LINE__ << "\n";                                           \
      std::abort();                                                             \
    }                                                                           \
  } while (false)
#endif

namespace karma::tests::navigation {

std::filesystem::path resolveRepoPath(const std::filesystem::path& relative);
karma::geometry::MeshData makePlaneMesh(float half_extent = 5.0f);
karma::geometry::MeshData combineMeshes(const std::vector<karma::geometry::MeshData>& meshes);
karma::navigation::NavMeshInputGeometry makePlaneGeometry(float half_extent = 5.0f);
void appendQuad(karma::geometry::MeshData& mesh,
                const karma::math::Vec3& a,
                const karma::math::Vec3& b,
                const karma::math::Vec3& c,
                const karma::math::Vec3& d);
karma::navigation::NavMeshInputGeometry makeRingGeometry();
karma::navigation::NavMeshInputGeometry makeCorridorGeometry();
void updateTileCacheUntilReady(karma::navigation::NavTileCache& tile_cache,
                               karma::navigation::NavMesh& nav_mesh);

void testFlatPlaneBuildAndPath();
void testHoleForcesDetour();
void testInvalidInputFailsCleanly();
void testNearestPoint();
void testNearestPolyFlagsAndSnapshotRefresh();
void testGlbPrefabCollectionAppliesWorldTransform();
void testWorldSurfaceCollectionUsesNavMeshSurfaceArea();
void testAreaFlagsFilterQueries();
void testConvexVolumeMarksArea();
void testPartitionModesAndTiledSnapshot();
void testBuildDebugDrawArtifacts();
void testAdvancedQueryHelpers();
void testTileCacheDynamicObstacleBlocksAndRestoresPath();
void testTileCacheBoxObstacleDiagnostics();
void testTileCacheSnapshotAndContentRoundTrip();
void testCrowdMovesAgentToTarget();
void testOffMeshConnectionBridgesGap();
void testSlicedPathCompletes();
void testNavigationSystemBuildsAndMovesAgent();
void testNavigationSystemTileCacheObstacleComponent();
void testNavigationSystemCrowdAgentComponent();
void testCrowdAgentCharacterControllerVelocityMode();
void testReplacementRequestKeepsCurrentPathMoving();
void testExampleWorldGlbCanBake();

}  // namespace karma::tests::navigation
