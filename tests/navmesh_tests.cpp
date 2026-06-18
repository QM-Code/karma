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
  testAreaFlagsFilterQueries();
  testConvexVolumeMarksArea();
  testPartitionModesAndTiledSnapshot();
  testBuildDebugDrawArtifacts();
  testAdvancedQueryHelpers();
  testTileCacheDynamicObstacleBlocksAndRestoresPath();
  testTileCacheBoxObstacleDiagnostics();
  testTileCacheSnapshotAndContentRoundTrip();
  testCrowdMovesAgentToTarget();
  testOffMeshConnectionBridgesGap();
  testSlicedPathCompletes();
  testNavigationSystemBuildsAndMovesAgent();
  testNavigationSystemTileCacheObstacleComponent();
  testNavigationSystemCrowdAgentComponent();
  testCrowdAgentCharacterControllerVelocityMode();
  testReplacementRequestKeepsCurrentPathMoving();
  testExampleWorldGlbCanBake();
  std::cout << "navmesh tests passed\n";
  return 0;
}
