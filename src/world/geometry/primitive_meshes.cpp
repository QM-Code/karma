#include "karma/world.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

namespace karma::world {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinExtent = 0.0f;

float sanitizedLength(float value) {
  return std::max(value, kMinExtent);
}

uint32_t sanitizedSegments(uint32_t segments) {
  return std::max(segments, 3u);
}

uint32_t sanitizedRings(uint32_t rings) {
  return std::max(rings, 2u);
}

glm::vec3 toPrimitiveGlm(const math::Vec3& value) {
  return {value.x, value.y, value.z};
}

glm::vec3 safeNormalize(const glm::vec3& value, const glm::vec3& fallback) {
  const float len_sq = glm::dot(value, value);
  if (len_sq <= 1.0e-10f) {
    return fallback;
  }
  return value * glm::inversesqrt(len_sq);
}

void addDefaultMaterialSlot(MeshData& mesh, std::string material_key) {
  if (material_key.empty()) {
    return;
  }
  mesh.material_slots.push_back(MeshMaterialSlot{
      .name = "default",
      .default_material_key = std::move(material_key),
  });
  if (!mesh.indices.empty()) {
    mesh.submeshes.push_back(MeshSubmesh{
        .index_offset = 0u,
        .index_count = static_cast<uint32_t>(mesh.indices.size()),
        .material_slot = 0u,
    });
  }
}

void appendVertex(MeshData& mesh,
                  const glm::vec3& position,
                  const glm::vec3& normal,
                  const glm::vec2& uv,
                  const glm::vec3& tangent) {
  mesh.vertices.push_back(position);
  mesh.normals.push_back(safeNormalize(normal, {0.0f, 1.0f, 0.0f}));
  mesh.uvs.push_back(uv);
  mesh.tangents.push_back(glm::vec4(safeNormalize(tangent, {1.0f, 0.0f, 0.0f}), 1.0f));
}

void appendQuad(MeshData& mesh,
                const glm::vec3& a,
                const glm::vec3& b,
                const glm::vec3& c,
                const glm::vec3& d,
                const glm::vec3& normal) {
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  const glm::vec3 tangent = safeNormalize(b - a, {1.0f, 0.0f, 0.0f});
  appendVertex(mesh, a, normal, {0.0f, 0.0f}, tangent);
  appendVertex(mesh, b, normal, {1.0f, 0.0f}, tangent);
  appendVertex(mesh, c, normal, {1.0f, 1.0f}, tangent);
  appendVertex(mesh, d, normal, {0.0f, 1.0f}, tangent);
  mesh.indices.insert(mesh.indices.end(), {base, base + 2u, base + 1u,
                                           base, base + 3u, base + 2u});
}

void appendSurfaceRows(MeshData& mesh, uint32_t row_count, uint32_t segments) {
  const uint32_t stride = segments + 1u;
  for (uint32_t row = 0u; row + 1u < row_count; ++row) {
    for (uint32_t segment = 0u; segment < segments; ++segment) {
      const uint32_t a = row * stride + segment;
      const uint32_t b = (row + 1u) * stride + segment;
      const uint32_t c = b + 1u;
      const uint32_t d = a + 1u;
      mesh.indices.insert(mesh.indices.end(), {a, d, b, d, c, b});
    }
  }
}

}  // namespace

MeshData createPlaneMesh(float width, float depth, std::string material_key) {
  const float hx = sanitizedLength(width) * 0.5f;
  const float hz = sanitizedLength(depth) * 0.5f;
  MeshData mesh{};
  appendQuad(mesh,
             {-hx, 0.0f, -hz},
             {hx, 0.0f, -hz},
             {hx, 0.0f, hz},
             {-hx, 0.0f, hz},
             {0.0f, 1.0f, 0.0f});
  addDefaultMaterialSlot(mesh, std::move(material_key));
  return mesh;
}

MeshData createBoxMesh(const math::Vec3& half_extents, std::string material_key) {
  const glm::vec3 half = glm::abs(toPrimitiveGlm(half_extents));
  const glm::vec3 min = -half;
  const glm::vec3 max = half;
  MeshData mesh{};

  appendQuad(mesh, {min.x, max.y, min.z}, {max.x, max.y, min.z},
             {max.x, max.y, max.z}, {min.x, max.y, max.z}, {0.0f, 1.0f, 0.0f});
  appendQuad(mesh, {min.x, min.y, max.z}, {max.x, min.y, max.z},
             {max.x, min.y, min.z}, {min.x, min.y, min.z}, {0.0f, -1.0f, 0.0f});
  appendQuad(mesh, {min.x, min.y, min.z}, {max.x, min.y, min.z},
             {max.x, max.y, min.z}, {min.x, max.y, min.z}, {0.0f, 0.0f, -1.0f});
  appendQuad(mesh, {max.x, min.y, max.z}, {min.x, min.y, max.z},
             {min.x, max.y, max.z}, {max.x, max.y, max.z}, {0.0f, 0.0f, 1.0f});
  appendQuad(mesh, {min.x, min.y, max.z}, {min.x, min.y, min.z},
             {min.x, max.y, min.z}, {min.x, max.y, max.z}, {-1.0f, 0.0f, 0.0f});
  appendQuad(mesh, {max.x, min.y, min.z}, {max.x, min.y, max.z},
             {max.x, max.y, max.z}, {max.x, max.y, min.z}, {1.0f, 0.0f, 0.0f});

  addDefaultMaterialSlot(mesh, std::move(material_key));
  return mesh;
}

MeshData createCubeMesh(float size, std::string material_key) {
  const float half = sanitizedLength(size) * 0.5f;
  return createBoxMesh({half, half, half}, std::move(material_key));
}

MeshData createSphereMesh(const SphereMeshDesc& desc) {
  const float radius = sanitizedLength(desc.radius);
  const uint32_t segments = sanitizedSegments(desc.segments);
  const uint32_t rings = sanitizedRings(desc.rings);
  const uint32_t stride = segments + 1u;
  MeshData mesh{};
  mesh.vertices.reserve(static_cast<size_t>(rings + 1u) * stride);
  mesh.normals.reserve(mesh.vertices.capacity());
  mesh.uvs.reserve(mesh.vertices.capacity());
  mesh.tangents.reserve(mesh.vertices.capacity());

  for (uint32_t ring = 0u; ring <= rings; ++ring) {
    const float v = static_cast<float>(ring) / static_cast<float>(rings);
    const float theta = v * kPi;
    const float sin_theta = std::sin(theta);
    const float cos_theta = std::cos(theta);
    for (uint32_t segment = 0u; segment <= segments; ++segment) {
      const float u = static_cast<float>(segment) / static_cast<float>(segments);
      const float phi = u * kPi * 2.0f;
      const float sin_phi = std::sin(phi);
      const float cos_phi = std::cos(phi);
      const glm::vec3 normal{sin_theta * cos_phi, cos_theta, sin_theta * sin_phi};
      const glm::vec3 tangent{-sin_phi, 0.0f, cos_phi};
      appendVertex(mesh, normal * radius, normal, {u, v}, tangent);
    }
  }

  appendSurfaceRows(mesh, rings + 1u, segments);
  addDefaultMaterialSlot(mesh, desc.material_key);
  return mesh;
}

MeshData createSphereMesh(float radius,
                          uint32_t segments,
                          uint32_t rings,
                          std::string material_key) {
  return createSphereMesh(SphereMeshDesc{
      .radius = radius,
      .segments = segments,
      .rings = rings,
      .material_key = std::move(material_key),
  });
}

MeshData createCapsuleMesh(const CapsuleMeshDesc& desc) {
  const float radius = sanitizedLength(desc.radius);
  const float cylinder_height = sanitizedLength(desc.cylinder_height);
  if (cylinder_height <= 1.0e-6f) {
    return createSphereMesh(radius,
                            desc.segments,
                            desc.hemisphere_rings * 2u,
                            desc.material_key);
  }

  const uint32_t segments = sanitizedSegments(desc.segments);
  const uint32_t hemisphere_rings = sanitizedRings(desc.hemisphere_rings);
  const uint32_t stride = segments + 1u;
  const float half_cylinder = cylinder_height * 0.5f;

  struct Row {
    float y = 0.0f;
    float radial = 0.0f;
    float normal_y = 1.0f;
  };

  std::vector<Row> rows;
  rows.reserve(static_cast<size_t>(hemisphere_rings) * 2u + 2u);
  for (uint32_t ring = 0u; ring <= hemisphere_rings; ++ring) {
    const float t = static_cast<float>(ring) / static_cast<float>(hemisphere_rings);
    const float theta = t * kPi * 0.5f;
    rows.push_back(Row{
        .y = half_cylinder + radius * std::cos(theta),
        .radial = radius * std::sin(theta),
        .normal_y = std::cos(theta),
    });
  }
  rows.push_back(Row{
      .y = -half_cylinder,
      .radial = radius,
      .normal_y = 0.0f,
  });
  for (uint32_t ring = 1u; ring <= hemisphere_rings; ++ring) {
    const float t = static_cast<float>(ring) / static_cast<float>(hemisphere_rings);
    const float theta = kPi * 0.5f + t * kPi * 0.5f;
    rows.push_back(Row{
        .y = -half_cylinder + radius * std::cos(theta),
        .radial = radius * std::sin(theta),
        .normal_y = std::cos(theta),
    });
  }

  MeshData mesh{};
  mesh.vertices.reserve(rows.size() * stride);
  mesh.normals.reserve(mesh.vertices.capacity());
  mesh.uvs.reserve(mesh.vertices.capacity());
  mesh.tangents.reserve(mesh.vertices.capacity());

  const float row_denominator = static_cast<float>(std::max<size_t>(rows.size() - 1u, 1u));
  for (size_t row_index = 0u; row_index < rows.size(); ++row_index) {
    const Row& row = rows[row_index];
    const float v = static_cast<float>(row_index) / row_denominator;
    const float normal_xz = radius > 0.0f ? row.radial / radius : 0.0f;
    for (uint32_t segment = 0u; segment <= segments; ++segment) {
      const float u = static_cast<float>(segment) / static_cast<float>(segments);
      const float phi = u * kPi * 2.0f;
      const float sin_phi = std::sin(phi);
      const float cos_phi = std::cos(phi);
      const glm::vec3 position{row.radial * cos_phi, row.y, row.radial * sin_phi};
      const glm::vec3 normal = safeNormalize(
          {normal_xz * cos_phi, row.normal_y, normal_xz * sin_phi},
          row.normal_y >= 0.0f ? glm::vec3{0.0f, 1.0f, 0.0f}
                               : glm::vec3{0.0f, -1.0f, 0.0f});
      const glm::vec3 tangent{-sin_phi, 0.0f, cos_phi};
      appendVertex(mesh, position, normal, {u, v}, tangent);
    }
  }

  appendSurfaceRows(mesh, static_cast<uint32_t>(rows.size()), segments);
  addDefaultMaterialSlot(mesh, desc.material_key);
  return mesh;
}

MeshData createCapsuleMesh(float radius,
                           float cylinder_height,
                           uint32_t segments,
                           uint32_t hemisphere_rings,
                           std::string material_key) {
  return createCapsuleMesh(CapsuleMeshDesc{
      .radius = radius,
      .cylinder_height = cylinder_height,
      .segments = segments,
      .hemisphere_rings = hemisphere_rings,
      .material_key = std::move(material_key),
  });
}

}  // namespace karma::world
