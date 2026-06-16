#pragma once

#include <cstdint>

#include "navigation_examples.h"

#include "karma/core/math/types.h"
#include "karma/simulation/navigation/nav_geometry_types.h"
#include "karma/simulation/navigation/nav_mesh.h"
#include "karma/world/ecs/entity.h"
#include "karma/world/geometry/mesh_data.h"

namespace karma::demo::navigation_examples {

inline constexpr float kGroundY = 0.0f;
inline constexpr uint16_t kFlagWater = 1u << 4u;
inline constexpr uint16_t kFlagDoor = 1u << 5u;
inline constexpr uint16_t kFlagDisabled = 1u << 6u;
inline constexpr unsigned char kAreaWater = 2;
inline constexpr unsigned char kAreaDoor = 3;

struct Bounds {
  math::Vec3 min{};
  math::Vec3 max{};
};

struct SurfaceBuild {
  geometry::MeshData mesh;
  navigation::NavMeshInputGeometry geometry;
};

struct AgentVisual {
  int id = -1;
  ecs::Entity marker{};
};

SurfaceBuild makeOpenSurface(float half_extent = 8.0f);
SurfaceBuild makeRingSurface();
SurfaceBuild makeOffMeshSurface();
Bounds computeBounds(const navigation::NavMeshInputGeometry& geometry);
math::Vec3 midpoint(const math::Vec3& a, const math::Vec3& b);
float distance2D(const math::Vec3& a, const math::Vec3& b);
navigation::NavMeshBuildConfig defaultBuildConfig(ExampleKind kind);
bool imguiCapturesMouse();

}  // namespace karma::demo::navigation_examples
