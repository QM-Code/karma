#include "karma/simulation/navigation/nav_geometry.h"

#include "karma/content/importers/glb_scene_import.h"

namespace karma::navigation {

NavMeshInputGeometry collectNavMeshGeometry(const scene::GlbScenePrefab& prefab) {
  NavMeshInputGeometry geometry;
  if (!prefab.valid()) {
    return geometry;
  }

  for (const scene::GlbScenePrefabNode& node : prefab.nodes) {
    for (const scene::GlbScenePrefabPrimitive& primitive : node.primitives) {
      appendGeometry(geometry,
                     primitive.mesh,
                     node.world_position,
                     node.world_rotation,
                     node.world_scale);
    }
  }
  return geometry;
}

}  // namespace karma::navigation
