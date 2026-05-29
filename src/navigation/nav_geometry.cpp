#include "karma/navigation/nav_geometry.h"

#include <cstdint>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/components/collider.h"
#include "karma/components/mesh.h"
#include "karma/components/transform.h"
#include "karma/ecs/world.h"
#include "karma/geometry/mesh_loader.h"
#include "karma/scene/glb_scene_import.h"

namespace karma::navigation {
namespace {

glm::vec3 toGlm(const math::Vec3& v) {
  return {v.x, v.y, v.z};
}

glm::quat toGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

math::Vec3 toVec3(const glm::vec3& v) {
  return {v.x, v.y, v.z};
}

glm::mat4 makeTransform(const math::Vec3& position,
                        const math::Quat& rotation,
                        const math::Vec3& scale) {
  glm::mat4 transform(1.0f);
  transform = glm::translate(transform, toGlm(position));
  transform *= glm::mat4_cast(toGlm(rotation));
  transform = glm::scale(transform, toGlm(scale));
  return transform;
}

template <class Mesh>
void appendMesh(NavMeshInputGeometry& out,
                const Mesh& mesh,
                const glm::mat4& transform) {
  const uint32_t base_vertex = static_cast<uint32_t>(out.vertices.size());
  out.vertices.reserve(out.vertices.size() + mesh.vertices.size());
  for (const glm::vec3& vertex : mesh.vertices) {
    const glm::vec4 world = transform * glm::vec4(vertex, 1.0f);
    out.vertices.push_back(toVec3(glm::vec3(world)));
  }

  out.indices.reserve(out.indices.size() + mesh.indices.size());
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const uint32_t a = static_cast<uint32_t>(mesh.indices[i]);
    const uint32_t b = static_cast<uint32_t>(mesh.indices[i + 1]);
    const uint32_t c = static_cast<uint32_t>(mesh.indices[i + 2]);
    if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size()) {
      continue;
    }
    out.indices.push_back(base_vertex + a);
    out.indices.push_back(base_vertex + b);
    out.indices.push_back(base_vertex + c);
  }
}

}  // namespace

void appendGeometry(NavMeshInputGeometry& out,
                    const renderer::MeshData& mesh,
                    const math::Vec3& position,
                    const math::Quat& rotation,
                    const math::Vec3& scale) {
  appendMesh(out, mesh, makeTransform(position, rotation, scale));
}

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

NavMeshInputGeometry collectNavMeshGeometry(const ecs::World& world) {
  NavMeshInputGeometry geometry;
  world.forEach<components::MeshColliderComponent, components::MeshComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
        const auto& mesh_component = world.get<components::MeshComponent>(entity);
        if (mesh_component.mesh_key.empty()) {
          return;
        }

        const auto& transform = world.get<components::TransformComponent>(entity);
        const glm::mat4 world_transform = makeTransform(transform.getPosition(),
                                                        transform.getRotation(),
                                                        transform.getScale());
        const std::vector<karma::geometry::MeshData> meshes =
            karma::geometry::loadGLB(mesh_component.mesh_key);
        for (const auto& mesh : meshes) {
          appendMesh(geometry, mesh, world_transform);
        }
      });
  return geometry;
}

}  // namespace karma::navigation

