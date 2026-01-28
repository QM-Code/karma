#include "karma/physics/backends/jolt/static_body_jolt.hpp"
#include "karma/physics/backends/jolt/physics_world_jolt.hpp"
#include "karma/geometry/mesh_loader.h"
#include "karma/geometry/mesh_loader.h"
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Math/Float3.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <spdlog/spdlog.h>
#include <vector>

using namespace JPH;

namespace {
template <class TVec>
inline glm::vec3 toGlm(const TVec& v) { return glm::vec3(static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())); }
}

namespace karma::physics_backend {

std::unique_ptr<PhysicsStaticBodyBackend> PhysicsStaticBodyJolt::fromMesh(PhysicsWorldJolt* world, const std::string& meshPath) {
    if (!world || !world->physicsSystem()) return std::make_unique<PhysicsStaticBodyJolt>();

    std::vector<karma::geometry::MeshData> meshes = karma::geometry::loadGLB(meshPath);
    if (meshes.empty()) {
        spdlog::warn("PhysicsStaticBodyJolt::fromMesh: No meshes found at {}", meshPath);
        return std::make_unique<PhysicsStaticBodyJolt>();
    }

    using JPH::VertexList;
    using JPH::IndexedTriangleList;

    VertexList vertices;
    IndexedTriangleList triangles;
    vertices.reserve(1024);
    triangles.reserve(2048);

    uint32_t base = 0;
    for (const auto& mesh : meshes) {
        for (const auto& v : mesh.vertices) {
            vertices.push_back(JPH::Float3(v.x, v.y, v.z));
        }

        const auto& idx = mesh.indices;
        if (idx.size() % 3 != 0) {
            spdlog::warn("PhysicsStaticBodyJolt::fromMesh: Mesh {} has non-multiple-of-3 indices; skipping remainder", meshPath);
        }
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            triangles.push_back(JPH::IndexedTriangle(base + idx[i], base + idx[i + 1], base + idx[i + 2]));
        }

        base = static_cast<uint32_t>(vertices.size());
    }

    JPH::MeshShapeSettings meshSettings(std::move(vertices), std::move(triangles));
    auto shapeResult = meshSettings.Create();
    if (shapeResult.HasError()) {
        spdlog::error("PhysicsStaticBodyJolt::fromMesh: Failed to create mesh shape: {}", shapeResult.GetError().c_str());
        return std::make_unique<PhysicsStaticBodyJolt>();
    }

    JPH::RefConst<JPH::Shape> shape = shapeResult.Get();
    JPH::BodyCreationSettings settings(shape,
                                      JPH::RVec3::sZero(),
                                      JPH::Quat::sIdentity(),
                                      JPH::EMotionType::Static,
                                      0);

    JPH::BodyInterface& bi = world->physicsSystem()->GetBodyInterface();
    JPH::Body* body = bi.CreateBody(settings);
    if (!body) {
        spdlog::error("PhysicsStaticBodyJolt::fromMesh: Failed to create body");
        return std::make_unique<PhysicsStaticBodyJolt>();
    }

    bi.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    return std::make_unique<PhysicsStaticBodyJolt>(world, body->GetID());
}

PhysicsStaticBodyJolt::PhysicsStaticBodyJolt(PhysicsWorldJolt* world, const BodyID& bodyId)
    : world_(world), body_(bodyId) {}

PhysicsStaticBodyJolt::~PhysicsStaticBodyJolt() {
    destroy();
}

bool PhysicsStaticBodyJolt::isValid() const {
    return world_ != nullptr && body_.has_value();
}

glm::vec3 PhysicsStaticBodyJolt::getPosition() const {
    const BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    RVec3 pos = bi.GetCenterOfMassPosition(*body_);
    return toGlm(pos);
}

glm::quat PhysicsStaticBodyJolt::getRotation() const {
    const BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    Quat rot = bi.GetRotation(*body_);
    return glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
}

void PhysicsStaticBodyJolt::destroy() {
    if (!world_ || !body_.has_value()) return;
    world_->removeBody(*body_);
    body_.reset();
    world_ = nullptr;
}

std::uintptr_t PhysicsStaticBodyJolt::nativeHandle() const {
    return body_.has_value() ? static_cast<std::uintptr_t>(body_->GetIndexAndSequenceNumber()) : 0;
}

} // namespace karma::physics_backend
