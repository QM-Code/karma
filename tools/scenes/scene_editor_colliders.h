#pragma once

#include "karma/components.h"
#include "karma/scenes.h"

#include <cstdint>
#include <vector>

namespace karma::tools::scene_editor {

/// One world-space segment in the selected collider's wire representation.
struct ColliderWireLine {
  math::Vec3 from{};
  math::Vec3 to{};
};

struct ColliderWireGeometry {
  std::vector<ColliderWireLine> lines;

  bool empty() const { return lines.empty(); }
};

/// Builds finite, bounded wire geometry for the collider shapes exposed by the
/// scene editor's typed inspector. Convex-hull, triangle, and height-field
/// point/sample arrays intentionally remain advanced-JSON-only and return no
/// geometry. Mesh colliders use their authored vertex bounds, or a unit bound
/// when only an asset key is available.
ColliderWireGeometry buildColliderWireGeometry(
    const components::ColliderComponent& collider,
    const scenes::SceneTransform& world_transform,
    uint32_t curve_segments = 24u);

}  // namespace karma::tools::scene_editor
