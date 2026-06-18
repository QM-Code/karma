#include "karma/simulation/navigation/nav_geometry.h"

#include "karma/content/importers/gltf_scene_import.h"

namespace karma::navigation {

NavMeshInputGeometry collectNavMeshGeometry(const scene::GltfScenePrefab& prefab) {
  NavMeshInputGeometry geometry;
  if (!prefab.valid()) {
    return geometry;
  }

  for (const scene::GltfScenePrefabNode& node : prefab.nodes) {
    for (const scene::GltfScenePrefabPrimitive& primitive : node.primitives) {
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
