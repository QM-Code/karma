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

#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/world.h"
#include "karma/navigation.h"
#include "karma/navigation.h"
#include "karma/navigation.h"
#include "karma/navigation.h"
#include "karma/navigation.h"
#include "karma/navigation.h"
#include "karma/assets.h"

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
karma::world::MeshData makePlaneMesh(float half_extent = 5.0f);
karma::world::MeshData combineMeshes(const std::vector<karma::world::MeshData>& meshes);
karma::navigation::NavMeshInputGeometry makePlaneGeometry(float half_extent = 5.0f);
void appendQuad(karma::world::MeshData& mesh,
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
void testWorldSurfaceCollectionResolvesMeshAssetKey();
void testWorldSurfaceCollectionHonorsLayerMasks();
void testAreaFlagsFilterQueries();
void testConvexVolumeMarksArea();
void testPartitionModesAndTiledSnapshot();
void testBuildDebugDrawArtifacts();
void testAdvancedQueryHelpers();
void testDynamicTraversalCostAvoidsExpensiveRegion();
void testDynamicTraversalCostUsesShorterExpensiveRegionWhenCheaper();
void testDefaultTraversalCostKeepsNormalPath();
void testTileCacheDynamicObstacleBlocksAndRestoresPath();
void testTileCacheBoxObstacleDiagnostics();
void testTileCacheSnapshotAndContentRoundTrip();
void testCrowdMovesAgentToTarget();
void testOffMeshConnectionBridgesGap();
void testSlicedPathCompletes();
void testNavigationSystemBuildsAndMovesAgent();
void testNavigationSystemTileCacheObstacleComponent();
void testNavigationSystemNavMeshCacheHitAndInvalidation();
void testNavigationSystemTileCacheCacheHitAndObstacleResync();
void testNavigationSystemBuildDebugDrawBypassesCacheOnce();
void testNavigationSystemCrowdAgentComponent();
void testCrowdAgentCharacterControllerVelocityMode();
void testReplacementRequestKeepsCurrentPathMoving();
void testNavigationSystemFollowsPrecomputedPath();
void testNavigationSystemFollowPathSkipsPassedPrefix();
void testExampleWorldGlbCanBake();

}  // namespace karma::tests::navigation
