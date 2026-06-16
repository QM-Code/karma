#pragma once

#include <string_view>

namespace karma::demo {

enum class RecastNavigationSampleKind {
  SoloMesh,
  TileMesh,
  TempObstacles,
  Debug,
};

const char* recastNavigationSampleName(RecastNavigationSampleKind kind);
int runRecastNavigationSample(RecastNavigationSampleKind kind);

}  // namespace karma::demo
