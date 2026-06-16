#pragma once

namespace karma::demo::navigation_examples {

enum class ExampleKind {
  PointClick,
  Crowds,
  TileCache,
  QueryLab,
  OffMeshAreas,
  PhysicsBridge,
};

const char* exampleName(ExampleKind kind);
int runExample(ExampleKind kind);

}  // namespace karma::demo::navigation_examples
