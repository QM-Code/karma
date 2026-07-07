#include "navigation/navmesh_test_utils.h"

int main() {
  using namespace karma::tests::navigation;

  testFlatPlaneBuildAndPath();
  testHoleForcesDetour();
  testInvalidInputFailsCleanly();
  testNearestPoint();
  testNearestPolyFlagsAndSnapshotRefresh();
  testGlbPrefabCollectionAppliesWorldTransform();
  testWorldSurfaceCollectionUsesNavMeshSurfaceArea();
  testWorldSurfaceCollectionResolvesMeshAssetKey();
  testWorldSurfaceCollectionHonorsLayerMasks();
  testAreaFlagsFilterQueries();
  testConvexVolumeMarksArea();
  testPartitionModesAndTiledSnapshot();
  testBuildDebugDrawArtifacts();
  testAdvancedQueryHelpers();
  testDynamicTraversalCostAvoidsExpensiveRegion();
  testDynamicTraversalCostUsesShorterExpensiveRegionWhenCheaper();
  testDefaultTraversalCostKeepsNormalPath();
  testTileCacheDynamicObstacleBlocksAndRestoresPath();
  testTileCacheBoxObstacleDiagnostics();
  testTileCacheSnapshotAndContentRoundTrip();
  testCrowdMovesAgentToTarget();
  testOffMeshConnectionBridgesGap();
  testSlicedPathCompletes();
  testNavigationSystemBuildsAndMovesAgent();
  testNavigationSystemTileCacheObstacleComponent();
  testNavigationSystemNavMeshCacheHitAndInvalidation();
  testNavigationSystemTileCacheCacheHitAndObstacleResync();
  testNavigationSystemBuildDebugDrawBypassesCacheOnce();
  testNavigationSystemCrowdAgentComponent();
  testCrowdAgentCharacterControllerVelocityMode();
  testReplacementRequestKeepsCurrentPathMoving();
  testNavigationSystemFollowsPrecomputedPath();
  testNavigationSystemFollowPathSkipsPassedPrefix();
  testNavigationSystemFollowPathUsesSpeedMultipliers();
  testExampleWorldGlbCanBake();
  std::cout << "navmesh tests passed\n";
  return 0;
}
