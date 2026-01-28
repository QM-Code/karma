#include "karma/physics/backends/bullet/static_body_bullet.hpp"
#include "karma/physics/backends/bullet/physics_world_bullet.hpp"
#include "karma/geometry/mesh_loader.h"
#include "engine/geometry/mesh_loader.hpp"
#include <btBulletDynamicsCommon.h>
#include <spdlog/spdlog.h>

namespace {
inline glm::vec3 toGlm(const btVector3& v) { return glm::vec3(v.x(), v.y(), v.z()); }
}

namespace karma::physics_backend {

std::unique_ptr<PhysicsStaticBodyBackend> PhysicsStaticBodyBullet::fromMesh(PhysicsWorldBullet* world, const std::string& meshPath) {
    if (!world || !world->world()) return std::make_unique<PhysicsStaticBodyBullet>();

    std::vector<karma::geometry::MeshData> meshes = karma::geometry::loadGLB(meshPath);
    if (meshes.empty()) {
        spdlog::warn("PhysicsStaticBodyBullet::fromMesh: No meshes found at {}", meshPath);
        return std::make_unique<PhysicsStaticBodyBullet>();
    }

    auto triangleMesh = std::make_unique<btTriangleMesh>();
    for (const auto& mesh : meshes) {
        const auto& verts = mesh.vertices;
        const auto& idx = mesh.indices;
        if (idx.size() % 3 != 0) {
            spdlog::warn("PhysicsStaticBodyBullet::fromMesh: Mesh {} has non-multiple-of-3 indices; skipping remainder", meshPath);
        }
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            const glm::vec3& a = verts[idx[i]];
            const glm::vec3& b = verts[idx[i + 1]];
            const glm::vec3& c = verts[idx[i + 2]];
            triangleMesh->addTriangle(btVector3(a.x, a.y, a.z),
                                      btVector3(b.x, b.y, b.z),
                                      btVector3(c.x, c.y, c.z));
        }
    }

    auto shape = std::make_unique<btBvhTriangleMeshShape>(triangleMesh.get(), true);

    btTransform transform;
    transform.setIdentity();
    auto motionState = std::make_unique<btDefaultMotionState>(transform);
    btRigidBody::btRigidBodyConstructionInfo info(0.0f, motionState.get(), shape.get(), btVector3(0, 0, 0));
    auto body = std::make_unique<btRigidBody>(info);
    world->world()->addRigidBody(body.get());

    return std::make_unique<PhysicsStaticBodyBullet>(world,
                                                     std::move(body),
                                                     std::move(shape),
                                                     std::move(triangleMesh),
                                                     std::move(motionState));
}

PhysicsStaticBodyBullet::PhysicsStaticBodyBullet(PhysicsWorldBullet* world,
                                                 std::unique_ptr<btRigidBody> body,
                                                 std::unique_ptr<btCollisionShape> shape,
                                                 std::unique_ptr<btTriangleMesh> mesh,
                                                 std::unique_ptr<btMotionState> motionState)
    : world_(world),
      body_(std::move(body)),
      shape_(std::move(shape)),
      mesh_(std::move(mesh)),
      motionState_(std::move(motionState)) {}

PhysicsStaticBodyBullet::~PhysicsStaticBodyBullet() {
    destroy();
}

bool PhysicsStaticBodyBullet::isValid() const {
    return world_ != nullptr && body_ != nullptr;
}

glm::vec3 PhysicsStaticBodyBullet::getPosition() const {
    if (!body_) return glm::vec3(0.0f);
    btTransform transform;
    body_->getMotionState()->getWorldTransform(transform);
    return toGlm(transform.getOrigin());
}

glm::quat PhysicsStaticBodyBullet::getRotation() const {
    if (!body_) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    btTransform transform;
    body_->getMotionState()->getWorldTransform(transform);
    const btQuaternion& q = transform.getRotation();
    return glm::quat(q.w(), q.x(), q.y(), q.z());
}

void PhysicsStaticBodyBullet::destroy() {
    if (!world_ || !body_) return;
    if (world_->world()) {
        world_->world()->removeRigidBody(body_.get());
    }
    body_.reset();
    motionState_.reset();
    shape_.reset();
    mesh_.reset();
    world_ = nullptr;
}

std::uintptr_t PhysicsStaticBodyBullet::nativeHandle() const {
    return reinterpret_cast<std::uintptr_t>(body_.get());
}

} // namespace karma::physics_backend
