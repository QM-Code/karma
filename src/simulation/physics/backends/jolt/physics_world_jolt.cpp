#include "private/physics/backends/jolt/physics_world_jolt.hpp"
#include "private/physics/backends/jolt/character_controller_jolt.hpp"
#include "private/physics/backends/jolt/constraint_jolt.hpp"
#include "private/physics/backends/jolt/rigid_body_jolt.hpp"
#include "shape_factory.h"
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SpringSettings.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <spdlog/spdlog.h>
#include <thread>

namespace {
using namespace JPH;

constexpr uint32 MAX_BODIES = 4096;
constexpr uint32 NUM_BODY_MUTEXES = 0;
constexpr uint32 MAX_BODY_PAIRS = 65536;
constexpr uint32 MAX_CONTACT_CONSTRAINTS = 8192;

using ObjectLayer = JPH::ObjectLayer;
constexpr ObjectLayer NonMoving = 0;
constexpr ObjectLayer Moving = 1;

static constexpr BroadPhaseLayer BP_NON_MOVING(0);
static constexpr BroadPhaseLayer BP_MOVING(1);
static constexpr uint BP_LAYER_COUNT = 2;

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[NonMoving] = BP_NON_MOVING;
        mObjectToBroadPhase[Moving] = BP_MOVING;
    }

    uint GetNumBroadPhaseLayers() const override { return BP_LAYER_COUNT; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {
        return mObjectToBroadPhase[static_cast<size_t>(layer)];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer layer) const override {
        if (layer == BP_NON_MOVING) return "NonMoving";
        if (layer == BP_MOVING) return "Moving";
        return "Unknown";
    }
#endif

private:
    BroadPhaseLayer mObjectToBroadPhase[2];
};

class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer, ObjectLayer) const override {
        return true;
    }
};

class ObjectVsBroadPhaseLayerFilterImpl final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer, BroadPhaseLayer bplayer) const override {
        return bplayer == BP_MOVING || bplayer == BP_NON_MOVING;
    }
};

inline Vec3 toJph(const glm::vec3& v) { return Vec3(v.x, v.y, v.z); }
inline Quat toJph(const glm::quat& q) { return Quat(q.x, q.y, q.z, q.w); }
inline glm::vec3 toGlm(const Vec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
inline glm::vec3 toGlmRVec(const RVec3& v) {
    return glm::vec3(static_cast<float>(v.GetX()),
                     static_cast<float>(v.GetY()),
                     static_cast<float>(v.GetZ()));
}

EMotionType toJph(karma::physics::PhysicsMotionType motion) {
    switch (motion) {
    case karma::physics::PhysicsMotionType::Static:
        return EMotionType::Static;
    case karma::physics::PhysicsMotionType::Kinematic:
        return EMotionType::Kinematic;
    case karma::physics::PhysicsMotionType::Dynamic:
        return EMotionType::Dynamic;
    }
    return EMotionType::Dynamic;
}

EMotionQuality toJph(karma::physics::PhysicsMotionQuality quality) {
    switch (quality) {
    case karma::physics::PhysicsMotionQuality::Discrete:
        return EMotionQuality::Discrete;
    case karma::physics::PhysicsMotionQuality::LinearCast:
        return EMotionQuality::LinearCast;
    }
    return EMotionQuality::Discrete;
}

EAllowedDOFs toAllowedDofs(uint8_t dof_mask) {
    if (dof_mask == karma::physics::PhysicsDofNone) {
        return EAllowedDOFs::All;
    }
    return static_cast<EAllowedDOFs>(dof_mask);
}

EConstraintSpace toJph(karma::physics::PhysicsConstraintSpace space) {
    switch (space) {
    case karma::physics::PhysicsConstraintSpace::World:
        return EConstraintSpace::WorldSpace;
    case karma::physics::PhysicsConstraintSpace::LocalToBodyCenterOfMass:
        return EConstraintSpace::LocalToBodyCOM;
    }
    return EConstraintSpace::WorldSpace;
}

ESpringMode toJph(karma::physics::PhysicsSpringMode mode) {
    switch (mode) {
    case karma::physics::PhysicsSpringMode::FrequencyAndDamping:
        return ESpringMode::FrequencyAndDamping;
    case karma::physics::PhysicsSpringMode::StiffnessAndDamping:
        return ESpringMode::StiffnessAndDamping;
    }
    return ESpringMode::FrequencyAndDamping;
}

SpringSettings toJph(const karma::physics::PhysicsSpringSettings& settings) {
    return SpringSettings(toJph(settings.mode), settings.frequency_or_stiffness, settings.damping);
}

Vec3 normalizedOr(Vec3 value, Vec3 fallback) {
    const float length_sq = value.LengthSq();
    if (length_sq <= 1.0e-8f) {
        return fallback;
    }
    return value / std::sqrt(length_sq);
}

template <typename Settings>
void fillConstraintBase(Settings& settings, const karma::physics::PhysicsConstraintDesc& desc) {
    settings.mEnabled = desc.enabled;
    settings.mConstraintPriority = desc.priority;
    settings.mNumVelocityStepsOverride = static_cast<JPH::uint>(desc.velocity_solver_steps);
    settings.mNumPositionStepsOverride = static_cast<JPH::uint>(desc.position_solver_steps);
    settings.mDrawConstraintSize = desc.draw_size;
    settings.mUserData = desc.user_data;
}

void JoltTrace(const char* fmt, ...) {
    va_list list;
    va_start(list, fmt);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, list);
    va_end(list);
    spdlog::trace("Jolt: {}", buffer);
}

void initJoltOnce() {
    static bool initialized = false;
    if (initialized) return;

    JPH::RegisterDefaultAllocator();
    JPH::Trace = &JoltTrace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = [](const char* expr, const char* msg, const char* file, uint line) {
        spdlog::error("Jolt assert failed: {} {} ({}:{})", expr, msg ? msg : "", file, line);
        return true;
    });

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    initialized = true;
}
} // namespace

namespace karma::physics::backend {

class PhysicsWorldJoltContactListener final : public JPH::ContactListener {
public:
    explicit PhysicsWorldJoltContactListener(PhysicsWorldJolt& world) : world_(world) {}

    JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1,
                                          const JPH::Body& inBody2,
                                          JPH::RVec3Arg /*inBaseOffset*/,
                                          const JPH::CollideShapeResult& /*inCollisionResult*/) override {
        return world_.bodiesShouldCollide(inBody1, inBody2)
                   ? JPH::ValidateResult::AcceptAllContactsForThisBodyPair
                   : JPH::ValidateResult::RejectAllContactsForThisBodyPair;
    }

    void OnContactAdded(const JPH::Body& inBody1,
                        const JPH::Body& inBody2,
                        const JPH::ContactManifold& inManifold,
                        JPH::ContactSettings& /*ioSettings*/) override {
        store(inBody1, inBody2, inManifold);
    }

    void OnContactPersisted(const JPH::Body& inBody1,
                            const JPH::Body& inBody2,
                            const JPH::ContactManifold& inManifold,
                            JPH::ContactSettings& /*ioSettings*/) override {
        store(inBody1, inBody2, inManifold);
    }

private:
    void store(const JPH::Body& inBody1,
               const JPH::Body& inBody2,
               const JPH::ContactManifold& inManifold) {
        if (inManifold.mRelativeContactPointsOn1.empty() || inManifold.mRelativeContactPointsOn2.empty()) {
            return;
        }

        const auto key = PhysicsWorldJolt::ContactKey{
            .a = inBody1.GetID().GetIndexAndSequenceNumber(),
            .b = inBody2.GetID().GetIndexAndSequenceNumber(),
        };

        const karma::physics::PhysicsContact contact{
            .handle_a = static_cast<std::uintptr_t>(key.a),
            .handle_b = static_cast<std::uintptr_t>(key.b),
            .point_a = toGlmRVec(inManifold.GetWorldSpaceContactPointOn1(0)),
            .point_b = toGlmRVec(inManifold.GetWorldSpaceContactPointOn2(0)),
            .normal_a_to_b = toGlm(inManifold.mWorldSpaceNormal),
        };

        std::lock_guard<std::mutex> lock(world_.contacts_mutex_);
        world_.contacts_[key] = contact;
    }

    PhysicsWorldJolt& world_;
};

PhysicsWorldJolt::PhysicsWorldJolt() {
    initJoltOnce();

    tempAllocator_ = std::make_unique<TempAllocatorImpl>(32u * 1024u * 1024u);
    jobSystem_ = std::make_unique<JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency());

    static BPLayerInterfaceImpl broadPhaseLayers;
    static ObjectVsBroadPhaseLayerFilterImpl objectVsBroadphaseFilter;
    static ObjectLayerPairFilterImpl objectPairFilter;

    physicsSystem_ = std::make_unique<JPH::PhysicsSystem>();
    physicsSystem_->Init(MAX_BODIES,
                         NUM_BODY_MUTEXES,
                         MAX_BODY_PAIRS,
                         MAX_CONTACT_CONSTRAINTS,
                         broadPhaseLayers,
                         objectVsBroadphaseFilter,
                         objectPairFilter);

    physicsSystem_->SetGravity(Vec3(0, -9.8f, 0));
    contactListener_ = std::make_unique<PhysicsWorldJoltContactListener>(*this);
    physicsSystem_->SetContactListener(contactListener_.get());
}

PhysicsWorldJolt::~PhysicsWorldJolt() {
    physicsSystem_.reset();
    jobSystem_.reset();
    tempAllocator_.reset();
}

void PhysicsWorldJolt::update(float deltaTime) {
    if (!physicsSystem_) return;
    {
        std::lock_guard<std::mutex> lock(contacts_mutex_);
        contacts_.clear();
    }
    physicsSystem_->Update(deltaTime, 1, tempAllocator_.get(), jobSystem_.get());
}

void PhysicsWorldJolt::setGravity(float gravity) {
    if (physicsSystem_) {
        physicsSystem_->SetGravity(Vec3(0, gravity, 0));
    }
}

std::unique_ptr<PhysicsRigidBodyBackend> PhysicsWorldJolt::createBody(
    const karma::physics::PhysicsBodyDesc& desc) {
    if (!physicsSystem_ || !tempAllocator_) return std::make_unique<PhysicsRigidBodyJolt>();

    std::string shape_error;
    JPH::RefConst<JPH::Shape> shape =
        jolt::createShape(desc.shape, *tempAllocator_, shape_error);
    if (shape == nullptr) {
        spdlog::error("Failed to create Jolt shape: {}", shape_error);
        return std::make_unique<PhysicsRigidBodyJolt>();
    }

    EMotionType motion_type = toJph(desc.motion);
    if (shape->MustBeStatic() && motion_type != EMotionType::Static) {
        spdlog::warn("Jolt shape requires a static body; forcing motion type to static");
        motion_type = EMotionType::Static;
    }

    const bool moving = motion_type != EMotionType::Static;
    BodyCreationSettings settings(shape.GetPtr(),
                                  RVec3(desc.position.x, desc.position.y, desc.position.z),
                                  toJph(desc.rotation),
                                  motion_type,
                                  moving ? Moving : NonMoving);
    settings.mLinearVelocity = toJph(desc.linear_velocity);
    settings.mAngularVelocity = toJph(desc.angular_velocity);
    settings.mUserData = desc.user_data;
    settings.mAllowedDOFs = toAllowedDofs(desc.allowed_dofs);
    settings.mAllowDynamicOrKinematic = desc.allow_dynamic_or_kinematic;
    settings.mIsSensor = desc.sensor;
    settings.mCollideKinematicVsNonDynamic = desc.collide_kinematic_vs_non_dynamic;
    settings.mUseManifoldReduction = desc.use_manifold_reduction;
    settings.mApplyGyroscopicForce = desc.apply_gyroscopic_force;
    settings.mMotionQuality = toJph(desc.motion_quality);
    settings.mEnhancedInternalEdgeRemoval = desc.enhanced_internal_edge_removal;
    settings.mAllowSleeping = desc.allow_sleeping;
    settings.mFriction = desc.material.friction;
    settings.mRestitution = desc.material.restitution;
    settings.mLinearDamping = desc.linear_damping;
    settings.mAngularDamping = desc.angular_damping;
    settings.mMaxLinearVelocity = desc.max_linear_velocity;
    settings.mMaxAngularVelocity = desc.max_angular_velocity;
    settings.mGravityFactor = desc.gravity_factor;
    settings.mNumVelocityStepsOverride = static_cast<JPH::uint>(desc.velocity_solver_steps);
    settings.mNumPositionStepsOverride = static_cast<JPH::uint>(desc.position_solver_steps);
    settings.mInertiaMultiplier = desc.inertia_multiplier;

    if (moving) {
        settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = std::max(desc.mass, 0.001f);
    }

    BodyInterface& bi = physicsSystem_->GetBodyInterface();
    Body* body = bi.CreateBody(settings);
    if (!body) {
        spdlog::error("Failed to create Jolt body");
        return std::make_unique<PhysicsRigidBodyJolt>();
    }

    registerBodyFilter(body->GetID(), desc.collision_filter);
    const EActivation activation =
        moving && desc.activate ? EActivation::Activate : EActivation::DontActivate;
    bi.AddBody(body->GetID(), activation);
    return std::make_unique<PhysicsRigidBodyJolt>(this, body->GetID());
}

std::unique_ptr<PhysicsConstraintBackend> PhysicsWorldJolt::createConstraint(
    const karma::physics::PhysicsConstraintDesc& desc,
    std::uintptr_t bodyA,
    std::uintptr_t bodyB) {
    if (!physicsSystem_ || bodyA == 0 || bodyB == 0) {
        return std::make_unique<PhysicsConstraintJolt>();
    }

    const BodyID body_a(static_cast<uint32_t>(bodyA));
    const BodyID body_b(static_cast<uint32_t>(bodyB));
    if (body_a.IsInvalid() || body_b.IsInvalid()) {
        return std::make_unique<PhysicsConstraintJolt>();
    }

    auto create = [&](const TwoBodyConstraintSettings& settings) -> TwoBodyConstraint* {
        return physicsSystem_->GetBodyInterface().CreateConstraint(&settings, body_a, body_b);
    };

    TwoBodyConstraint* constraint = nullptr;
    switch (desc.type) {
    case karma::physics::PhysicsConstraintType::Fixed: {
        FixedConstraintSettings settings;
        fillConstraintBase(settings, desc);
        settings.mSpace = toJph(desc.space);
        settings.mAutoDetectPoint = desc.auto_detect_point;
        settings.mPoint1 = RVec3(desc.point1.x, desc.point1.y, desc.point1.z);
        settings.mPoint2 = RVec3(desc.point2.x, desc.point2.y, desc.point2.z);
        settings.mAxisX1 = normalizedOr(toJph(desc.axis1), Vec3::sAxisX());
        settings.mAxisY1 = normalizedOr(toJph(desc.normal1), Vec3::sAxisY());
        settings.mAxisX2 = normalizedOr(toJph(desc.axis2), Vec3::sAxisX());
        settings.mAxisY2 = normalizedOr(toJph(desc.normal2), Vec3::sAxisY());
        constraint = create(settings);
        break;
    }
    case karma::physics::PhysicsConstraintType::Point: {
        PointConstraintSettings settings;
        fillConstraintBase(settings, desc);
        settings.mSpace = toJph(desc.space);
        settings.mPoint1 = RVec3(desc.point1.x, desc.point1.y, desc.point1.z);
        settings.mPoint2 = RVec3(desc.point2.x, desc.point2.y, desc.point2.z);
        constraint = create(settings);
        break;
    }
    case karma::physics::PhysicsConstraintType::Distance: {
        DistanceConstraintSettings settings;
        fillConstraintBase(settings, desc);
        settings.mSpace = toJph(desc.space);
        settings.mPoint1 = RVec3(desc.point1.x, desc.point1.y, desc.point1.z);
        settings.mPoint2 = RVec3(desc.point2.x, desc.point2.y, desc.point2.z);
        settings.mMinDistance = desc.min_distance;
        settings.mMaxDistance = desc.max_distance;
        settings.mLimitsSpringSettings = toJph(desc.limit_spring);
        constraint = create(settings);
        break;
    }
    case karma::physics::PhysicsConstraintType::Hinge: {
        HingeConstraintSettings settings;
        fillConstraintBase(settings, desc);
        settings.mSpace = toJph(desc.space);
        settings.mPoint1 = RVec3(desc.point1.x, desc.point1.y, desc.point1.z);
        settings.mPoint2 = RVec3(desc.point2.x, desc.point2.y, desc.point2.z);
        settings.mHingeAxis1 = normalizedOr(toJph(desc.axis1), Vec3::sAxisY());
        settings.mHingeAxis2 = normalizedOr(toJph(desc.axis2), Vec3::sAxisY());
        settings.mNormalAxis1 = normalizedOr(toJph(desc.normal1), Vec3::sAxisX());
        settings.mNormalAxis2 = normalizedOr(toJph(desc.normal2), Vec3::sAxisX());
        settings.mLimitsMin = desc.limits_min;
        settings.mLimitsMax = desc.limits_max;
        settings.mLimitsSpringSettings = toJph(desc.limit_spring);
        settings.mMaxFrictionTorque = desc.max_friction_torque;
        constraint = create(settings);
        break;
    }
    case karma::physics::PhysicsConstraintType::Slider: {
        SliderConstraintSettings settings;
        fillConstraintBase(settings, desc);
        settings.mSpace = toJph(desc.space);
        settings.mAutoDetectPoint = desc.auto_detect_point;
        settings.mPoint1 = RVec3(desc.point1.x, desc.point1.y, desc.point1.z);
        settings.mPoint2 = RVec3(desc.point2.x, desc.point2.y, desc.point2.z);
        settings.mSliderAxis1 = normalizedOr(toJph(desc.axis1), Vec3::sAxisX());
        settings.mSliderAxis2 = normalizedOr(toJph(desc.axis2), Vec3::sAxisX());
        settings.mNormalAxis1 = normalizedOr(toJph(desc.normal1), Vec3::sAxisY());
        settings.mNormalAxis2 = normalizedOr(toJph(desc.normal2), Vec3::sAxisY());
        settings.mLimitsMin = desc.limits_min;
        settings.mLimitsMax = desc.limits_max;
        settings.mLimitsSpringSettings = toJph(desc.limit_spring);
        settings.mMaxFrictionForce = desc.max_friction_force;
        constraint = create(settings);
        break;
    }
    case karma::physics::PhysicsConstraintType::Cone: {
        ConeConstraintSettings settings;
        fillConstraintBase(settings, desc);
        settings.mSpace = toJph(desc.space);
        settings.mPoint1 = RVec3(desc.point1.x, desc.point1.y, desc.point1.z);
        settings.mPoint2 = RVec3(desc.point2.x, desc.point2.y, desc.point2.z);
        settings.mTwistAxis1 = normalizedOr(toJph(desc.axis1), Vec3::sAxisX());
        settings.mTwistAxis2 = normalizedOr(toJph(desc.axis2), Vec3::sAxisX());
        settings.mHalfConeAngle = desc.half_cone_angle;
        constraint = create(settings);
        break;
    }
    case karma::physics::PhysicsConstraintType::SwingTwist: {
        SwingTwistConstraintSettings settings;
        fillConstraintBase(settings, desc);
        settings.mSpace = toJph(desc.space);
        settings.mPosition1 = RVec3(desc.point1.x, desc.point1.y, desc.point1.z);
        settings.mPosition2 = RVec3(desc.point2.x, desc.point2.y, desc.point2.z);
        settings.mTwistAxis1 = normalizedOr(toJph(desc.axis1), Vec3::sAxisX());
        settings.mTwistAxis2 = normalizedOr(toJph(desc.axis2), Vec3::sAxisX());
        settings.mPlaneAxis1 = normalizedOr(toJph(desc.plane_axis1), Vec3::sAxisY());
        settings.mPlaneAxis2 = normalizedOr(toJph(desc.plane_axis2), Vec3::sAxisY());
        settings.mNormalHalfConeAngle = desc.normal_half_cone_angle;
        settings.mPlaneHalfConeAngle = desc.plane_half_cone_angle;
        settings.mTwistMinAngle = desc.twist_min_angle;
        settings.mTwistMaxAngle = desc.twist_max_angle;
        settings.mMaxFrictionTorque = desc.max_friction_torque;
        constraint = create(settings);
        break;
    }
    case karma::physics::PhysicsConstraintType::SixDof: {
        SixDOFConstraintSettings settings;
        fillConstraintBase(settings, desc);
        settings.mSpace = toJph(desc.space);
        settings.mPosition1 = RVec3(desc.point1.x, desc.point1.y, desc.point1.z);
        settings.mPosition2 = RVec3(desc.point2.x, desc.point2.y, desc.point2.z);
        settings.mAxisX1 = normalizedOr(toJph(desc.axis1), Vec3::sAxisX());
        settings.mAxisX2 = normalizedOr(toJph(desc.axis2), Vec3::sAxisX());
        settings.mAxisY1 = normalizedOr(toJph(desc.normal1), Vec3::sAxisY());
        settings.mAxisY2 = normalizedOr(toJph(desc.normal2), Vec3::sAxisY());
        for (int i = 0; i < SixDOFConstraintSettings::EAxis::Num; ++i) {
            settings.SetLimitedAxis(static_cast<SixDOFConstraintSettings::EAxis>(i),
                                    desc.six_dof_min_limits[static_cast<size_t>(i)],
                                    desc.six_dof_max_limits[static_cast<size_t>(i)]);
            settings.mMaxFriction[i] = desc.six_dof_max_friction[static_cast<size_t>(i)];
        }
        for (int i = 0; i < SixDOFConstraintSettings::EAxis::NumTranslation; ++i) {
            settings.mLimitsSpringSettings[i] = toJph(desc.limit_spring);
        }
        constraint = create(settings);
        break;
    }
    }

    if (constraint == nullptr) {
        spdlog::error("Failed to create Jolt constraint");
        return std::make_unique<PhysicsConstraintJolt>();
    }

    physicsSystem_->AddConstraint(constraint);
    physicsSystem_->GetBodyInterface().ActivateBody(body_a);
    physicsSystem_->GetBodyInterface().ActivateBody(body_b);
    return std::make_unique<PhysicsConstraintJolt>(this, constraint);
}

std::unique_ptr<PhysicsRigidBodyBackend> PhysicsWorldJolt::createBoxBody(const glm::vec3& halfExtents,
                                                                         float mass,
                                                                         const glm::vec3& position,
                                                                         const karma::physics::PhysicsMaterial& material) {
    karma::physics::PhysicsBodyDesc desc;
    desc.shape.type = karma::physics::PhysicsShapeType::Box;
    desc.shape.half_extents = halfExtents;
    desc.mass = mass;
    desc.position = position;
    desc.material = material;
    desc.motion = mass > 0.0f ? karma::physics::PhysicsMotionType::Dynamic
                              : karma::physics::PhysicsMotionType::Static;
    return createBody(desc);
}

std::unique_ptr<PhysicsCharacterControllerBackend> PhysicsWorldJolt::createCharacterController(const glm::vec3& size) {
    const glm::vec3 halfExtents = size * 0.5f;
    auto controller = std::make_unique<PhysicsCharacterControllerJolt>(this, halfExtents, glm::vec3(0.0f, 2.0f, 0.0f));
    controller->setRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    controller->setVelocity(glm::vec3(0.0f));
    controller->setAngularVelocity(glm::vec3(0.0f));
    spdlog::info("Created kinematic character controller");
    return controller;
}

bool PhysicsWorldJolt::raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& hitPoint, glm::vec3& hitNormal) const {
    if (!physicsSystem_) return false;

    RVec3 origin(from.x, from.y, from.z);
    Vec3 direction(to.x - from.x, to.y - from.y, to.z - from.z);
    RRayCast ray(origin, direction);
    RayCastResult result;
    if (!physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, result)) {
        return false;
    }

    glm::vec3 rayVec = glm::vec3(direction.GetX(), direction.GetY(), direction.GetZ());
    hitPoint = glm::vec3(from) + rayVec * result.mFraction;

    BodyLockRead lock(physicsSystem_->GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded()) {
        hitNormal = toGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction)));
    } else {
        hitNormal = glm::vec3(0.0f);
    }
    return true;
}

bool PhysicsWorldJolt::raycastDetailed(const glm::vec3& from,
                                       const glm::vec3& to,
                                       karma::physics::PhysicsGroundContact& outHit) const {
    if (!physicsSystem_) return false;

    RVec3 origin(from.x, from.y, from.z);
    Vec3 direction(to.x - from.x, to.y - from.y, to.z - from.z);
    RRayCast ray(origin, direction);
    RayCastResult result;
    if (!physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, result)) {
        return false;
    }

    outHit.grounded = true;
    outHit.point = glm::vec3(from) +
                   glm::vec3(direction.GetX(), direction.GetY(), direction.GetZ()) * result.mFraction;
    outHit.support_handle = static_cast<std::uintptr_t>(result.mBodyID.GetIndexAndSequenceNumber());

    BodyLockRead lock(physicsSystem_->GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded()) {
        outHit.normal = toGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(
            result.mSubShapeID2, ray.GetPointOnRay(result.mFraction)));
    } else {
        outHit.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return true;
}

void PhysicsWorldJolt::collectContacts(std::vector<karma::physics::PhysicsContact>& outContacts) const {
    std::lock_guard<std::mutex> lock(contacts_mutex_);
    outContacts.reserve(outContacts.size() + contacts_.size());
    for (const auto& [key, contact] : contacts_) {
        (void)key;
        outContacts.push_back(contact);
    }
}

bool PhysicsWorldJolt::bodiesShouldCollide(const JPH::Body& bodyA, const JPH::Body& bodyB) const {
    auto lookup_filter = [&](const JPH::Body& body) {
        const uint32_t key = body.GetID().GetIndexAndSequenceNumber();
        auto it = body_filters_.find(key);
        return it != body_filters_.end() ? it->second : karma::physics::PhysicsCollisionFilter{};
    };

    std::lock_guard<std::mutex> lock(body_filters_mutex_);
    const karma::physics::PhysicsCollisionFilter filter_a = lookup_filter(bodyA);
    const karma::physics::PhysicsCollisionFilter filter_b = lookup_filter(bodyB);
    return (filter_a.layers & filter_b.collides_with) != 0u &&
           (filter_b.layers & filter_a.collides_with) != 0u;
}

void PhysicsWorldJolt::registerBodyFilter(const JPH::BodyID& id,
                                          karma::physics::PhysicsCollisionFilter filter) {
    std::lock_guard<std::mutex> lock(body_filters_mutex_);
    body_filters_[id.GetIndexAndSequenceNumber()] = filter;
}

void PhysicsWorldJolt::unregisterBodyFilter(const JPH::BodyID& id) {
    std::lock_guard<std::mutex> lock(body_filters_mutex_);
    body_filters_.erase(id.GetIndexAndSequenceNumber());
}

void PhysicsWorldJolt::removeBody(const JPH::BodyID& id) {
    if (!physicsSystem_) return;
    unregisterBodyFilter(id);
    BodyInterface& bi = physicsSystem_->GetBodyInterface();
    if (!bi.IsAdded(id)) return;

    bi.RemoveBody(id);
    bi.DestroyBody(id);
}

void PhysicsWorldJolt::removeConstraint(JPH::Constraint* constraint) const {
    if (!physicsSystem_ || constraint == nullptr) return;
    physicsSystem_->RemoveConstraint(constraint);
}

} // namespace karma::physics::backend
