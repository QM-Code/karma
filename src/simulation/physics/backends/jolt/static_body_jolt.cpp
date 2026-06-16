#include "karma/simulation/physics/backends/jolt/static_body_jolt.hpp"
#include "karma/simulation/physics/backends/jolt/physics_world_jolt.hpp"
#include <glm/glm.hpp>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <spdlog/spdlog.h>

using namespace JPH;

namespace {
template <class TVec>
inline glm::vec3 toGlm(const TVec& v) { return glm::vec3(static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())); }
}

namespace karma::physics_backend {

std::unique_ptr<PhysicsStaticBodyBackend> PhysicsStaticBodyJolt::fromMesh(PhysicsWorldJolt* world, const std::string& meshPath) {
    (void)world;
    spdlog::warn("PhysicsStaticBodyJolt::fromMesh no longer imports '{}'; use MeshColliderComponent geometry or PhysicsShapeDesc mesh data", meshPath);
    return std::make_unique<PhysicsStaticBodyJolt>();
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
