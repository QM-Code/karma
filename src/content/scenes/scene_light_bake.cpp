#include "scene_light_bake.h"

#include "scene_runtime_assets.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace karma::scenes::detail {
namespace {

constexpr uint32_t kCpuAtlasHardLimit = 512u;
constexpr uint32_t kCpuSkySampleHardLimit = 64u;
constexpr uint32_t kCpuDilationHardLimit = 64u;
constexpr size_t kCpuLightHardLimit = 64u;
constexpr size_t kCpuTargetHardLimit = 4096u;
constexpr size_t kCpuTriangleHardLimit = 1'000'000u;
constexpr uint64_t kCpuTotalTexelHardLimit = 16u * 1024u * 1024u;
constexpr float kRayEpsilon = 1.0e-3f;
constexpr float kPi = 3.14159265358979323846f;

struct WorldTriangle {
  glm::vec3 a{};
  glm::vec3 b{};
  glm::vec3 c{};
  uint64_t owner_key = 0u;
  uint32_t triangle_index = 0u;
};

struct TriangleBvhNode {
  glm::vec3 minimum{};
  glm::vec3 maximum{};
  uint32_t first = 0u;
  uint32_t count = 0u;
  uint32_t left = 0u;
  uint32_t right = 0u;
  uint8_t split_axis = 0u;
};

struct BakeTarget {
  std::string owner_id;
  world::Entity entity{};
  const world::MeshData* source_mesh = nullptr;
  glm::mat4 transform{1.0f};
  glm::mat3 normal_transform{1.0f};
};

struct BakeLight {
  components::LightComponent component{};
  glm::vec3 position{};
  glm::vec3 direction{0.0f, -1.0f, 0.0f};
  uint64_t mixed_bit = 0u;
};

struct EvaluatedLight {
  glm::vec3 irradiance{};
  glm::vec3 direction{};
};

struct DerivedMesh {
  world::MeshData mesh;
  bool generated_uv1 = false;
};

uint64_t entityKey(world::Entity entity) {
  return (static_cast<uint64_t>(entity.index) << 32u) |
         static_cast<uint64_t>(entity.generation);
}

glm::mat4 worldMatrix(const components::TransformComponent& transform) {
  glm::mat4 matrix = glm::translate(
      glm::mat4(1.0f), math::toGlm(transform.worldPosition()));
  matrix *= glm::mat4_cast(math::toGlm(transform.worldRotation()));
  return glm::scale(matrix, math::toGlm(transform.worldScale()));
}

glm::mat4 instanceMatrix(const components::MeshInstance& instance) {
  glm::mat4 matrix =
      glm::translate(glm::mat4(1.0f), math::toGlm(instance.position));
  matrix *= glm::mat4_cast(math::toGlm(instance.rotation));
  return glm::scale(matrix, math::toGlm(instance.scale));
}

glm::mat4 instanceMatrix(const components::PlanarMeshInstance& instance) {
  glm::mat4 matrix =
      glm::translate(glm::mat4(1.0f), math::toGlm(instance.position));
  matrix = glm::rotate(
      matrix, instance.yaw_radians, glm::vec3(0.0f, 1.0f, 0.0f));
  return glm::scale(matrix, math::toGlm(instance.scale));
}

size_t authoredInstanceCount(
    const components::InstancedMeshComponent& component) {
  switch (component.gpu_layout) {
    case rendering::InstanceGpuLayout::Matrix4x4Params:
      return component.instances.size();
    case rendering::InstanceGpuLayout::PositionYawScaleParams:
      return component.planar_instances.size();
  }
  return 0u;
}

glm::vec3 transformPoint(const glm::mat4& matrix, const glm::vec3& point) {
  return glm::vec3(matrix * glm::vec4(point, 1.0f));
}

bool finiteVec2(const glm::vec2& value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

float orient2d(const glm::vec2& a,
               const glm::vec2& b,
               const glm::vec2& c) {
  return (b.x - a.x) * (c.y - a.y) -
         (b.y - a.y) * (c.x - a.x);
}

bool strictlyInsideTriangle(const glm::vec2& point,
                            const glm::vec2& a,
                            const glm::vec2& b,
                            const glm::vec2& c) {
  constexpr float epsilon = 1.0e-6f;
  const float ab = orient2d(a, b, point);
  const float bc = orient2d(b, c, point);
  const float ca = orient2d(c, a, point);
  return (ab > epsilon && bc > epsilon && ca > epsilon) ||
         (ab < -epsilon && bc < -epsilon && ca < -epsilon);
}

bool properSegmentIntersection(const glm::vec2& a,
                               const glm::vec2& b,
                               const glm::vec2& c,
                               const glm::vec2& d) {
  constexpr float epsilon = 1.0e-6f;
  const float ab_c = orient2d(a, b, c);
  const float ab_d = orient2d(a, b, d);
  const float cd_a = orient2d(c, d, a);
  const float cd_b = orient2d(c, d, b);
  return ((ab_c > epsilon && ab_d < -epsilon) ||
          (ab_c < -epsilon && ab_d > epsilon)) &&
         ((cd_a > epsilon && cd_b < -epsilon) ||
          (cd_a < -epsilon && cd_b > epsilon));
}

bool uvTrianglesOverlap(const std::array<glm::vec2, 3>& lhs,
                        const std::array<glm::vec2, 3>& rhs) {
  for (size_t a = 0u; a < 3u; ++a) {
    for (size_t b = 0u; b < 3u; ++b) {
      if (properSegmentIntersection(lhs[a], lhs[(a + 1u) % 3u],
                                    rhs[b], rhs[(b + 1u) % 3u])) {
        return true;
      }
    }
  }
  const glm::vec2 lhs_center = (lhs[0] + lhs[1] + lhs[2]) / 3.0f;
  const glm::vec2 rhs_center = (rhs[0] + rhs[1] + rhs[2]) / 3.0f;
  return strictlyInsideTriangle(lhs_center, rhs[0], rhs[1], rhs[2]) ||
         strictlyInsideTriangle(rhs_center, lhs[0], lhs[1], lhs[2]);
}

bool hasUsableUv1(const world::MeshData& mesh) {
  if (mesh.uvs1.size() != mesh.vertices.size() ||
      mesh.indices.size() < 3u || mesh.indices.size() % 3u != 0u) {
    return false;
  }
  for (const glm::vec2& uv : mesh.uvs1) {
    if (!finiteVec2(uv) || uv.x < 0.0f || uv.x > 1.0f ||
        uv.y < 0.0f || uv.y > 1.0f) {
      return false;
    }
  }
  const size_t triangle_count = mesh.indices.size() / 3u;
  if (triangle_count > 2048u) {
    return false;
  }
  std::vector<std::array<glm::vec2, 3>> triangles;
  triangles.reserve(triangle_count);
  for (size_t triangle = 0u; triangle < triangle_count; ++triangle) {
    std::array<glm::vec2, 3> uvs{};
    for (size_t corner = 0u; corner < 3u; ++corner) {
      const uint32_t index = mesh.indices[triangle * 3u + corner];
      if (index >= mesh.uvs1.size()) return false;
      uvs[corner] = mesh.uvs1[index];
    }
    if (std::abs(orient2d(uvs[0], uvs[1], uvs[2])) <= 1.0e-8f) {
      return false;
    }
    triangles.push_back(uvs);
  }
  for (size_t lhs = 0u; lhs < triangles.size(); ++lhs) {
    for (size_t rhs = lhs + 1u; rhs < triangles.size(); ++rhs) {
      if (uvTrianglesOverlap(triangles[lhs], triangles[rhs])) {
        return false;
      }
    }
  }
  return true;
}

template <typename T>
void appendVertexAttribute(const std::vector<T>& source,
                           size_t vertex_count,
                           uint32_t index,
                           std::vector<T>& destination) {
  if (source.size() == vertex_count) {
    destination.push_back(source[index]);
  }
}

DerivedMesh generateUv1Mesh(const world::MeshData& source) {
  DerivedMesh derived{};
  derived.generated_uv1 = true;
  const size_t triangle_count = source.indices.size() / 3u;
  if (triangle_count == 0u) return derived;
  const uint32_t columns = static_cast<uint32_t>(
      std::ceil(std::sqrt(static_cast<double>(triangle_count))));
  const uint32_t rows = static_cast<uint32_t>(
      (triangle_count + columns - 1u) / columns);
  const float cell_width = 1.0f / static_cast<float>(columns);
  const float cell_height = 1.0f / static_cast<float>(rows);
  constexpr std::array<glm::vec2, 3> kCellUv{
      glm::vec2{0.12f, 0.12f},
      glm::vec2{0.88f, 0.12f},
      glm::vec2{0.12f, 0.88f},
  };

  const size_t vertex_count = source.vertices.size();
  derived.mesh.vertices.reserve(triangle_count * 3u);
  derived.mesh.indices.reserve(triangle_count * 3u);
  derived.mesh.uvs1.reserve(triangle_count * 3u);
  derived.mesh.morph_targets.resize(source.morph_targets.size());
  for (size_t triangle = 0u; triangle < triangle_count; ++triangle) {
    const uint32_t column = static_cast<uint32_t>(triangle) % columns;
    const uint32_t row = static_cast<uint32_t>(triangle) / columns;
    for (size_t corner = 0u; corner < 3u; ++corner) {
      const uint32_t source_index = source.indices[triangle * 3u + corner];
      if (source_index >= vertex_count) {
        return {};
      }
      const uint32_t destination_index =
          static_cast<uint32_t>(derived.mesh.vertices.size());
      derived.mesh.vertices.push_back(source.vertices[source_index]);
      appendVertexAttribute(source.normals,
                            vertex_count,
                            source_index,
                            derived.mesh.normals);
      appendVertexAttribute(source.uvs,
                            vertex_count,
                            source_index,
                            derived.mesh.uvs);
      appendVertexAttribute(source.tangents,
                            vertex_count,
                            source_index,
                            derived.mesh.tangents);
      appendVertexAttribute(source.joint_indices,
                            vertex_count,
                            source_index,
                            derived.mesh.joint_indices);
      appendVertexAttribute(source.joint_weights,
                            vertex_count,
                            source_index,
                            derived.mesh.joint_weights);
      for (size_t morph = 0u; morph < source.morph_targets.size(); ++morph) {
        const auto& source_target = source.morph_targets[morph];
        auto& target = derived.mesh.morph_targets[morph];
        appendVertexAttribute(source_target.position_deltas,
                              vertex_count,
                              source_index,
                              target.position_deltas);
        appendVertexAttribute(source_target.normal_deltas,
                              vertex_count,
                              source_index,
                              target.normal_deltas);
        appendVertexAttribute(source_target.tangent_deltas,
                              vertex_count,
                              source_index,
                              target.tangent_deltas);
      }
      const glm::vec2 cell = kCellUv[corner];
      derived.mesh.uvs1.push_back({
          (static_cast<float>(column) + cell.x) * cell_width,
          (static_cast<float>(row) + cell.y) * cell_height,
      });
      derived.mesh.indices.push_back(destination_index);
    }
  }
  derived.mesh.submeshes = source.submeshes;
  derived.mesh.material_slots = source.material_slots;
  return derived;
}

void applyGeneratedUv1Padding(world::MeshData& mesh,
                              uint32_t resolution,
                              uint32_t padding) {
  const size_t triangle_count = mesh.indices.size() / 3u;
  if (triangle_count == 0u) return;
  const uint32_t columns = static_cast<uint32_t>(
      std::ceil(std::sqrt(static_cast<double>(triangle_count))));
  const uint32_t rows = static_cast<uint32_t>(
      (triangle_count + columns - 1u) / columns);
  for (size_t triangle = 0u; triangle < triangle_count; ++triangle) {
    const uint32_t column = static_cast<uint32_t>(triangle) % columns;
    const uint32_t row = static_cast<uint32_t>(triangle) / columns;
    const uint32_t x0 = static_cast<uint32_t>(
        static_cast<uint64_t>(column) * resolution / columns);
    const uint32_t x1 = static_cast<uint32_t>(
        static_cast<uint64_t>(column + 1u) * resolution / columns);
    const uint32_t y0 = static_cast<uint32_t>(
        static_cast<uint64_t>(row) * resolution / rows);
    const uint32_t y1 = static_cast<uint32_t>(
        static_cast<uint64_t>(row + 1u) * resolution / rows);
    const float min_x = static_cast<float>(x0 + padding) + 0.5f;
    const float max_x = static_cast<float>(x1 - padding) - 0.5f;
    const float min_y = static_cast<float>(y0 + padding) + 0.5f;
    const float max_y = static_cast<float>(y1 - padding) - 0.5f;
    const glm::vec2 triangle_uvs[3]{
        {min_x / resolution, min_y / resolution},
        {max_x / resolution, min_y / resolution},
        {min_x / resolution, max_y / resolution},
    };
    for (size_t corner = 0u; corner < 3u; ++corner) {
      mesh.uvs1[mesh.indices[triangle * 3u + corner]] =
          triangle_uvs[corner];
    }
  }
}

uint32_t nextPowerOfTwo(uint32_t value) {
  value = std::max(value, 1u);
  --value;
  value |= value >> 1u;
  value |= value >> 2u;
  value |= value >> 4u;
  value |= value >> 8u;
  value |= value >> 16u;
  return value + 1u;
}

float worldSurfaceArea(const world::MeshData& mesh,
                       const glm::mat4& transform) {
  double area = 0.0;
  for (size_t index = 0u; index + 2u < mesh.indices.size(); index += 3u) {
    const uint32_t ia = mesh.indices[index];
    const uint32_t ib = mesh.indices[index + 1u];
    const uint32_t ic = mesh.indices[index + 2u];
    if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() ||
        ic >= mesh.vertices.size()) {
      continue;
    }
    const glm::vec3 a = transformPoint(transform, mesh.vertices[ia]);
    const glm::vec3 b = transformPoint(transform, mesh.vertices[ib]);
    const glm::vec3 c = transformPoint(transform, mesh.vertices[ic]);
    area += 0.5 * static_cast<double>(glm::length(glm::cross(b - a, c - a)));
  }
  return static_cast<float>(std::max(area, 0.0));
}

std::optional<uint32_t> atlasResolution(const world::MeshData& mesh,
                                        const glm::mat4& transform,
                                        const SceneLightmapBakeSettings& settings,
                                        bool generated_uv1) {
  const uint32_t hard_limit =
      std::min(settings.max_atlas_size, kCpuAtlasHardLimit);
  if (hard_limit < 4u) return std::nullopt;
  const float desired = std::sqrt(worldSurfaceArea(mesh, transform)) *
                        settings.texels_per_unit;
  uint32_t minimum = 8u;
  if (generated_uv1) {
    const uint64_t triangle_count = mesh.indices.size() / 3u;
    const uint64_t grid = static_cast<uint64_t>(
        std::ceil(std::sqrt(static_cast<double>(triangle_count))));
    const uint64_t cell_size =
        static_cast<uint64_t>(settings.padding) * 2u + 3u;
    const uint64_t required = grid * cell_size;
    if (required > hard_limit) return std::nullopt;
    minimum = std::max(minimum, static_cast<uint32_t>(required));
  }
  const double finite_desired = std::isfinite(desired)
                                    ? std::max<double>(desired, 1.0)
                                    : static_cast<double>(hard_limit);
  const uint32_t desired_resolution = static_cast<uint32_t>(
      std::min<double>(std::ceil(finite_desired), hard_limit));
  uint32_t resolution = nextPowerOfTwo(
      std::max(minimum, desired_resolution));
  if (resolution > hard_limit) {
    if (minimum > hard_limit) return std::nullopt;
    resolution = hard_limit;
  }
  return resolution;
}

bool rayTriangle(const glm::vec3& origin,
                 const glm::vec3& direction,
                 const WorldTriangle& triangle,
                 float max_distance) {
  constexpr float epsilon = 1.0e-7f;
  const glm::vec3 edge1 = triangle.b - triangle.a;
  const glm::vec3 edge2 = triangle.c - triangle.a;
  const glm::vec3 p = glm::cross(direction, edge2);
  const float determinant = glm::dot(edge1, p);
  if (std::abs(determinant) <= epsilon) return false;
  const float inverse = 1.0f / determinant;
  const glm::vec3 t = origin - triangle.a;
  const float u = glm::dot(t, p) * inverse;
  if (u < 0.0f || u > 1.0f) return false;
  const glm::vec3 q = glm::cross(t, edge1);
  const float v = glm::dot(direction, q) * inverse;
  if (v < 0.0f || u + v > 1.0f) return false;
  const float distance = glm::dot(edge2, q) * inverse;
  return distance > kRayEpsilon && distance < max_distance;
}

class TriangleBvh {
 public:
  TriangleBvh(const std::vector<WorldTriangle>& triangles,
              const std::function<bool()>& is_cancelled)
      : triangles_(triangles), is_cancelled_(&is_cancelled) {
    indices_.resize(triangles_.size());
    for (size_t index = 0u; index < indices_.size(); ++index) {
      indices_[index] = static_cast<uint32_t>(index);
    }
    if (!indices_.empty()) build(0u, static_cast<uint32_t>(indices_.size()));
  }

  bool empty() const { return nodes_.empty(); }
  bool cancelled() const { return cancelled_; }

  bool occluded(const glm::vec3& origin,
                const glm::vec3& direction,
                float max_distance,
                uint64_t owner_key,
                uint32_t source_triangle,
                SceneLightBakeStatistics& statistics) const {
    ++statistics.ray_queries;
    if (nodes_.empty()) return false;
    std::array<uint32_t, 128> stack{};
    size_t stack_size = 1u;
    stack[0] = 0u;
    while (stack_size != 0u) {
      const TriangleBvhNode& node = nodes_[stack[--stack_size]];
      ++statistics.bvh_node_visits;
      if (!intersectsBounds(origin,
                            direction,
                            max_distance,
                            node.minimum,
                            node.maximum)) {
        continue;
      }
      if (node.count != 0u) {
        for (uint32_t offset = 0u; offset < node.count; ++offset) {
          const WorldTriangle& triangle =
              triangles_[indices_[node.first + offset]];
          if (triangle.owner_key == owner_key &&
              triangle.triangle_index == source_triangle) {
            continue;
          }
          ++statistics.triangle_tests;
          if (rayTriangle(origin, direction, triangle, max_distance)) {
            return true;
          }
        }
        continue;
      }
      const bool forward = direction[node.split_axis] >= 0.0f;
      const uint32_t near_child = forward ? node.left : node.right;
      const uint32_t far_child = forward ? node.right : node.left;
      if (stack_size + 2u > stack.size()) return false;
      stack[stack_size++] = far_child;
      stack[stack_size++] = near_child;
    }
    return false;
  }

 private:
  static glm::vec3 triangleMinimum(const WorldTriangle& triangle) {
    return glm::min(triangle.a, glm::min(triangle.b, triangle.c));
  }

  static glm::vec3 triangleMaximum(const WorldTriangle& triangle) {
    return glm::max(triangle.a, glm::max(triangle.b, triangle.c));
  }

  static glm::vec3 centroid(const WorldTriangle& triangle) {
    return (triangle.a + triangle.b + triangle.c) / 3.0f;
  }

  static bool intersectsBounds(const glm::vec3& origin,
                               const glm::vec3& direction,
                               float max_distance,
                               const glm::vec3& minimum,
                               const glm::vec3& maximum) {
    float near_distance = 0.0f;
    float far_distance = max_distance;
    for (uint8_t axis = 0u; axis < 3u; ++axis) {
      if (std::abs(direction[axis]) <= 1.0e-12f) {
        if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) {
          return false;
        }
        continue;
      }
      const float inverse = 1.0f / direction[axis];
      float first = (minimum[axis] - origin[axis]) * inverse;
      float second = (maximum[axis] - origin[axis]) * inverse;
      if (first > second) std::swap(first, second);
      near_distance = std::max(near_distance, first);
      far_distance = std::min(far_distance, second);
      if (near_distance > far_distance) return false;
    }
    return far_distance > kRayEpsilon;
  }

  uint32_t build(uint32_t first, uint32_t count) {
    if (is_cancelled_ != nullptr && *is_cancelled_ &&
        (*is_cancelled_)()) {
      cancelled_ = true;
      return 0u;
    }
    const uint32_t node_index = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back({});
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    glm::vec3 centroid_minimum(std::numeric_limits<float>::max());
    glm::vec3 centroid_maximum(std::numeric_limits<float>::lowest());
    for (uint32_t offset = 0u; offset < count; ++offset) {
      const WorldTriangle& triangle = triangles_[indices_[first + offset]];
      minimum = glm::min(minimum, triangleMinimum(triangle));
      maximum = glm::max(maximum, triangleMaximum(triangle));
      const glm::vec3 center = centroid(triangle);
      centroid_minimum = glm::min(centroid_minimum, center);
      centroid_maximum = glm::max(centroid_maximum, center);
    }
    nodes_[node_index].minimum = minimum;
    nodes_[node_index].maximum = maximum;
    constexpr uint32_t kLeafSize = 4u;
    if (count <= kLeafSize) {
      nodes_[node_index].first = first;
      nodes_[node_index].count = count;
      return node_index;
    }

    const glm::vec3 extent = centroid_maximum - centroid_minimum;
    uint8_t axis = 0u;
    if (extent.y > extent.x) axis = 1u;
    if (extent.z > extent[axis]) axis = 2u;
    const uint32_t left_count = count / 2u;
    const auto begin = indices_.begin() + first;
    const auto middle = begin + left_count;
    const auto end = begin + count;
    std::nth_element(begin,
                     middle,
                     end,
                     [&](uint32_t lhs, uint32_t rhs) {
                       const float lhs_center = centroid(triangles_[lhs])[axis];
                       const float rhs_center = centroid(triangles_[rhs])[axis];
                       return lhs_center < rhs_center ||
                              (lhs_center == rhs_center && lhs < rhs);
                     });
    const uint32_t left = build(first, left_count);
    if (cancelled_) return node_index;
    const uint32_t right = build(first + left_count, count - left_count);
    if (cancelled_) return node_index;
    nodes_[node_index].left = left;
    nodes_[node_index].right = right;
    nodes_[node_index].split_axis = axis;
    return node_index;
  }

  const std::vector<WorldTriangle>& triangles_;
  const std::function<bool()>* is_cancelled_ = nullptr;
  std::vector<uint32_t> indices_;
  std::vector<TriangleBvhNode> nodes_;
  bool cancelled_ = false;
};

glm::vec3 hemisphereDirection(const glm::vec3& normal,
                              uint32_t index,
                              uint32_t count) {
  const float z = (static_cast<float>(index) + 0.5f) /
                  static_cast<float>(count);
  const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
  const float phi = 2.0f * kPi *
                    std::fmod(static_cast<float>(index) * 0.61803398875f,
                              1.0f);
  const glm::vec3 helper = std::abs(normal.y) < 0.95f
                               ? glm::vec3(0.0f, 1.0f, 0.0f)
                               : glm::vec3(1.0f, 0.0f, 0.0f);
  const glm::vec3 tangent = glm::normalize(glm::cross(helper, normal));
  const glm::vec3 bitangent = glm::cross(normal, tangent);
  return glm::normalize(tangent * (std::cos(phi) * radius) +
                        bitangent * (std::sin(phi) * radius) + normal * z);
}

float ambientOcclusion(const glm::vec3& position,
                       const glm::vec3& normal,
                       const TriangleBvh& occluders,
                       uint64_t owner_key,
                       uint32_t source_triangle,
                       const SceneLightmapBakeSettings& settings,
                       SceneLightBakeStatistics& statistics) {
  if (settings.ao_max_distance <= 0.0f || occluders.empty()) return 1.0f;
  const uint32_t samples = std::clamp(settings.sky_samples,
                                      1u,
                                      kCpuSkySampleHardLimit);
  uint32_t blocked = 0u;
  const glm::vec3 origin = position + normal * kRayEpsilon;
  for (uint32_t sample = 0u; sample < samples; ++sample) {
    if (occluders.occluded(origin,
                           hemisphereDirection(normal, sample, samples),
                           settings.ao_max_distance,
                           owner_key,
                           source_triangle,
                           statistics)) {
      ++blocked;
    }
  }
  return 1.0f - static_cast<float>(blocked) / static_cast<float>(samples);
}

EvaluatedLight evaluateLight(const BakeLight& light,
                             const glm::vec3& position,
                             const glm::vec3& normal,
                             const TriangleBvh& occluders,
                             uint64_t owner_key,
                             uint32_t source_triangle,
                             SceneLightBakeStatistics& statistics) {
  glm::vec3 to_light{};
  float attenuation = 1.0f;
  float max_distance = std::numeric_limits<float>::max();
  if (light.component.type == components::LightComponent::Type::Directional) {
    to_light = -light.direction;
  } else {
    const glm::vec3 delta = light.position - position;
    const float distance = glm::length(delta);
    if (distance <= kRayEpsilon || distance > light.component.range ||
        light.component.range <= 0.0f) {
      return {};
    }
    to_light = delta / distance;
    max_distance = distance - kRayEpsilon;
    const float normalized = std::clamp(distance / light.component.range,
                                        0.0f,
                                        1.0f);
    attenuation = (1.0f - normalized) * (1.0f - normalized);
    if (light.component.type == components::LightComponent::Type::Spot) {
      const float cone = glm::dot(light.direction, -to_light);
      const float inner = std::cos(glm::radians(
          std::clamp(light.component.inner_cone_degrees, 0.0f, 179.0f)));
      const float outer = std::cos(glm::radians(
          std::clamp(light.component.outer_cone_degrees, 0.0f, 179.0f)));
      const float high = std::max(inner, outer);
      const float low = std::min(inner, outer);
      if (cone <= low) return {};
      attenuation *= std::clamp((cone - low) /
                                    std::max(high - low, 1.0e-5f),
                                0.0f,
                                1.0f);
    }
  }
  const float diffuse = std::max(glm::dot(normal, to_light), 0.0f);
  if (diffuse <= 0.0f) return {};
  if (light.component.casts_shadows &&
      occluders.occluded(position + normal * kRayEpsilon,
                         to_light,
                         max_distance,
                         owner_key,
                         source_triangle,
                         statistics)) {
    return {};
  }
  return EvaluatedLight{
      .irradiance = glm::vec3(light.component.color.r,
                              light.component.color.g,
                              light.component.color.b) *
                    (light.component.intensity * attenuation * diffuse),
      .direction = to_light,
  };
}

void dilateImage(assets::Rgba8Image& image, uint32_t iterations) {
  if (iterations == 0u) return;
  iterations = std::min(iterations, kCpuDilationHardLimit);
  std::vector<uint8_t> next = image.pixels;
  for (uint32_t iteration = 0u; iteration < iterations; ++iteration) {
    next = image.pixels;
    bool changed = false;
    for (int y = 0; y < image.height; ++y) {
      for (int x = 0; x < image.width; ++x) {
        const size_t offset =
            (static_cast<size_t>(y) * image.width + x) * 4u;
        if (image.pixels[offset + 3u] != 0u) continue;
        constexpr std::array<std::array<int, 2>, 4> kNeighbors{{
            {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
        }};
        for (const auto& neighbor : kNeighbors) {
          const int nx = x + neighbor[0];
          const int ny = y + neighbor[1];
          if (nx < 0 || ny < 0 || nx >= image.width || ny >= image.height) {
            continue;
          }
          const size_t source =
              (static_cast<size_t>(ny) * image.width + nx) * 4u;
          if (image.pixels[source + 3u] == 0u) continue;
          std::copy_n(image.pixels.begin() +
                          static_cast<std::ptrdiff_t>(source),
                      4u,
                      next.begin() + static_cast<std::ptrdiff_t>(offset));
          changed = true;
          break;
        }
      }
    }
    image.pixels.swap(next);
    if (!changed) break;
  }
}

bool rasterize(const BakeTarget& target,
               const world::MeshData& mesh,
               uint32_t resolution,
               const std::vector<BakeLight>& lights,
               const TriangleBvh& occluders,
               float ambient_intensity,
               const SceneLightmapBakeSettings& settings,
               const SceneBakeExecutionOptions& execution,
               SceneBakeResult& result,
               assets::Rgba8Image& image,
               assets::Rgba8Image* direction_image,
               uint64_t& mixed_mask) {
  image.width = static_cast<int>(resolution);
  image.height = static_cast<int>(resolution);
  image.pixels.assign(static_cast<size_t>(resolution) * resolution * 4u, 0u);
  if (direction_image != nullptr) {
    direction_image->width = static_cast<int>(resolution);
    direction_image->height = static_cast<int>(resolution);
    direction_image->pixels.assign(
        static_cast<size_t>(resolution) * resolution * 4u, 0u);
  }
  mixed_mask = 0u;
  const uint64_t owner_key = entityKey(target.entity);

  for (size_t triangle = 0u; triangle < mesh.indices.size() / 3u; ++triangle) {
    if (execution.is_cancelled && execution.is_cancelled()) {
      result.cancelled = true;
      result.diagnostic = "scene bake cancelled during lighting rasterization";
      return false;
    }
    const uint32_t indices[3]{mesh.indices[triangle * 3u],
                              mesh.indices[triangle * 3u + 1u],
                              mesh.indices[triangle * 3u + 2u]};
    if (indices[0] >= mesh.vertices.size() ||
        indices[1] >= mesh.vertices.size() ||
        indices[2] >= mesh.vertices.size() ||
        indices[0] >= mesh.uvs1.size() || indices[1] >= mesh.uvs1.size() ||
        indices[2] >= mesh.uvs1.size()) {
      result.diagnostic = "derived lightmap mesh has invalid indices";
      return false;
    }
    const glm::vec2 uv[3]{mesh.uvs1[indices[0]],
                          mesh.uvs1[indices[1]],
                          mesh.uvs1[indices[2]]};
    const float area = orient2d(uv[0], uv[1], uv[2]);
    if (std::abs(area) <= 1.0e-8f) continue;
    const glm::vec3 positions[3]{
        transformPoint(target.transform, mesh.vertices[indices[0]]),
        transformPoint(target.transform, mesh.vertices[indices[1]]),
        transformPoint(target.transform, mesh.vertices[indices[2]]),
    };
    glm::vec3 face_normal = glm::cross(positions[1] - positions[0],
                                      positions[2] - positions[0]);
    if (glm::dot(face_normal, face_normal) <= 1.0e-12f) continue;
    face_normal = glm::normalize(face_normal);

    const float min_u = std::min({uv[0].x, uv[1].x, uv[2].x});
    const float max_u = std::max({uv[0].x, uv[1].x, uv[2].x});
    const float min_v = std::min({uv[0].y, uv[1].y, uv[2].y});
    const float max_v = std::max({uv[0].y, uv[1].y, uv[2].y});
    const int min_x = std::clamp(
        static_cast<int>(std::floor(min_u * resolution)),
        0,
        static_cast<int>(resolution) - 1);
    const int max_x = std::clamp(
        static_cast<int>(std::ceil(max_u * resolution)),
        0,
        static_cast<int>(resolution) - 1);
    const int min_y = std::clamp(
        static_cast<int>(std::floor(min_v * resolution)),
        0,
        static_cast<int>(resolution) - 1);
    const int max_y = std::clamp(
        static_cast<int>(std::ceil(max_v * resolution)),
        0,
        static_cast<int>(resolution) - 1);
    for (int y = min_y; y <= max_y; ++y) {
      if (execution.is_cancelled && execution.is_cancelled()) {
        result.cancelled = true;
        result.diagnostic =
            "scene bake cancelled during lighting rasterization";
        return false;
      }
      for (int x = min_x; x <= max_x; ++x) {
        const glm::vec2 sample{
            (static_cast<float>(x) + 0.5f) / resolution,
            (static_cast<float>(y) + 0.5f) / resolution,
        };
        const float w0 = orient2d(uv[1], uv[2], sample) / area;
        const float w1 = orient2d(uv[2], uv[0], sample) / area;
        const float w2 = 1.0f - w0 - w1;
        constexpr float epsilon = -1.0e-5f;
        if (w0 < epsilon || w1 < epsilon || w2 < epsilon) continue;
        const glm::vec3 position =
            positions[0] * w0 + positions[1] * w1 + positions[2] * w2;
        glm::vec3 normal = face_normal;
        if (mesh.normals.size() == mesh.vertices.size()) {
          normal = target.normal_transform *
                   (mesh.normals[indices[0]] * w0 +
                    mesh.normals[indices[1]] * w1 +
                    mesh.normals[indices[2]] * w2);
          const float length_squared = glm::dot(normal, normal);
          if (!std::isfinite(normal.x) || !std::isfinite(normal.y) ||
              !std::isfinite(normal.z) || length_squared <= 1.0e-12f) {
            normal = face_normal;
          } else {
            normal = glm::normalize(normal);
          }
        }
        const float ao = ambientOcclusion(position,
                                          normal,
                                          occluders,
                                          owner_key,
                                          static_cast<uint32_t>(triangle),
                                          settings,
                                          result.lighting_statistics);
        const float sky = 0.25f + 0.75f * std::max(normal.y, 0.0f);
        glm::vec3 irradiance(ambient_intensity * sky * ao);
        glm::vec3 weighted_direction{};
        float directional_strength = 0.0f;
        for (const BakeLight& light : lights) {
          const EvaluatedLight contribution = evaluateLight(
              light,
              position,
              normal,
              occluders,
              owner_key,
              static_cast<uint32_t>(triangle),
              result.lighting_statistics);
          irradiance += contribution.irradiance;
          const float strength = glm::dot(
              contribution.irradiance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
          weighted_direction += contribution.direction * strength;
          directional_strength += strength;
          if (light.mixed_bit != 0u &&
              glm::dot(contribution.irradiance,
                       contribution.irradiance) > 0.0f) {
            mixed_mask |= light.mixed_bit;
          }
        }
        irradiance = glm::clamp(irradiance, glm::vec3(0.0f), glm::vec3(1.0f));
        const size_t offset =
            (static_cast<size_t>(y) * resolution + x) * 4u;
        image.pixels[offset] = static_cast<uint8_t>(
            std::lround(irradiance.r * 255.0f));
        image.pixels[offset + 1u] = static_cast<uint8_t>(
            std::lround(irradiance.g * 255.0f));
        image.pixels[offset + 2u] = static_cast<uint8_t>(
            std::lround(irradiance.b * 255.0f));
        image.pixels[offset + 3u] = 255u;
        if (direction_image != nullptr) {
          glm::vec3 dominant = normal;
          if (glm::dot(weighted_direction, weighted_direction) > 1.0e-12f) {
            dominant = glm::normalize(weighted_direction);
          }
          const glm::vec3 encoded = dominant * 0.5f + 0.5f;
          direction_image->pixels[offset] = static_cast<uint8_t>(
              std::lround(std::clamp(encoded.x, 0.0f, 1.0f) * 255.0f));
          direction_image->pixels[offset + 1u] = static_cast<uint8_t>(
              std::lround(std::clamp(encoded.y, 0.0f, 1.0f) * 255.0f));
          direction_image->pixels[offset + 2u] = static_cast<uint8_t>(
              std::lround(std::clamp(encoded.z, 0.0f, 1.0f) * 255.0f));
          const float baked_luminance = glm::dot(
              irradiance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
          const float directional_share =
              directional_strength / std::max(baked_luminance, 1.0e-6f);
          direction_image->pixels[offset + 3u] = static_cast<uint8_t>(
              std::lround(std::clamp(directional_share, 0.0f, 1.0f) *
                          255.0f));
        }
      }
    }
  }
  dilateImage(image, settings.dilation);
  if (direction_image != nullptr) {
    dilateImage(*direction_image, settings.dilation);
  }
  return true;
}

std::string safeStem(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const char ch : value) {
    const unsigned char byte = static_cast<unsigned char>(ch);
    out.push_back(std::isalnum(byte) != 0 || ch == '-' || ch == '_'
                      ? ch
                      : '_');
  }
  return out.empty() ? std::string("static") : out;
}

std::optional<std::filesystem::path> artifactDirectory(
    const SceneBakeDesc& desc) {
  std::filesystem::path directory = desc.path.parent_path();
  if (directory.empty()) directory = "bakes";
  directory = (directory / "lightmaps").lexically_normal();
  if (!isPortableBakeArtifactPath(directory)) return std::nullopt;
  return directory;
}

std::vector<BakeTarget> collectTargets(const SceneInstantiateResult& instance,
                                       const world::World& world,
                                       const assets::AssetRegistry& assets,
                                       SceneBakeResult& result) {
  std::vector<std::pair<std::string, world::Entity>> owners(
      instance.navigation_owners_by_id.begin(),
      instance.navigation_owners_by_id.end());
  std::sort(owners.begin(), owners.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.first < rhs.first;
  });
  std::vector<BakeTarget> targets;
  for (const auto& [owner_id, entity] : owners) {
    if (!world.isAlive(entity) ||
        !world.has<components::StaticComponent>(entity)) {
      continue;
    }
    const auto& membership = world.get<components::StaticComponent>(entity);
    if (!membership.enabled ||
        (membership.flags & components::StaticComponentLighting) == 0u) {
      continue;
    }
    if (world.has<components::InstancedMeshComponent>(entity)) {
      const auto& instanced =
          world.get<components::InstancedMeshComponent>(entity);
      if (instanced.visible && authoredInstanceCount(instanced) != 0u) {
        result.lighting_warnings.push_back(
            "Static Lighting receiver '" + owner_id +
            "' uses InstancedMeshComponent and was excluded; the current "
            "material/instance ABI has no per-instance lightmap binding");
      }
      continue;
    }
    if (!world.has<components::MeshComponent>(entity) ||
        !world.has<components::TransformComponent>(entity)) {
      continue;
    }
    const auto& mesh_component = world.get<components::MeshComponent>(entity);
    if (mesh_component.mesh_asset_key.empty()) {
      continue;
    }
    const world::MeshData* mesh =
        assets.findMeshAsset(mesh_component.mesh_asset_key);
    if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.size() < 3u) {
      continue;
    }
    const glm::mat4 transform =
        worldMatrix(world.get<components::TransformComponent>(entity));
    targets.push_back(BakeTarget{
        .owner_id = owner_id,
        .entity = entity,
        .source_mesh = mesh,
        .transform = transform,
        .normal_transform = glm::inverseTranspose(glm::mat3(transform)),
    });
  }
  std::sort(targets.begin(), targets.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.owner_id < rhs.owner_id;
  });
  return targets;
}

bool appendOccluderMesh(const world::MeshData& mesh,
                        const glm::mat4& transform,
                        world::Entity entity,
                        std::string_view owner_id,
                        const SceneBakeExecutionOptions& execution,
                        SceneBakeResult& result,
                        std::vector<WorldTriangle>& triangles) {
  if (mesh.indices.size() % 3u != 0u) {
    result.diagnostic =
        "CPU light bake occluder has a non-triangular index stream: " +
        std::string(owner_id);
    return false;
  }
  for (size_t triangle = 0u; triangle < mesh.indices.size() / 3u;
       ++triangle) {
    const uint32_t ia = mesh.indices[triangle * 3u];
    const uint32_t ib = mesh.indices[triangle * 3u + 1u];
    const uint32_t ic = mesh.indices[triangle * 3u + 2u];
    if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() ||
        ic >= mesh.vertices.size()) {
      result.diagnostic =
          "CPU light bake occluder has an out-of-range index: " +
          std::string(owner_id);
      return false;
    }
    if (triangles.size() >= kCpuTriangleHardLimit) {
      result.diagnostic = "CPU light bake occluder triangle limit exceeded";
      return false;
    }
    if ((triangles.size() & 4095u) == 0u && execution.is_cancelled &&
        execution.is_cancelled()) {
      result.cancelled = true;
      result.diagnostic =
          "scene bake cancelled while collecting lighting occluders";
      return false;
    }
    triangles.push_back(WorldTriangle{
        .a = transformPoint(transform, mesh.vertices[ia]),
        .b = transformPoint(transform, mesh.vertices[ib]),
        .c = transformPoint(transform, mesh.vertices[ic]),
        .owner_key = entityKey(entity),
        .triangle_index = static_cast<uint32_t>(triangle),
    });
  }
  return true;
}

std::vector<WorldTriangle> collectOccluders(
    const SceneInstantiateResult& instance,
    const world::World& world,
    const assets::AssetRegistry& assets,
    const SceneBakeExecutionOptions& execution,
    SceneBakeResult& result) {
  std::vector<std::pair<std::string, world::Entity>> owners(
      instance.navigation_owners_by_id.begin(),
      instance.navigation_owners_by_id.end());
  std::sort(owners.begin(), owners.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.first < rhs.first;
  });
  std::vector<WorldTriangle> triangles;
  for (const auto& [owner_id, entity] : owners) {
    if (!world.isAlive(entity) ||
        !world.has<components::StaticComponent>(entity)) {
      continue;
    }
    const auto& membership = world.get<components::StaticComponent>(entity);
    if (!membership.enabled ||
        (membership.flags & components::StaticComponentShadows) == 0u) {
      continue;
    }
    if (world.has<components::MeshComponent>(entity) &&
        world.has<components::TransformComponent>(entity)) {
      const auto& component = world.get<components::MeshComponent>(entity);
      const world::MeshData* mesh =
          assets.findMeshAsset(component.mesh_asset_key);
      if (mesh != nullptr &&
          !appendOccluderMesh(*mesh,
                              worldMatrix(world.get<components::TransformComponent>(
                                  entity)),
                              entity,
                              owner_id,
                              execution,
                              result,
                              triangles)) {
        return {};
      }
    }

    if (!world.has<components::InstancedMeshComponent>(entity)) continue;
    const auto& instanced =
        world.get<components::InstancedMeshComponent>(entity);
    if (!instanced.visible || !instanced.shadow_visible) continue;
    const world::MeshData* mesh =
        assets.findMeshAsset(instanced.mesh_asset_key);
    if (mesh == nullptr) continue;

    // Static occlusion deliberately uses the highest-detail base mesh. LOD
    // meshes are camera-distance display representations and do not describe
    // distinct authored objects in the offline bake. Instance transforms are
    // owner-local, matching the renderer and navigation surface collector.
    const glm::mat4 owner_transform =
        world.has<components::TransformComponent>(entity)
            ? worldMatrix(world.get<components::TransformComponent>(entity))
            : glm::mat4(1.0f);
    switch (instanced.gpu_layout) {
      case rendering::InstanceGpuLayout::Matrix4x4Params:
        for (const components::MeshInstance& authored : instanced.instances) {
          if (!appendOccluderMesh(*mesh,
                                  owner_transform * instanceMatrix(authored),
                                  entity,
                                  owner_id,
                                  execution,
                                  result,
                                  triangles)) {
            return {};
          }
        }
        break;
      case rendering::InstanceGpuLayout::PositionYawScaleParams:
        for (const components::PlanarMeshInstance& authored :
             instanced.planar_instances) {
          if (!appendOccluderMesh(*mesh,
                                  owner_transform * instanceMatrix(authored),
                                  entity,
                                  owner_id,
                                  execution,
                                  result,
                                  triangles)) {
            return {};
          }
        }
        break;
    }
  }
  return triangles;
}

std::vector<BakeLight> collectLights(const SceneInstantiateResult& instance,
                                     const world::World& world,
                                     SceneBakeResult& result) {
  std::unordered_map<uint64_t, std::string> stable_ids;
  for (const auto& [id, entity] : instance.navigation_owners_by_id) {
    stable_ids.emplace(entityKey(entity), "owner:" + id);
  }
  for (const auto& [id, entity] : instance.lights_by_id) {
    stable_ids[entityKey(entity)] = "scene_light:" + id;
  }
  std::vector<std::pair<std::string, world::Entity>> entities;
  world.forEach<components::LightComponent, components::TransformComponent>(
      [&](world::Entity entity) {
        const auto& light = world.get<components::LightComponent>(entity);
        if (light.bake_mode == components::LightComponent::BakeMode::Realtime ||
            light.intensity <= 0.0f) {
          return;
        }
        const auto stable = stable_ids.find(entityKey(entity));
        entities.emplace_back(stable == stable_ids.end()
                                  ? "entity-index:" +
                                        std::to_string(entity.index) + ":" +
                                        std::to_string(entity.generation)
                                  : stable->second,
                              entity);
      });
  std::sort(entities.begin(), entities.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.first < rhs.first;
  });
  if (entities.size() > kCpuLightHardLimit) {
    result.diagnostic = "CPU light bake supports at most " +
                        std::to_string(kCpuLightHardLimit) +
                        " non-Realtime lights";
    return {};
  }
  size_t mixed_index = 0u;
  std::vector<BakeLight> lights;
  for (const auto& [id, entity] : entities) {
    const auto& component = world.get<components::LightComponent>(entity);
    if (component.bake_mode == components::LightComponent::BakeMode::Mixed &&
        id.starts_with("entity-index:")) {
      result.diagnostic =
          "Mixed light has no stable scene or prefab owner: " + id;
      return {};
    }
    if (component.bake_mode == components::LightComponent::BakeMode::Mixed &&
        mixed_index >= 64u) {
      result.diagnostic = "CPU light bake supports at most 64 Mixed lights";
      return {};
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    const glm::quat rotation = math::toGlm(transform.worldRotation());
    glm::vec3 direction = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    if (glm::dot(direction, direction) <= 1.0e-12f) {
      direction = {0.0f, -1.0f, 0.0f};
    } else {
      direction = glm::normalize(direction);
    }
    const uint64_t mixed_bit =
        component.bake_mode == components::LightComponent::BakeMode::Mixed
            ? (uint64_t{1} << mixed_index++)
            : 0u;
    if (mixed_bit != 0u) result.mixed_light_ids.push_back(id);
    lights.push_back(BakeLight{
        .component = component,
        .position = math::toGlm(transform.worldPosition()),
        .direction = direction,
        .mixed_bit = mixed_bit,
    });
  }
  return lights;
}

}  // namespace

bool bakeSceneLightmaps(const SceneDocument& document,
                        const SceneBakeDesc& desc,
                        world::World& world,
                        const SceneInstantiateResult& instance,
                        assets::AssetRegistry& assets,
                        std::string_view scene_fingerprint,
                        const SceneBakeExecutionOptions& execution,
                        BakeArtifactTransaction& artifacts,
                        SceneBakeResult& result) {
  const std::vector<BakeTarget> targets =
      collectTargets(instance, world, assets, result);
  if (targets.size() > kCpuTargetHardLimit) {
    result.diagnostic = "CPU light bake target limit exceeded";
    return false;
  }
  const std::vector<WorldTriangle> occluders =
      collectOccluders(instance, world, assets, execution, result);
  if (!result.diagnostic.empty()) return false;
  if (occluders.size() > kCpuTriangleHardLimit) {
    result.diagnostic = "CPU light bake occluder triangle limit exceeded";
    return false;
  }
  const TriangleBvh occluder_bvh(occluders, execution.is_cancelled);
  if (occluder_bvh.cancelled()) {
    result.cancelled = true;
    result.diagnostic = "scene bake cancelled while building lighting BVH";
    return false;
  }
  const std::vector<BakeLight> lights = collectLights(instance, world, result);
  if (!result.diagnostic.empty()) return false;

  float ambient_intensity = 0.03f;
  if (document.environment.has_value() &&
      document.environment->component.enabled) {
    ambient_intensity = std::clamp(
        document.environment->component.intensity * 0.08f, 0.0f, 1.0f);
  }

  const std::optional<std::filesystem::path> relative_directory =
      artifactDirectory(desc);
  if (!relative_directory.has_value()) {
    result.diagnostic =
        "lightmap artifacts require a content-root-relative bake path";
    return false;
  }
  uint64_t total_texels = 0u;
  for (size_t index = 0u; index < targets.size(); ++index) {
    if (execution.is_cancelled && execution.is_cancelled()) {
      result.cancelled = true;
      result.diagnostic = "scene bake cancelled during lighting";
      return false;
    }
    if (execution.on_progress) {
      execution.on_progress(SceneBakeProgress{
          .stage = SceneBakeStage::Lighting,
          .current = static_cast<uint64_t>(index),
          .total = static_cast<uint64_t>(targets.size()),
          .message = "Baking lightmap for " + targets[index].owner_id,
      });
    }

    const BakeTarget& target = targets[index];
    if (target.source_mesh->indices.size() % 3u != 0u) {
      result.diagnostic = "CPU light bake target has a non-triangular index "
                          "stream for '" +
                          target.owner_id + "'";
      return false;
    }
    if (target.source_mesh->indices.size() / 3u > kCpuTriangleHardLimit) {
      result.diagnostic = "CPU light bake target triangle limit exceeded for '" +
                          target.owner_id + "'";
      return false;
    }
    const bool usable_uv1 = hasUsableUv1(*target.source_mesh);
    if (!usable_uv1 && !desc.lighting.generate_uv1) {
      result.diagnostic = "mesh for '" + target.owner_id +
                          "' has no usable UV1 and UV generation is disabled";
      return false;
    }
    DerivedMesh derived = usable_uv1
                              ? DerivedMesh{.mesh = *target.source_mesh,
                                            .generated_uv1 = false}
                              : generateUv1Mesh(*target.source_mesh);
    if (derived.mesh.vertices.empty() ||
        derived.mesh.uvs1.size() != derived.mesh.vertices.size()) {
      result.diagnostic = "failed to derive a lightmap mesh for '" +
                          target.owner_id + "'";
      return false;
    }
    const std::optional<uint32_t> resolution = atlasResolution(
        derived.mesh,
        target.transform,
        desc.lighting,
        derived.generated_uv1);
    if (!resolution.has_value()) {
      result.diagnostic = "lightmap atlas limit is too small for '" +
                          target.owner_id + "'";
      return false;
    }
    const uint64_t target_texels =
        static_cast<uint64_t>(*resolution) * *resolution;
    if (target_texels > kCpuTotalTexelHardLimit - total_texels) {
      result.diagnostic = "CPU light bake total texel limit exceeded";
      return false;
    }
    total_texels += target_texels;
    if (derived.generated_uv1) {
      applyGeneratedUv1Padding(
          derived.mesh, *resolution, desc.lighting.padding);
    }
    assets::Rgba8Image irradiance{};
    assets::Rgba8Image direction{};
    uint64_t mixed_mask = 0u;
    if (!rasterize(target,
                   derived.mesh,
                   *resolution,
                   lights,
                   occluder_bvh,
                   ambient_intensity,
                   desc.lighting,
                   execution,
                   result,
                   irradiance,
                   desc.lighting.directional ? &direction : nullptr,
                   mixed_mask)) {
      return false;
    }

    const std::string owner_hash = assets::hashString(target.owner_id);
    std::string owner_label = safeStem(target.owner_id);
    owner_label.resize(std::min<size_t>(owner_label.size(), 64u));
    const std::string owner_stem =
        owner_label + "-" + owner_hash.substr(0u, 12u);
    std::string bake_stem = safeStem(desc.id.empty() ? "scene" : desc.id);
    bake_stem.resize(std::min<size_t>(bake_stem.size(), 48u));
    const std::filesystem::path mesh_relative =
        *relative_directory /
        (bake_stem + "." + owner_stem + ".kbmesh");
    const std::filesystem::path irradiance_relative =
        *relative_directory /
        (bake_stem + "." + owner_stem + ".irradiance.krgba8");
    const std::filesystem::path direction_relative =
        *relative_directory /
        (bake_stem + "." + owner_stem + ".direction.krgba8");
    const std::filesystem::path mesh_path =
        resolveDocumentPath(document, mesh_relative);
    const std::filesystem::path irradiance_path =
        resolveDocumentPath(document, irradiance_relative);
    const std::filesystem::path direction_path =
        resolveDocumentPath(document, direction_relative);
    const std::filesystem::path staged_mesh_path = artifacts.stage(mesh_path);
    const std::filesystem::path staged_irradiance_path =
        artifacts.stage(irradiance_path);
    const std::filesystem::path staged_direction_path =
        desc.lighting.directional ? artifacts.stage(direction_path)
                                  : std::filesystem::path{};
    std::string diagnostic;
    if (!assets::saveBakedMeshArtifact(staged_mesh_path,
                                       derived.mesh,
                                       &diagnostic)) {
      result.diagnostic = "failed to write derived mesh for '" +
                          target.owner_id + "': " + diagnostic;
      return false;
    }
    if (desc.lighting.directional) {
      diagnostic.clear();
      if (!assets::saveBakedRgba8Artifact(staged_direction_path,
                                          direction,
                                          &diagnostic)) {
        result.diagnostic = "failed to write dominant direction for '" +
                            target.owner_id + "': " + diagnostic;
        return false;
      }
    }
    diagnostic.clear();
    if (!assets::saveBakedRgba8Artifact(staged_irradiance_path,
                                        irradiance,
                                        &diagnostic)) {
      result.diagnostic = "failed to write irradiance for '" +
                          target.owner_id + "': " + diagnostic;
      return false;
    }

    const std::string key_prefix =
        "scene_bake/" + std::string(scene_fingerprint) + "/" + owner_stem;
    const std::string mesh_key = key_prefix + "/mesh";
    const std::string irradiance_key = key_prefix + "/irradiance";
    const std::string direction_key = desc.lighting.directional
                                          ? key_prefix + "/direction"
                                          : std::string{};
    result.lightmap_bindings.push_back(BakedLightmapBinding{
        .target_id = target.owner_id,
        .derived_mesh_asset_key = mesh_key,
        .irradiance_asset_key = irradiance_key,
        .direction_asset_key = direction_key,
        .uv_scale_offset = {1.0f, 1.0f, 0.0f, 0.0f},
        .intensity = 1.0f,
        .mixed_light_mask = mixed_mask,
    });
    result.produced_assets.push_back(SceneAssetRef{
        .id = mesh_key,
        .path = mesh_relative,
        .type = "baked_mesh",
    });
    result.produced_assets.push_back(SceneAssetRef{
        .id = irradiance_key,
        .path = irradiance_relative,
        .type = "baked_irradiance_rgba8",
    });
    if (desc.lighting.directional) {
      result.produced_assets.push_back(SceneAssetRef{
          .id = direction_key,
          .path = direction_relative,
          .type = "baked_direction_rgba8",
      });
    }
  }
  if (execution.on_progress && !targets.empty()) {
    execution.on_progress(SceneBakeProgress{
        .stage = SceneBakeStage::Lighting,
        .current = static_cast<uint64_t>(targets.size()),
        .total = static_cast<uint64_t>(targets.size()),
        .message = "CPU lightmap bake complete",
    });
  }
  return true;
}

}  // namespace karma::scenes::detail
