#pragma once

#include <cstdint>

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
                    const math::Vec3& scale = {1.0f, 1.0f, 1.0f},
                    unsigned char area = kNavAreaDefault);

NavMeshInputGeometry collectNavMeshGeometry(const scene::GlbScenePrefab& prefab);
NavMeshInputGeometry collectNavMeshGeometry(const ecs::World& world,
                                            uint32_t source_mask = 0xffffffffu);

}  // namespace karma::navigation
