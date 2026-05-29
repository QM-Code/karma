#pragma once

#include "karma/navigation/nav_mesh.h"
#include "karma/renderer/types.h"

namespace karma::ecs {
class World;
}

namespace karma::scene {
struct GlbScenePrefab;
}

namespace karma::navigation {

void appendGeometry(NavMeshInputGeometry& out,
                    const renderer::MeshData& mesh,
                    const math::Vec3& position = {},
                    const math::Quat& rotation = {},
                    const math::Vec3& scale = {1.0f, 1.0f, 1.0f});

NavMeshInputGeometry collectNavMeshGeometry(const scene::GlbScenePrefab& prefab);
NavMeshInputGeometry collectNavMeshGeometry(const ecs::World& world);

}  // namespace karma::navigation
