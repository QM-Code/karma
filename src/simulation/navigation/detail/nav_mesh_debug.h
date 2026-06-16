#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include <Recast.h>

#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_types.h"

class dtNavMesh;

namespace karma::navigation::detail {

size_t debugModeIndex(NavMeshDebugDrawMode mode);
bool validDebugMode(NavMeshDebugDrawMode mode);
void clearDebugLines(std::array<std::vector<NavDebugLine>, kNavMeshDebugDrawModeCount>& lines);
void clearBuildDebugLines(std::array<std::vector<NavDebugLine>, kNavMeshDebugDrawModeCount>& lines);

std::vector<math::Vec3> buildDebugEdges(const rcPolyMesh& poly_mesh);
std::vector<math::Vec3> buildDebugEdges(const dtNavMesh& nav_mesh);

void captureBuildDebugLines(const rcHeightfield& solid,
                            const rcCompactHeightfield& compact,
                            const rcContourSet& contours,
                            const rcPolyMesh& poly_mesh,
                            const rcPolyMeshDetail& detail_mesh,
                            std::array<std::vector<NavDebugLine>, kNavMeshDebugDrawModeCount>& lines);
void captureDetourDebugLines(const dtNavMesh& nav_mesh,
                             std::array<std::vector<NavDebugLine>, kNavMeshDebugDrawModeCount>& lines);

}  // namespace karma::navigation::detail
