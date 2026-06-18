#include "karma/simulation/navigation/nav_geometry.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/core/math/glm.h"
#include "karma/world/components/collider.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/nav_mesh.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"

namespace karma::navigation {
namespace {

glm::mat4 makeTransform(const math::Vec3& position,
                        const math::Quat& rotation,
                        const math::Vec3& scale) {
  glm::mat4 transform(1.0f);
  transform = glm::translate(transform, math::toGlm(position));
  transform *= glm::mat4_cast(math::toGlm(rotation));
  transform = glm::scale(transform, math::toGlm(scale));
  return transform;
}

glm::vec3 toGeometryVertex(const glm::vec3& vertex) {
  return vertex;
}

glm::vec3 toGeometryVertex(const math::Vec3& vertex) {
  return math::toGlm(vertex);
}

template <class Mesh>
void appendMesh(NavMeshInputGeometry& out,
                const Mesh& mesh,
                const glm::mat4& transform,
                unsigned char area) {
  const uint32_t base_vertex = static_cast<uint32_t>(out.vertices.size());
  out.vertices.reserve(out.vertices.size() + mesh.vertices.size());
  for (const auto& vertex : mesh.vertices) {
    const glm::vec4 world = transform * glm::vec4(toGeometryVertex(vertex), 1.0f);
    out.vertices.push_back(math::fromGlm(glm::vec3(world)));
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
    out.triangle_areas.push_back(area);
  }
}

void appendOffMeshLinks(NavMeshInputGeometry& geometry,
                        const ecs::World& world,
                        uint32_t source_mask) {
  world.forEach<components::NavOffMeshLinkComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
        const auto& link = world.get<components::NavOffMeshLinkComponent>(entity);
        if (!link.enabled || (link.layer_mask & source_mask) == 0u) {
          return;
        }

        const auto& start_transform = world.get<components::TransformComponent>(entity);
        const math::Vec3 start_position = start_transform.getPosition();
        math::Vec3 end_position = start_position;
        if (link.end_entity.isValid() &&
            world.isAlive(link.end_entity) &&
            world.has<components::TransformComponent>(link.end_entity)) {
          end_position = world.get<components::TransformComponent>(link.end_entity).getPosition();
        }

        geometry.off_mesh_connections.push_back({
            .start = {start_position.x + link.start_offset.x,
                      start_position.y + link.start_offset.y,
                      start_position.z + link.start_offset.z},
            .end = {end_position.x + link.end_offset.x,
                    end_position.y + link.end_offset.y,
                    end_position.z + link.end_offset.z},
            .radius = link.radius,
            .area = link.area,
            .flags = link.flags,
            .bidirectional = link.bidirectional,
            .user_id = link.user_id,
        });
      });
}

void appendConvexVolumes(NavMeshInputGeometry& geometry,
                         const ecs::World& world,
                         uint32_t source_mask) {
  world.forEach<components::NavConvexVolumeComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
        const auto& volume = world.get<components::NavConvexVolumeComponent>(entity);
        if (!volume.enabled ||
            volume.vertices.size() < 3 ||
            volume.area == kNavAreaNull ||
            (volume.layer_mask & source_mask) == 0u) {
          return;
        }

        const auto& transform = world.get<components::TransformComponent>(entity);
        const glm::mat4 world_transform = makeTransform(transform.getPosition(),
                                                        transform.getRotation(),
                                                        transform.getScale());
        NavConvexVolume out;
        out.vertices.reserve(volume.vertices.size());
        float min_y = std::numeric_limits<float>::max();
        float max_y = -std::numeric_limits<float>::max();
        for (const math::Vec3& vertex : volume.vertices) {
          const glm::vec4 world = world_transform * glm::vec4(vertex.x, vertex.y, vertex.z, 1.0f);
          out.vertices.push_back(math::fromGlm(glm::vec3(world)));
          min_y = std::min(min_y, out.vertices.back().y);
          max_y = std::max(max_y, out.vertices.back().y);
        }
        out.min_y = min_y + volume.min_y;
        out.max_y = max_y + volume.max_y;
        out.area = volume.area;
        geometry.convex_volumes.push_back(std::move(out));
      });
}

}  // namespace

void appendGeometry(NavMeshInputGeometry& out,
                    const geometry::MeshData& mesh,
                    const math::Vec3& position,
                    const math::Quat& rotation,
                    const math::Vec3& scale,
                    unsigned char area) {
  appendMesh(out, mesh, makeTransform(position, rotation, scale), area);
}

NavMeshInputGeometry collectNavMeshGeometry(const ecs::World& world, uint32_t source_mask) {
  NavMeshInputGeometry geometry;
  bool has_explicit_surfaces = false;
  world.forEach<components::NavMeshSurfaceComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
        const auto& surface = world.get<components::NavMeshSurfaceComponent>(entity);
        if (!surface.enabled || (surface.layer_mask & source_mask) == 0u) {
          return;
        }
        has_explicit_surfaces = true;

        const auto& transform = world.get<components::TransformComponent>(entity);
        const glm::mat4 world_transform = makeTransform(transform.getPosition(),
                                                        transform.getRotation(),
                                                        transform.getScale());
        const unsigned char area = surface.walkable ? surface.area : kNavAreaNull;
        if (surface.mesh_data) {
          appendMesh(geometry, *surface.mesh_data, world_transform, area);
          return;
        }

      });

  if (has_explicit_surfaces) {
    appendOffMeshLinks(geometry, world, source_mask);
    appendConvexVolumes(geometry, world, source_mask);
    return geometry;
  }

  world.forEach<components::ColliderComponent, components::MeshComponent, components::TransformComponent>(
      [&](const ecs::Entity entity) {
        const auto& collider = world.get<components::ColliderComponent>(entity);
        const auto* mesh_shape = std::get_if<components::MeshColliderShape>(&collider.shape);
        if (mesh_shape == nullptr) {
          return;
        }
        if (mesh_shape->vertices.empty() || mesh_shape->indices.empty()) {
          return;
        }

        const auto& transform = world.get<components::TransformComponent>(entity);
        const glm::mat4 world_transform = makeTransform(transform.getPosition(),
                                                        transform.getRotation(),
                                                        transform.getScale());
        appendMesh(geometry, *mesh_shape, world_transform, kNavAreaDefault);
      });
  appendOffMeshLinks(geometry, world, source_mask);
  appendConvexVolumes(geometry, world, source_mask);
  return geometry;
}

}  // namespace karma::navigation
