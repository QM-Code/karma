#include "private/physics/backends/jolt/physics_world_jolt.hpp"

#include "shape_factory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <spdlog/spdlog.h>

namespace {

JPH::Vec3 toJph(const glm::vec3& value) {
    return {value.x, value.y, value.z};
}

JPH::Quat toJph(const glm::quat& value) {
    const float length_sq = value.w * value.w + value.x * value.x +
                            value.y * value.y + value.z * value.z;
    if (length_sq <= 0.000001f) {
        return JPH::Quat::sIdentity();
    }
    const float inv_len = 1.0f / std::sqrt(length_sq);
    return {value.x * inv_len, value.y * inv_len, value.z * inv_len, value.w * inv_len};
}

glm::vec3 toGlm(const JPH::Vec3& value) {
    return {value.GetX(), value.GetY(), value.GetZ()};
}

JPH::EBackFaceMode toJph(karma::physics::PhysicsBackFaceMode mode) {
    switch (mode) {
    case karma::physics::PhysicsBackFaceMode::Ignore:
        return JPH::EBackFaceMode::IgnoreBackFaces;
    case karma::physics::PhysicsBackFaceMode::Collide:
        return JPH::EBackFaceMode::CollideWithBackFaces;
    }
    return JPH::EBackFaceMode::IgnoreBackFaces;
}

glm::vec3 normalizedGlmOr(JPH::Vec3 value, const glm::vec3& fallback) {
    const float length_sq = value.LengthSq();
    if (length_sq <= 1.0e-8f) {
        return fallback;
    }
    value /= std::sqrt(length_sq);
    return toGlm(value);
}

std::uintptr_t bodyHandle(const JPH::BodyID& id) {
    return static_cast<std::uintptr_t>(id.GetIndexAndSequenceNumber());
}

bool queryIgnoresBody(const karma::physics::PhysicsQueryFilter& filter, std::uintptr_t handle) {
    if (handle == 0) {
        return false;
    }
    if (filter.ignored_body == handle) {
        return true;
    }
    return std::find(filter.ignored_bodies.begin(), filter.ignored_bodies.end(), handle) !=
           filter.ignored_bodies.end();
}

JPH::RMat44 queryTransform(const glm::vec3& position, const glm::quat& rotation) {
    return JPH::RMat44::sRotationTranslation(toJph(rotation),
                                             JPH::RVec3(position.x, position.y, position.z));
}

}  // namespace

namespace karma::physics::backend {

class PhysicsWorldJoltQueryBodyFilter final : public JPH::BodyFilter {
public:
    PhysicsWorldJoltQueryBodyFilter(const PhysicsWorldJolt& world,
                                    const karma::physics::PhysicsQueryFilter& filter)
        : world_(world), filter_(filter) {}

    bool ShouldCollide(const JPH::BodyID& inBodyID) const override {
        return !queryIgnoresBody(filter_, bodyHandle(inBodyID));
    }

    bool ShouldCollideLocked(const JPH::Body& inBody) const override {
        return world_.queryAllowsBody(inBody, filter_);
    }

private:
    const PhysicsWorldJolt& world_;
    const karma::physics::PhysicsQueryFilter& filter_;
};

bool PhysicsWorldJolt::castRay(const karma::physics::PhysicsRaycastDesc& desc,
                               karma::physics::PhysicsQueryHit& outHit) const {
    std::vector<karma::physics::PhysicsQueryHit> hits;
    castRayAll(desc, hits);
    if (hits.empty()) {
        return false;
    }
    outHit = hits.front();
    return true;
}

void PhysicsWorldJolt::castRayAll(const karma::physics::PhysicsRaycastDesc& desc,
                                  std::vector<karma::physics::PhysicsQueryHit>& outHits) const {
    if (!physicsSystem_) return;

    JPH::RVec3 origin(desc.from.x, desc.from.y, desc.from.z);
    JPH::Vec3 direction(desc.to.x - desc.from.x, desc.to.y - desc.from.y, desc.to.z - desc.from.z);
    JPH::RRayCast ray(origin, direction);

    JPH::RayCastSettings settings;
    settings.SetBackFaceMode(toJph(desc.back_face_mode));
    settings.mTreatConvexAsSolid = desc.treat_convex_as_solid;

    PhysicsWorldJoltQueryBodyFilter body_filter(*this, desc.filter);
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, settings, collector, {}, {}, body_filter);
    collector.Sort();

    const glm::vec3 ray_vector(direction.GetX(), direction.GetY(), direction.GetZ());
    for (const JPH::RayCastResult& result : collector.mHits) {
        karma::physics::PhysicsQueryHit hit;
        hit.body = bodyHandle(result.mBodyID);
        hit.fraction = result.mFraction;
        hit.point = glm::vec3(desc.from) + ray_vector * result.mFraction;
        hit.point_on_query = hit.point;
        hit.point_on_body = hit.point;

        JPH::BodyLockRead lock(physicsSystem_->GetBodyLockInterface(), result.mBodyID);
        if (lock.Succeeded()) {
            hit.normal = toGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(
                result.mSubShapeID2, ray.GetPointOnRay(result.mFraction)));
        }
        outHits.push_back(hit);
    }
}

void PhysicsWorldJolt::collidePoint(const glm::vec3& point,
                                    const karma::physics::PhysicsQueryFilter& filter,
                                    std::vector<karma::physics::PhysicsQueryHit>& outHits) const {
    if (!physicsSystem_) return;

    PhysicsWorldJoltQueryBodyFilter body_filter(*this, filter);
    JPH::AllHitCollisionCollector<JPH::CollidePointCollector> collector;
    physicsSystem_->GetNarrowPhaseQuery().CollidePoint(
        JPH::RVec3(point.x, point.y, point.z), collector, {}, {}, body_filter);

    for (const JPH::CollidePointResult& result : collector.mHits) {
        karma::physics::PhysicsQueryHit hit;
        hit.body = bodyHandle(result.mBodyID);
        hit.point = point;
        hit.point_on_query = point;
        hit.point_on_body = point;
        outHits.push_back(hit);
    }
}

void PhysicsWorldJolt::collideShape(const karma::physics::PhysicsShapeQueryDesc& desc,
                                    std::vector<karma::physics::PhysicsQueryHit>& outHits) const {
    if (!physicsSystem_ || !tempAllocator_) return;

    std::string shape_error;
    JPH::RefConst<JPH::Shape> shape = jolt::createShape(desc.shape, *tempAllocator_, shape_error);
    if (shape == nullptr) {
        spdlog::error("Failed to create Jolt query shape: {}", shape_error);
        return;
    }

    JPH::CollideShapeSettings settings;
    settings.mBackFaceMode = toJph(desc.back_face_mode);
    settings.mMaxSeparationDistance = std::max(desc.max_separation_distance, 0.0f);

    PhysicsWorldJoltQueryBodyFilter body_filter(*this, desc.filter);
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    physicsSystem_->GetNarrowPhaseQuery().CollideShape(
        shape.GetPtr(),
        toJph(desc.scale),
        queryTransform(desc.position, desc.rotation),
        settings,
        JPH::RVec3::sZero(),
        collector,
        {},
        {},
        body_filter);
    collector.Sort();

    for (const JPH::CollideShapeResult& result : collector.mHits) {
        karma::physics::PhysicsQueryHit hit;
        hit.body = bodyHandle(result.mBodyID2);
        hit.penetration_depth = result.mPenetrationDepth;
        hit.point_on_query = toGlm(result.mContactPointOn1);
        hit.point_on_body = toGlm(result.mContactPointOn2);
        hit.point = hit.point_on_body;
        hit.normal = normalizedGlmOr(result.mPenetrationAxis, glm::vec3(0.0f, 1.0f, 0.0f));
        outHits.push_back(hit);
    }
}

void PhysicsWorldJolt::castShape(const karma::physics::PhysicsShapeCastDesc& desc,
                                 std::vector<karma::physics::PhysicsQueryHit>& outHits) const {
    if (!physicsSystem_ || !tempAllocator_) return;

    std::string shape_error;
    JPH::RefConst<JPH::Shape> shape = jolt::createShape(desc.shape, *tempAllocator_, shape_error);
    if (shape == nullptr) {
        spdlog::error("Failed to create Jolt cast shape: {}", shape_error);
        return;
    }
    if (shape->GetType() == JPH::EShapeType::Mesh ||
        shape->GetType() == JPH::EShapeType::HeightField) {
        spdlog::warn("Jolt shape casts require a non-mesh query shape");
        return;
    }

    JPH::RShapeCast shape_cast = JPH::RShapeCast::sFromWorldTransform(
        shape.GetPtr(),
        toJph(desc.scale),
        queryTransform(desc.from, desc.rotation),
        toJph(desc.translation));

    JPH::ShapeCastSettings settings;
    settings.mBackFaceModeTriangles = toJph(desc.back_face_mode_triangles);
    settings.mBackFaceModeConvex = toJph(desc.back_face_mode_convex);
    settings.mUseShrunkenShapeAndConvexRadius = desc.use_shrunken_shape_and_convex_radius;
    settings.mReturnDeepestPoint = desc.return_deepest_point;

    PhysicsWorldJoltQueryBodyFilter body_filter(*this, desc.filter);
    JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector;
    physicsSystem_->GetNarrowPhaseQuery().CastShape(
        shape_cast, settings, JPH::RVec3::sZero(), collector, {}, {}, body_filter);
    collector.Sort();

    for (const JPH::ShapeCastResult& result : collector.mHits) {
        karma::physics::PhysicsQueryHit hit;
        hit.body = bodyHandle(result.mBodyID2);
        hit.fraction = result.mFraction;
        hit.penetration_depth = result.mPenetrationDepth;
        hit.point_on_query = toGlm(result.mContactPointOn1);
        hit.point_on_body = toGlm(result.mContactPointOn2);
        hit.point = hit.point_on_body;
        hit.normal = normalizedGlmOr(result.mPenetrationAxis, glm::vec3(0.0f, 1.0f, 0.0f));
        hit.back_face = result.mIsBackFaceHit;
        outHits.push_back(hit);
    }
}

bool PhysicsWorldJolt::queryAllowsBody(const JPH::Body& body,
                                       const karma::physics::PhysicsQueryFilter& filter) const {
    const std::uintptr_t handle = bodyHandle(body.GetID());
    if (queryIgnoresBody(filter, handle)) {
        return false;
    }
    if (!filter.include_sensors && body.IsSensor()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(body_filters_mutex_);
    auto it = body_filters_.find(body.GetID().GetIndexAndSequenceNumber());
    const karma::physics::PhysicsCollisionFilter body_filter =
        it != body_filters_.end() ? it->second : karma::physics::PhysicsCollisionFilter{};
    return (body_filter.layers & filter.collision_mask) != 0u;
}

}  // namespace karma::physics::backend
