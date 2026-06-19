#pragma once

#include <cstdint>

#include "karma/simulation/navigation/nav_geometry_types.h"
#include "karma/world/geometry/mesh_data.h"

namespace karma::ecs {
class World;
}

namespace karma::content {
class AssetRegistry;
}

namespace karma::navigation {

/// \ingroup karma_navigation
/// Appends transformed mesh triangles to navmesh input geometry.
void appendGeometry(NavMeshInputGeometry& out,
                    const geometry::MeshData& mesh,
                    const math::Vec3& position = {},
                    const math::Quat& rotation = {},
                    const math::Vec3& scale = {1.0f, 1.0f, 1.0f},
                    unsigned char area = kNavAreaDefault);

/// Collects navmesh geometry from ECS navmesh surface/off-mesh-link components.
NavMeshInputGeometry collectNavMeshGeometry(const ecs::World& world,
                                            uint32_t source_mask = 0xffffffffu);
/// Collects navmesh geometry from ECS components, resolving mesh asset keys
/// through the runtime asset registry when embedded mesh data is not present.
NavMeshInputGeometry collectNavMeshGeometry(const ecs::World& world,
                                            const content::AssetRegistry* assets,
                                            uint32_t source_mask = 0xffffffffu);

}  // namespace karma::navigation
