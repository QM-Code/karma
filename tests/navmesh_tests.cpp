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
  testCrowdAgentPlayerControllerVelocityMode();
  testReplacementRequestKeepsCurrentPathMoving();
  testExampleWorldGlbCanBake();
  std::cout << "navmesh tests passed\n";
  return 0;
}
