#include "navigation_example_scene.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <imgui.h>

#include "karma/simulation/navigation/nav_geometry.h"
#include "karma/simulation/navigation/nav_types.h"

namespace karma::demo::navigation_examples {
namespace {

void appendVertex(geometry::MeshData& mesh,
                  const math::Vec3& position,
                  const math::Vec3& normal = {0.0f, 1.0f, 0.0f}) {
  mesh.vertices.push_back({position.x, position.y, position.z});
  mesh.normals.push_back({normal.x, normal.y, normal.z});
  mesh.uvs.push_back({});
  mesh.tangents.push_back({1.0f, 0.0f, 0.0f, 1.0f});
}

void appendQuadMesh(geometry::MeshData& mesh,
                    const math::Vec3& a,
                    const math::Vec3& b,
                    const math::Vec3& c,
                    const math::Vec3& d) {
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  appendVertex(mesh, a);
  appendVertex(mesh, b);
  appendVertex(mesh, c);
  appendVertex(mesh, d);
  mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
}

geometry::MeshData makeQuadMesh(const math::Vec3& a,
                                const math::Vec3& b,
                                const math::Vec3& c,
                                const math::Vec3& d) {
  geometry::MeshData mesh;
  appendQuadMesh(mesh, a, b, c, d);
  return mesh;
}

}  // namespace

SurfaceBuild makeOpenSurface(float half_extent) {
  SurfaceBuild out;
  appendQuadMesh(out.mesh,
                 {-half_extent, 0.0f, -half_extent},
                 { half_extent, 0.0f, -half_extent},
                 { half_extent, 0.0f,  half_extent},
                 {-half_extent, 0.0f,  half_extent});
  navigation::appendGeometry(out.geometry, out.mesh);
  return out;
}

SurfaceBuild makeRingSurface() {
  SurfaceBuild out;
  const std::array<geometry::MeshData, 4> quads = {
      makeQuadMesh({-8.0f, 0.0f, -8.0f}, {-1.4f, 0.0f, -8.0f}, {-1.4f, 0.0f, 8.0f}, {-8.0f, 0.0f, 8.0f}),
      makeQuadMesh({ 1.4f, 0.0f, -8.0f}, { 8.0f, 0.0f, -8.0f}, { 8.0f, 0.0f, 8.0f}, { 1.4f, 0.0f, 8.0f}),
      makeQuadMesh({-1.4f, 0.0f, -8.0f}, { 1.4f, 0.0f, -8.0f}, { 1.4f, 0.0f, -1.4f}, {-1.4f, 0.0f, -1.4f}),
      makeQuadMesh({-1.4f, 0.0f,  1.4f}, { 1.4f, 0.0f,  1.4f}, { 1.4f, 0.0f, 8.0f}, {-1.4f, 0.0f, 8.0f}),
  };
  for (const geometry::MeshData& quad : quads) {
    const uint32_t base = static_cast<uint32_t>(out.mesh.vertices.size());
    out.mesh.vertices.insert(out.mesh.vertices.end(), quad.vertices.begin(), quad.vertices.end());
    out.mesh.normals.insert(out.mesh.normals.end(), quad.normals.begin(), quad.normals.end());
    out.mesh.uvs.insert(out.mesh.uvs.end(), quad.uvs.begin(), quad.uvs.end());
    out.mesh.tangents.insert(out.mesh.tangents.end(), quad.tangents.begin(), quad.tangents.end());
    for (uint32_t index : quad.indices) {
      out.mesh.indices.push_back(base + index);
    }
    navigation::appendGeometry(out.geometry, quad);
  }
  return out;
}

SurfaceBuild makeOffMeshSurface() {
  SurfaceBuild out;
  const geometry::MeshData left =
      makeQuadMesh({-8.0f, 0.0f, -4.0f}, {-1.5f, 0.0f, -4.0f}, {-1.5f, 0.0f, 4.0f}, {-8.0f, 0.0f, 4.0f});
  const geometry::MeshData right =
      makeQuadMesh({1.5f, 0.0f, -4.0f}, {8.0f, 0.0f, -4.0f}, {8.0f, 0.0f, 4.0f}, {1.5f, 0.0f, 4.0f});
  const geometry::MeshData water =
      makeQuadMesh({-1.5f, -0.03f, -4.0f}, {1.5f, -0.03f, -4.0f}, {1.5f, -0.03f, 4.0f}, {-1.5f, -0.03f, 4.0f});
  for (const auto* mesh : {&left, &right, &water}) {
    const uint32_t base = static_cast<uint32_t>(out.mesh.vertices.size());
    out.mesh.vertices.insert(out.mesh.vertices.end(), mesh->vertices.begin(), mesh->vertices.end());
    out.mesh.normals.insert(out.mesh.normals.end(), mesh->normals.begin(), mesh->normals.end());
    out.mesh.uvs.insert(out.mesh.uvs.end(), mesh->uvs.begin(), mesh->uvs.end());
    out.mesh.tangents.insert(out.mesh.tangents.end(), mesh->tangents.begin(), mesh->tangents.end());
    for (uint32_t index : mesh->indices) {
      out.mesh.indices.push_back(base + index);
    }
  }
  navigation::appendGeometry(out.geometry, left);
  navigation::appendGeometry(out.geometry, right);
  navigation::appendGeometry(out.geometry, water, {}, {}, {1.0f, 1.0f, 1.0f}, kAreaWater);
  out.geometry.off_mesh_connections.push_back({
      .start = {-1.65f, 0.0f, 0.0f},
      .end = {1.65f, 0.0f, 0.0f},
      .radius = 0.55f,
      .area = kAreaDoor,
      .flags = static_cast<uint16_t>(navigation::kNavPolyFlagWalk | navigation::kNavPolyFlagOffMesh | kFlagDoor),
      .bidirectional = true,
      .user_id = 42,
  });
  out.geometry.convex_volumes.push_back({
      .vertices = {{-7.0f, 0.0f, -3.0f}, {-4.5f, 0.0f, -3.0f}, {-4.5f, 0.0f, 3.0f}, {-7.0f, 0.0f, 3.0f}},
      .min_y = -1.0f,
      .max_y = 1.0f,
      .area = kAreaDoor,
  });
  return out;
}

Bounds computeBounds(const navigation::NavMeshInputGeometry& geometry) {
  Bounds bounds;
  if (geometry.vertices.empty()) {
    return bounds;
  }
  bounds.min = geometry.vertices.front();
  bounds.max = geometry.vertices.front();
  for (const math::Vec3& vertex : geometry.vertices) {
    bounds.min.x = std::min(bounds.min.x, vertex.x);
    bounds.min.y = std::min(bounds.min.y, vertex.y);
    bounds.min.z = std::min(bounds.min.z, vertex.z);
    bounds.max.x = std::max(bounds.max.x, vertex.x);
    bounds.max.y = std::max(bounds.max.y, vertex.y);
    bounds.max.z = std::max(bounds.max.z, vertex.z);
  }
  return bounds;
}

math::Vec3 midpoint(const math::Vec3& a, const math::Vec3& b) {
  return {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f};
}

float distance2D(const math::Vec3& a, const math::Vec3& b) {
  const float dx = a.x - b.x;
  const float dz = a.z - b.z;
  return std::sqrt(dx * dx + dz * dz);
}

navigation::NavMeshBuildConfig defaultBuildConfig(ExampleKind kind) {
  navigation::NavMeshBuildConfig config;
  config.build_mode = kind == ExampleKind::TileCache || kind == ExampleKind::OffMeshAreas
      ? navigation::NavMeshBuildMode::Tiled
      : navigation::NavMeshBuildMode::Solo;
  config.cell_size = 0.25f;
  config.cell_height = 0.12f;
  config.agent_height = 1.7f;
  config.agent_radius = 0.28f;
  config.agent_max_climb = 0.6f;
  config.agent_max_slope_degrees = 45.0f;
  config.region_min_size = 4.0f;
  config.region_merge_size = 12.0f;
  config.tile_size = 32;
  config.collect_build_debug_draw = true;
  config.area_configs = {
      {.area = navigation::kNavAreaDefault, .flags = navigation::kNavPolyFlagWalk, .cost = 1.0f},
      {.area = kAreaWater, .flags = kFlagWater, .cost = 4.0f},
      {.area = kAreaDoor, .flags = static_cast<uint16_t>(navigation::kNavPolyFlagWalk | kFlagDoor), .cost = 1.2f},
  };
  return config;
}

bool imguiCapturesMouse() {
  return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
}

}  // namespace karma::demo::navigation_examples
