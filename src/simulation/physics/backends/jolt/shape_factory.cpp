#include "shape_factory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/TriangleShape.h>
#include <Jolt/Math/Float3.h>

namespace {

constexpr float kMinExtent = 0.001f;
constexpr float kQuatEpsilon = 0.0001f;
constexpr float kVecEpsilon = 0.0001f;

float positive(float value) {
  return std::max(std::abs(value), kMinExtent);
}

glm::vec3 positive(const glm::vec3& value) {
  return {positive(value.x), positive(value.y), positive(value.z)};
}

JPH::Vec3 toJph(const glm::vec3& value) {
  return {value.x, value.y, value.z};
}

JPH::Quat toJph(const glm::quat& value) {
  const float length_sq = value.w * value.w + value.x * value.x +
                          value.y * value.y + value.z * value.z;
  if (length_sq <= kQuatEpsilon * kQuatEpsilon) {
    return JPH::Quat::sIdentity();
  }
  const float inv_len = 1.0f / std::sqrt(length_sq);
  return {value.x * inv_len, value.y * inv_len, value.z * inv_len, value.w * inv_len};
}

bool hasTranslation(const glm::vec3& value) {
  return std::abs(value.x) > kVecEpsilon ||
         std::abs(value.y) > kVecEpsilon ||
         std::abs(value.z) > kVecEpsilon;
}

bool hasRotation(const glm::quat& value) {
  return std::abs(value.x) > kQuatEpsilon ||
         std::abs(value.y) > kQuatEpsilon ||
         std::abs(value.z) > kQuatEpsilon ||
         std::abs(value.w - 1.0f) > kQuatEpsilon;
}

JPH::RefConst<JPH::Shape> shapeFromResult(JPH::Shape::ShapeResult result,
                                          std::string& error) {
  if (result.HasError()) {
    error = result.GetError().c_str();
    return nullptr;
  }
  return result.Get();
}

JPH::RefConst<JPH::Shape> applyLocalTransform(JPH::RefConst<JPH::Shape> shape,
                                              const glm::vec3& center,
                                              const glm::quat& rotation,
                                              std::string& error) {
  if (shape == nullptr || (!hasTranslation(center) && !hasRotation(rotation))) {
    return shape;
  }

  JPH::RotatedTranslatedShapeSettings settings(toJph(center), toJph(rotation), shape.GetPtr());
  return shapeFromResult(settings.Create(), error);
}

JPH::RefConst<JPH::Shape> createMeshShape(const karma::physics::PhysicsShapeDesc& desc,
                                          std::string& error) {
  if (desc.mesh_vertices.empty() || desc.mesh_indices.size() < 3) {
    error = desc.mesh_path.empty()
                ? "mesh shape requires vertices and triangle indices"
                : "mesh shape requires vertices and triangle indices: " + desc.mesh_path;
    return nullptr;
  }

  JPH::VertexList vertices;
  vertices.reserve(desc.mesh_vertices.size());
  for (const glm::vec3& vertex : desc.mesh_vertices) {
    vertices.push_back(JPH::Float3(vertex.x, vertex.y, vertex.z));
  }

  JPH::IndexedTriangleList triangles;
  triangles.reserve(desc.mesh_indices.size() / 3);
  const uint32_t vertex_count = static_cast<uint32_t>(desc.mesh_vertices.size());
  for (size_t i = 0; i + 2 < desc.mesh_indices.size(); i += 3) {
    const uint32_t a = desc.mesh_indices[i];
    const uint32_t b = desc.mesh_indices[i + 1];
    const uint32_t c = desc.mesh_indices[i + 2];
    if (a >= vertex_count || b >= vertex_count || c >= vertex_count) {
      error = desc.mesh_path.empty()
                  ? "mesh shape index out of range"
                  : "mesh shape index out of range: " + desc.mesh_path;
      return nullptr;
    }
    triangles.push_back(JPH::IndexedTriangle(a, b, c));
  }

  if (triangles.empty()) {
    error = desc.mesh_path.empty()
                ? "mesh shape requires at least one triangle"
                : "mesh shape requires at least one triangle: " + desc.mesh_path;
    return nullptr;
  }

  JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
  return shapeFromResult(settings.Create(), error);
}

JPH::RefConst<JPH::Shape> createCompoundShape(const karma::physics::PhysicsShapeDesc& desc,
                                              JPH::TempAllocator& temp_allocator,
                                              std::string& error) {
  if (desc.children.empty()) {
    error = "compound shape requires at least one child";
    return nullptr;
  }

  if (desc.children.size() == 1) {
    return karma::physics_backend::jolt::createShape(desc.children.front(), temp_allocator, error);
  }

  JPH::StaticCompoundShapeSettings settings;
  for (const auto& child : desc.children) {
    JPH::RefConst<JPH::Shape> child_shape =
        karma::physics_backend::jolt::createShape(child, temp_allocator, error);
    if (child_shape == nullptr) {
      return nullptr;
    }
    settings.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), child_shape.GetPtr());
  }

  return shapeFromResult(settings.Create(temp_allocator), error);
}

JPH::RefConst<JPH::Shape> createBaseShape(const karma::physics::PhysicsShapeDesc& desc,
                                          JPH::TempAllocator& temp_allocator,
                                          std::string& error) {
  using karma::physics::PhysicsShapeType;

  if (desc.type == PhysicsShapeType::Compound || !desc.children.empty()) {
    return createCompoundShape(desc, temp_allocator, error);
  }

  switch (desc.type) {
    case PhysicsShapeType::Box: {
      JPH::BoxShapeSettings settings(toJph(positive(desc.half_extents)),
                                     std::max(desc.convex_radius, 0.0f));
      return shapeFromResult(settings.Create(), error);
    }
    case PhysicsShapeType::Sphere: {
      JPH::SphereShapeSettings settings(positive(desc.radius));
      return shapeFromResult(settings.Create(), error);
    }
    case PhysicsShapeType::Capsule: {
      const float radius = positive(desc.radius);
      const float half_cylinder_height = std::max(desc.height * 0.5f - radius, 0.0f);
      JPH::CapsuleShapeSettings settings(half_cylinder_height, radius);
      return shapeFromResult(settings.Create(), error);
    }
    case PhysicsShapeType::Cylinder: {
      JPH::CylinderShapeSettings settings(positive(desc.height * 0.5f),
                                          positive(desc.radius),
                                          std::max(desc.convex_radius, 0.0f));
      return shapeFromResult(settings.Create(), error);
    }
    case PhysicsShapeType::TaperedCapsule: {
      const float top_radius = positive(desc.top_radius);
      const float bottom_radius = positive(desc.bottom_radius);
      const float cap_radius = std::max(top_radius, bottom_radius);
      const float half_height = std::max(desc.height * 0.5f - cap_radius, 0.0f);
      JPH::TaperedCapsuleShapeSettings settings(half_height, top_radius, bottom_radius);
      return shapeFromResult(settings.Create(), error);
    }
    case PhysicsShapeType::ConvexHull: {
      if (desc.points.empty()) {
        error = "convex hull shape requires points";
        return nullptr;
      }

      JPH::Array<JPH::Vec3> points;
      points.reserve(desc.points.size());
      for (const glm::vec3& point : desc.points) {
        points.push_back(toJph(point));
      }

      JPH::ConvexHullShapeSettings settings(points, std::max(desc.convex_radius, 0.0f));
      return shapeFromResult(settings.Create(), error);
    }
    case PhysicsShapeType::Triangle: {
      JPH::TriangleShapeSettings settings(toJph(desc.triangle[0]),
                                          toJph(desc.triangle[1]),
                                          toJph(desc.triangle[2]),
                                          std::max(desc.convex_radius, 0.0f));
      return shapeFromResult(settings.Create(), error);
    }
    case PhysicsShapeType::Mesh:
      return createMeshShape(desc, error);
    case PhysicsShapeType::HeightField: {
      const uint32_t expected_count = desc.height_sample_count * desc.height_sample_count;
      if (desc.height_sample_count == 0 || desc.height_samples.size() != expected_count) {
        error = "height field shape requires sample_count^2 height samples";
        return nullptr;
      }

      JPH::HeightFieldShapeSettings settings(desc.height_samples.data(),
                                             toJph(desc.height_offset),
                                             toJph(desc.height_scale),
                                             desc.height_sample_count);
      settings.mBlockSize = std::max(desc.height_block_size, 2u);
      settings.mBitsPerSample = std::clamp(desc.height_bits_per_sample, 1u, 8u);
      return shapeFromResult(settings.Create(), error);
    }
    case PhysicsShapeType::Compound:
      break;
  }

  error = "unsupported shape type";
  return nullptr;
}

}  // namespace

namespace karma::physics_backend::jolt {

JPH::RefConst<JPH::Shape> createShape(const karma::physics::PhysicsShapeDesc& desc,
                                      JPH::TempAllocator& temp_allocator,
                                      std::string& error) {
  JPH::RefConst<JPH::Shape> shape = createBaseShape(desc, temp_allocator, error);
  return applyLocalTransform(shape, desc.center, desc.rotation, error);
}

}  // namespace karma::physics_backend::jolt
