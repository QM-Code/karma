#include "private/physics/backends/jolt/rigid_body_jolt.hpp"
#include "private/physics/backends/jolt/physics_world_jolt.hpp"
#include "shape_factory.h"
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <spdlog/spdlog.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

using namespace JPH;

namespace {
template <class TVec>
inline glm::vec3 toGlm(const TVec& v) { return glm::vec3(static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())); }
inline Vec3 toJph(const glm::vec3& v) { return Vec3(v.x, v.y, v.z); }
inline RVec3 toJphR(const glm::vec3& v) { return RVec3(v.x, v.y, v.z); }
EMotionQuality toJph(karma::physics::PhysicsMotionQuality quality) {
    switch (quality) {
    case karma::physics::PhysicsMotionQuality::Discrete:
        return EMotionQuality::Discrete;
    case karma::physics::PhysicsMotionQuality::LinearCast:
        return EMotionQuality::LinearCast;
    }
    return EMotionQuality::Discrete;
}
}

namespace karma::physics::backend {

PhysicsRigidBodyJolt::PhysicsRigidBodyJolt(PhysicsWorldJolt* world, const BodyID& bodyId)
    : world_(world), body_(bodyId) {}

PhysicsRigidBodyJolt::~PhysicsRigidBodyJolt() {
    destroy();
}

bool PhysicsRigidBodyJolt::isValid() const {
    return world_ != nullptr && body_.has_value();
}

glm::vec3 PhysicsRigidBodyJolt::getPosition() const {
    const BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    RVec3 pos = bi.GetPosition(*body_);
    return toGlm(pos);
}

glm::quat PhysicsRigidBodyJolt::getRotation() const {
    const BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    Quat rot = bi.GetRotation(*body_);
    return glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
}

glm::vec3 PhysicsRigidBodyJolt::getVelocity() const {
    const BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    Vec3 vel = bi.GetLinearVelocity(*body_);
    return toGlm(vel);
}

glm::vec3 PhysicsRigidBodyJolt::getAngularVelocity() const {
    const BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    Vec3 vel = bi.GetAngularVelocity(*body_);
    return toGlm(vel);
}

glm::vec3 PhysicsRigidBodyJolt::getForwardVector() const {
    const BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    Quat rot = bi.GetRotation(*body_);
    glm::vec3 forward = glm::rotate(glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ()), glm::vec3(0, 0, -1));
    return glm::normalize(forward);
}

bool PhysicsRigidBodyJolt::isActive() const {
    if (!world_ || !body_.has_value()) return false;
    return world_->physicsSystem()->GetBodyInterface().IsActive(*body_);
}

void PhysicsRigidBodyJolt::setPosition(const glm::vec3& position) {
    BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    bi.SetPosition(*body_, RVec3(position.x, position.y, position.z), EActivation::Activate);
}

void PhysicsRigidBodyJolt::setRotation(const glm::quat& rotation) {
    BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    bi.SetRotation(*body_, Quat(rotation.x, rotation.y, rotation.z, rotation.w), EActivation::Activate);
}

void PhysicsRigidBodyJolt::setPositionAndRotation(const glm::vec3& position, const glm::quat& rotation) {
    if (!world_ || !body_.has_value()) return;
    BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    bi.SetPositionAndRotation(*body_,
                              RVec3(position.x, position.y, position.z),
                              Quat(rotation.x, rotation.y, rotation.z, rotation.w),
                              EActivation::Activate);
}

void PhysicsRigidBodyJolt::moveKinematic(const glm::vec3& targetPosition,
                                         const glm::quat& targetRotation,
                                         float deltaTime) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().MoveKinematic(
        *body_,
        RVec3(targetPosition.x, targetPosition.y, targetPosition.z),
        Quat(targetRotation.x, targetRotation.y, targetRotation.z, targetRotation.w),
        deltaTime);
}

void PhysicsRigidBodyJolt::setVelocity(const glm::vec3& velocity) {
    BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    bi.SetLinearVelocity(*body_, Vec3(velocity.x, velocity.y, velocity.z));
    bi.ActivateBody(*body_);
}

void PhysicsRigidBodyJolt::setAngularVelocity(const glm::vec3& angularVelocity) {
    BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    bi.SetAngularVelocity(*body_, Vec3(angularVelocity.x, angularVelocity.y, angularVelocity.z));
    bi.ActivateBody(*body_);
}

void PhysicsRigidBodyJolt::setLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                                       const glm::vec3& angularVelocity) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().SetLinearAndAngularVelocity(
        *body_, toJph(linearVelocity), toJph(angularVelocity));
}

void PhysicsRigidBodyJolt::addLinearVelocity(const glm::vec3& velocity) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().AddLinearVelocity(*body_, toJph(velocity));
}

void PhysicsRigidBodyJolt::addLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                                       const glm::vec3& angularVelocity) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().AddLinearAndAngularVelocity(
        *body_, toJph(linearVelocity), toJph(angularVelocity));
}

glm::vec3 PhysicsRigidBodyJolt::getPointVelocity(const glm::vec3& point) const {
    if (!world_ || !body_.has_value()) return glm::vec3(0.0f);
    return toGlm(world_->physicsSystem()->GetBodyInterface().GetPointVelocity(*body_, toJphR(point)));
}

void PhysicsRigidBodyJolt::setKinematic(bool kinematic) {
    if (!world_ || !body_.has_value()) return;
    BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    const EMotionType motion = kinematic ? EMotionType::Kinematic : EMotionType::Dynamic;
    bi.SetMotionType(*body_, motion, EActivation::Activate);
}

void PhysicsRigidBodyJolt::setMotionQuality(karma::physics::PhysicsMotionQuality quality) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().SetMotionQuality(*body_, toJph(quality));
}

void PhysicsRigidBodyJolt::setUseGravity(bool useGravity) {
    if (!world_ || !body_.has_value()) return;
    BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    bi.SetGravityFactor(*body_, useGravity ? 1.0f : 0.0f);
    bi.ActivateBody(*body_);
}

void PhysicsRigidBodyJolt::setGravityFactor(float gravityFactor) {
    if (!world_ || !body_.has_value()) return;
    BodyInterface& bi = world_->physicsSystem()->GetBodyInterface();
    bi.SetGravityFactor(*body_, gravityFactor);
    bi.ActivateBody(*body_);
}

float PhysicsRigidBodyJolt::getGravityFactor() const {
    if (!world_ || !body_.has_value()) return 0.0f;
    return world_->physicsSystem()->GetBodyInterface().GetGravityFactor(*body_);
}

void PhysicsRigidBodyJolt::setTrigger(bool trigger) {
    if (!world_ || !body_.has_value()) return;
    {
        BodyLockWrite lock(world_->physicsSystem()->GetBodyLockInterface(), *body_);
        if (!lock.Succeeded()) {
            return;
        }
        lock.GetBody().SetIsSensor(trigger);
    }
    world_->physicsSystem()->GetBodyInterface().ActivateBody(*body_);
}

void PhysicsRigidBodyJolt::setFriction(float friction) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().SetFriction(*body_, friction);
}

float PhysicsRigidBodyJolt::getFriction() const {
    if (!world_ || !body_.has_value()) return 0.0f;
    return world_->physicsSystem()->GetBodyInterface().GetFriction(*body_);
}

void PhysicsRigidBodyJolt::setRestitution(float restitution) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().SetRestitution(*body_, restitution);
}

float PhysicsRigidBodyJolt::getRestitution() const {
    if (!world_ || !body_.has_value()) return 0.0f;
    return world_->physicsSystem()->GetBodyInterface().GetRestitution(*body_);
}

void PhysicsRigidBodyJolt::setUseManifoldReduction(bool enabled) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().SetUseManifoldReduction(*body_, enabled);
}

bool PhysicsRigidBodyJolt::getUseManifoldReduction() const {
    if (!world_ || !body_.has_value()) return false;
    return world_->physicsSystem()->GetBodyInterface().GetUseManifoldReduction(*body_);
}

void PhysicsRigidBodyJolt::setUserData(uint64_t userData) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().SetUserData(*body_, userData);
}

uint64_t PhysicsRigidBodyJolt::getUserData() const {
    if (!world_ || !body_.has_value()) return 0;
    return world_->physicsSystem()->GetBodyInterface().GetUserData(*body_);
}

void PhysicsRigidBodyJolt::activate() {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().ActivateBody(*body_);
}

void PhysicsRigidBodyJolt::deactivate() {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().DeactivateBody(*body_);
}

void PhysicsRigidBodyJolt::resetSleepTimer() {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().ResetSleepTimer(*body_);
}

bool PhysicsRigidBodyJolt::setShape(const karma::physics::PhysicsShapeDesc& shape,
                                    bool updateMassProperties,
                                    bool activate) {
    if (!world_ || !body_.has_value() || world_->tempAllocator() == nullptr) return false;

    std::string shape_error;
    JPH::RefConst<JPH::Shape> jolt_shape =
        jolt::createShape(shape, *world_->tempAllocator(), shape_error);
    if (jolt_shape == nullptr) {
        spdlog::error("Failed to replace Jolt body shape: {}", shape_error);
        return false;
    }

    world_->physicsSystem()->GetBodyInterface().SetShape(
        *body_,
        jolt_shape.GetPtr(),
        updateMassProperties,
        activate ? EActivation::Activate : EActivation::DontActivate);
    return true;
}

void PhysicsRigidBodyJolt::addForce(const glm::vec3& force) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().AddForce(*body_, toJph(force));
}

void PhysicsRigidBodyJolt::addForceAtPosition(const glm::vec3& force, const glm::vec3& position) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().AddForce(*body_, toJph(force), toJphR(position));
}

void PhysicsRigidBodyJolt::addTorque(const glm::vec3& torque) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().AddTorque(*body_, toJph(torque));
}

void PhysicsRigidBodyJolt::addImpulse(const glm::vec3& impulse) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().AddImpulse(*body_, toJph(impulse));
}

void PhysicsRigidBodyJolt::addImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& position) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().AddImpulse(*body_, toJph(impulse), toJphR(position));
}

void PhysicsRigidBodyJolt::addAngularImpulse(const glm::vec3& impulse) {
    if (!world_ || !body_.has_value()) return;
    world_->physicsSystem()->GetBodyInterface().AddAngularImpulse(*body_, toJph(impulse));
}

bool PhysicsRigidBodyJolt::applyBuoyancyImpulse(const karma::physics::PhysicsBuoyancyDesc& desc) {
    if (!world_ || !body_.has_value()) return false;
    return world_->physicsSystem()->GetBodyInterface().ApplyBuoyancyImpulse(
        *body_,
        toJphR(desc.surface_position),
        toJph(desc.surface_normal),
        desc.buoyancy,
        desc.linear_drag,
        desc.angular_drag,
        toJph(desc.fluid_velocity),
        toJph(desc.gravity),
        desc.delta_time);
}

bool PhysicsRigidBodyJolt::isGrounded(const glm::vec3& dimensions) const {
    if (!world_ || !body_.has_value()) return false;

    RefConst<Shape> shape = new BoxShape(Vec3(dimensions.x * 0.5f, dimensions.y * 0.5f, dimensions.z * 0.5f));
    RMat44 transform = world_->physicsSystem()->GetBodyInterface().GetCenterOfMassTransform(*body_);
    RShapeCast shapeCast(shape, Vec3::sReplicate(1.0f), transform, Vec3(0, -0.1f, 0));

    ShapeCastSettings settings;
    ClosestHitCollisionCollector<CastShapeCollector> collector;
    world_->physicsSystem()->GetNarrowPhaseQuery().CastShape(shapeCast, settings, RVec3::sZero(), collector);
    if (!collector.HadHit()) return false;

    Vec3 n = collector.mHit.mPenetrationAxis;
    if (n.LengthSq() < 1e-6f) return false;
    n = n.Normalized();
    return n.Dot(Vec3(0, 1, 0)) > 0.7f;
}

void PhysicsRigidBodyJolt::destroy() {
    if (!world_ || !body_.has_value()) {
        return;
    }

    world_->removeBody(*body_);
    body_.reset();
    world_ = nullptr;
}

std::uintptr_t PhysicsRigidBodyJolt::nativeHandle() const {
    return body_.has_value() ? static_cast<std::uintptr_t>(body_->GetIndexAndSequenceNumber()) : 0;
}

} // namespace karma::physics::backend
