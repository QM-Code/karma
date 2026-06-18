#include "karma/simulation/physics/backends/jolt/vehicle_jolt.hpp"

#include "karma/simulation/physics/backends/jolt/physics_world_jolt.hpp"
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Vehicle/MotorcycleController.h>
#include <Jolt/Physics/Vehicle/TrackedVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleAntiRollBar.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace {

using ObjectLayer = JPH::ObjectLayer;
constexpr ObjectLayer NonMoving = 0;
constexpr ObjectLayer Moving = 1;

JPH::Vec3 toJph(const glm::vec3& value) {
    return JPH::Vec3(value.x, value.y, value.z);
}

glm::vec3 toGlm(const JPH::Vec3& value) {
    return {value.GetX(), value.GetY(), value.GetZ()};
}

glm::vec3 toGlmRVec(const JPH::RVec3& value) {
    return {
        static_cast<float>(value.GetX()),
        static_cast<float>(value.GetY()),
        static_cast<float>(value.GetZ()),
    };
}

JPH::ESpringMode toJph(karma::physics::PhysicsSpringMode mode) {
    switch (mode) {
    case karma::physics::PhysicsSpringMode::FrequencyAndDamping:
        return JPH::ESpringMode::FrequencyAndDamping;
    case karma::physics::PhysicsSpringMode::StiffnessAndDamping:
        return JPH::ESpringMode::StiffnessAndDamping;
    }
    return JPH::ESpringMode::FrequencyAndDamping;
}

JPH::ETransmissionMode toJph(karma::physics::PhysicsVehicleTransmissionMode mode) {
    switch (mode) {
    case karma::physics::PhysicsVehicleTransmissionMode::Automatic:
        return JPH::ETransmissionMode::Auto;
    case karma::physics::PhysicsVehicleTransmissionMode::Manual:
        return JPH::ETransmissionMode::Manual;
    }
    return JPH::ETransmissionMode::Auto;
}

void fillSpring(JPH::SpringSettings& out, const karma::physics::PhysicsSpringSettings& in) {
    out.mMode = toJph(in.mode);
    out.mFrequency = in.frequency_or_stiffness;
    out.mStiffness = in.frequency_or_stiffness;
    out.mDamping = in.damping;
}

void fillCurve(JPH::LinearCurve& out, const std::vector<karma::physics::PhysicsVehicleCurvePoint>& points) {
    if (points.empty()) {
        return;
    }
    out.Clear();
    out.Reserve(static_cast<JPH::uint>(points.size()));
    for (const auto& point : points) {
        out.AddPoint(point.x, point.y);
    }
    out.Sort();
}

void fillEngine(JPH::VehicleEngineSettings& out, const karma::physics::PhysicsVehicleEngineDesc& in) {
    out.mMaxTorque = in.max_torque;
    out.mMinRPM = in.min_rpm;
    out.mMaxRPM = in.max_rpm;
    out.mInertia = in.inertia;
    out.mAngularDamping = in.angular_damping;
    fillCurve(out.mNormalizedTorque, in.normalized_torque);
}

void fillTransmission(JPH::VehicleTransmissionSettings& out,
                      const karma::physics::PhysicsVehicleTransmissionDesc& in) {
    out.mMode = toJph(in.mode);
    out.mGearRatios.clear();
    out.mGearRatios.reserve(in.gear_ratios.size());
    for (float ratio : in.gear_ratios) {
        out.mGearRatios.push_back(ratio);
    }
    out.mReverseGearRatios.clear();
    out.mReverseGearRatios.reserve(in.reverse_gear_ratios.size());
    for (float ratio : in.reverse_gear_ratios) {
        out.mReverseGearRatios.push_back(ratio);
    }
    out.mSwitchTime = in.switch_time;
    out.mClutchReleaseTime = in.clutch_release_time;
    out.mSwitchLatency = in.switch_latency;
    out.mShiftUpRPM = in.shift_up_rpm;
    out.mShiftDownRPM = in.shift_down_rpm;
    out.mClutchStrength = in.clutch_strength;
}

void fillWheelBase(JPH::WheelSettings& out, const karma::physics::PhysicsVehicleWheelDesc& in) {
    out.mPosition = toJph(in.position);
    out.mSuspensionForcePoint = toJph(in.suspension_force_point);
    out.mSuspensionDirection = toJph(in.suspension_direction);
    out.mSteeringAxis = toJph(in.steering_axis);
    out.mWheelUp = toJph(in.wheel_up);
    out.mWheelForward = toJph(in.wheel_forward);
    out.mSuspensionMinLength = in.suspension_min_length;
    out.mSuspensionMaxLength = in.suspension_max_length;
    out.mSuspensionPreloadLength = in.suspension_preload_length;
    fillSpring(out.mSuspensionSpring, in.suspension_spring);
    out.mRadius = in.radius;
    out.mWidth = in.width;
    out.mEnableSuspensionForcePoint = in.enable_suspension_force_point;
}

JPH::Ref<JPH::WheelSettings> makeWheelSettings(const karma::physics::PhysicsVehicleWheelDesc& desc,
                                               karma::physics::PhysicsVehicleControllerType controller) {
    if (controller == karma::physics::PhysicsVehicleControllerType::Tracked) {
        auto* wheel = new JPH::WheelSettingsTV();
        fillWheelBase(*wheel, desc);
        wheel->mLongitudinalFriction = desc.tracked_longitudinal_friction;
        wheel->mLateralFriction = desc.tracked_lateral_friction;
        return wheel;
    }

    auto* wheel = new JPH::WheelSettingsWV();
    fillWheelBase(*wheel, desc);
    wheel->mInertia = desc.inertia;
    wheel->mAngularDamping = desc.angular_damping;
    wheel->mMaxSteerAngle = desc.max_steer_angle;
    fillCurve(wheel->mLongitudinalFriction, desc.longitudinal_friction);
    fillCurve(wheel->mLateralFriction, desc.lateral_friction);
    wheel->mMaxBrakeTorque = desc.max_brake_torque;
    wheel->mMaxHandBrakeTorque = desc.max_hand_brake_torque;
    return wheel;
}

void fillDifferential(JPH::VehicleDifferentialSettings& out,
                      const karma::physics::PhysicsVehicleDifferentialDesc& in) {
    out.mLeftWheel = in.left_wheel;
    out.mRightWheel = in.right_wheel;
    out.mDifferentialRatio = in.differential_ratio;
    out.mLeftRightSplit = in.left_right_split;
    out.mLimitedSlipRatio = in.limited_slip_ratio;
    out.mEngineTorqueRatio = in.engine_torque_ratio;
}

void fillTrack(JPH::VehicleTrackSettings& out, const karma::physics::PhysicsVehicleTrackDesc& in) {
    out.mDrivenWheel = in.driven_wheel;
    out.mWheels.clear();
    out.mWheels.reserve(in.wheels.size());
    for (uint32_t wheel : in.wheels) {
        out.mWheels.push_back(wheel);
    }
    out.mInertia = in.inertia;
    out.mAngularDamping = in.angular_damping;
    out.mMaxBrakeTorque = in.max_brake_torque;
    out.mDifferentialRatio = in.differential_ratio;
}

JPH::Ref<JPH::VehicleControllerSettings> makeControllerSettings(
    const karma::physics::PhysicsVehicleDesc& desc) {
    if (desc.controller == karma::physics::PhysicsVehicleControllerType::Tracked) {
        auto* settings = new JPH::TrackedVehicleControllerSettings();
        fillEngine(settings->mEngine, desc.engine);
        fillTransmission(settings->mTransmission, desc.transmission);
        fillTrack(settings->mTracks[static_cast<int>(JPH::ETrackSide::Left)], desc.tracks[0]);
        fillTrack(settings->mTracks[static_cast<int>(JPH::ETrackSide::Right)], desc.tracks[1]);
        return settings;
    }

    if (desc.controller == karma::physics::PhysicsVehicleControllerType::Motorcycle) {
        auto* settings = new JPH::MotorcycleControllerSettings();
        fillEngine(settings->mEngine, desc.engine);
        fillTransmission(settings->mTransmission, desc.transmission);
        for (const auto& differential_desc : desc.differentials) {
            JPH::VehicleDifferentialSettings differential;
            fillDifferential(differential, differential_desc);
            settings->mDifferentials.push_back(differential);
        }
        settings->mDifferentialLimitedSlipRatio = desc.differential_limited_slip_ratio;
        settings->mMaxLeanAngle = desc.motorcycle.max_lean_angle;
        settings->mLeanSpringConstant = desc.motorcycle.lean_spring_constant;
        settings->mLeanSpringDamping = desc.motorcycle.lean_spring_damping;
        settings->mLeanSpringIntegrationCoefficient =
            desc.motorcycle.lean_spring_integration_coefficient;
        settings->mLeanSpringIntegrationCoefficientDecay =
            desc.motorcycle.lean_spring_integration_decay;
        settings->mLeanSmoothingFactor = desc.motorcycle.lean_smoothing_factor;
        return settings;
    }

    auto* settings = new JPH::WheeledVehicleControllerSettings();
    fillEngine(settings->mEngine, desc.engine);
    fillTransmission(settings->mTransmission, desc.transmission);
    for (const auto& differential_desc : desc.differentials) {
        JPH::VehicleDifferentialSettings differential;
        fillDifferential(differential, differential_desc);
        settings->mDifferentials.push_back(differential);
    }
    settings->mDifferentialLimitedSlipRatio = desc.differential_limited_slip_ratio;
    return settings;
}

JPH::VehicleCollisionTester* makeCollisionTester(const karma::physics::PhysicsVehicleDesc& desc) {
    const ObjectLayer object_layer = desc.collision_test_layer == 0u ? NonMoving : Moving;
    switch (desc.collision_tester) {
    case karma::physics::PhysicsVehicleCollisionTesterType::Ray:
        return new JPH::VehicleCollisionTesterRay(object_layer,
                                                  toJph(desc.up),
                                                  desc.collision_test_max_slope_angle);
    case karma::physics::PhysicsVehicleCollisionTesterType::SphereCast:
        return new JPH::VehicleCollisionTesterCastSphere(object_layer,
                                                         desc.collision_test_sphere_radius,
                                                         toJph(desc.up),
                                                         desc.collision_test_max_slope_angle);
    case karma::physics::PhysicsVehicleCollisionTesterType::CylinderCast:
        return new JPH::VehicleCollisionTesterCastCylinder(
            object_layer,
            desc.collision_test_cylinder_convex_radius_fraction);
    }
    return new JPH::VehicleCollisionTesterRay(object_layer,
                                              toJph(desc.up),
                                              desc.collision_test_max_slope_angle);
}

void fillWheelState(karma::physics::PhysicsVehicleWheelState& out, const JPH::Wheel& wheel) {
    out.has_contact = wheel.HasContact();
    out.contact_body = out.has_contact
        ? static_cast<std::uintptr_t>(wheel.GetContactBodyID().GetIndexAndSequenceNumber())
        : 0;
    if (out.has_contact) {
        out.contact_position = toGlmRVec(wheel.GetContactPosition());
        out.contact_normal = toGlm(wheel.GetContactNormal());
        out.contact_longitudinal = toGlm(wheel.GetContactLongitudinal());
        out.contact_lateral = toGlm(wheel.GetContactLateral());
    }
    out.suspension_length = wheel.GetSuspensionLength();
    out.suspension_lambda = wheel.GetSuspensionLambda();
    out.longitudinal_lambda = wheel.GetLongitudinalLambda();
    out.lateral_lambda = wheel.GetLateralLambda();
    out.steer_angle = wheel.GetSteerAngle();
    out.rotation_angle = wheel.GetRotationAngle();
    out.angular_velocity = wheel.GetAngularVelocity();
}

float nonZeroRatio(float value) {
    if (std::abs(value) > 0.0001f) {
        return std::clamp(value, -1.0f, 1.0f);
    }
    return value < 0.0f ? -0.0001f : 0.0001f;
}

}  // namespace

namespace karma::physics_backend {

PhysicsVehicleJolt::PhysicsVehicleJolt(PhysicsWorldJolt* world,
                                       JPH::VehicleConstraint* vehicle,
                                       JPH::VehicleCollisionTester* tester,
                                       karma::physics::PhysicsVehicleControllerType controller,
                                       bool manualTransmission)
    : world_(world),
      vehicle_(vehicle),
      tester_(tester),
      controller_(controller),
      manual_transmission_(manualTransmission) {}

PhysicsVehicleJolt::~PhysicsVehicleJolt() {
    destroy();
}

bool PhysicsVehicleJolt::isValid() const {
    return world_ != nullptr && vehicle_ != nullptr;
}

void PhysicsVehicleJolt::setInput(const karma::physics::PhysicsVehicleInput& input) {
    if (!isValid()) {
        return;
    }

    if (controller_ == karma::physics::PhysicsVehicleControllerType::Tracked) {
        auto* controller = static_cast<JPH::TrackedVehicleController*>(vehicle_->GetController());
        controller->SetDriverInput(std::clamp(input.forward, -1.0f, 1.0f),
                                   nonZeroRatio(input.left_ratio),
                                   nonZeroRatio(input.right_ratio),
                                   std::clamp(input.brake, 0.0f, 1.0f));
        if (manual_transmission_) {
            controller->GetTransmission().Set(input.current_gear,
                                              std::clamp(input.clutch_friction, 0.0f, 1.0f));
        }
        return;
    }

    auto* controller = static_cast<JPH::WheeledVehicleController*>(vehicle_->GetController());
    controller->SetDriverInput(std::clamp(input.forward, -1.0f, 1.0f),
                               std::clamp(input.right, -1.0f, 1.0f),
                               std::clamp(input.brake, 0.0f, 1.0f),
                               std::clamp(input.hand_brake, 0.0f, 1.0f));
    if (manual_transmission_) {
        controller->GetTransmission().Set(input.current_gear,
                                          std::clamp(input.clutch_friction, 0.0f, 1.0f));
    }
}

karma::physics::PhysicsVehicleState PhysicsVehicleJolt::getState() const {
    karma::physics::PhysicsVehicleState state;
    if (!isValid()) {
        return state;
    }

    state.valid = true;
    state.active = vehicle_->IsActive();
    state.handle = nativeHandle();

    const JPH::Wheels& wheels = vehicle_->GetWheels();
    state.wheels.resize(wheels.size());
    for (size_t i = 0; i < wheels.size(); ++i) {
        if (wheels[i] != nullptr) {
            fillWheelState(state.wheels[i], *wheels[i]);
        }
    }

    if (controller_ == karma::physics::PhysicsVehicleControllerType::Tracked) {
        const auto* controller = static_cast<const JPH::TrackedVehicleController*>(vehicle_->GetController());
        state.engine_rpm = controller->GetEngine().GetCurrentRPM();
        state.current_gear = controller->GetTransmission().GetCurrentGear();
        state.clutch_friction = controller->GetTransmission().GetClutchFriction();
        state.switching_gear = controller->GetTransmission().IsSwitchingGear();
        const auto& tracks = controller->GetTracks();
        state.tracked_left_angular_velocity =
            tracks[static_cast<int>(JPH::ETrackSide::Left)].mAngularVelocity;
        state.tracked_right_angular_velocity =
            tracks[static_cast<int>(JPH::ETrackSide::Right)].mAngularVelocity;
        return state;
    }

    const auto* controller = static_cast<const JPH::WheeledVehicleController*>(vehicle_->GetController());
    state.engine_rpm = controller->GetEngine().GetCurrentRPM();
    state.current_gear = controller->GetTransmission().GetCurrentGear();
    state.clutch_friction = controller->GetTransmission().GetClutchFriction();
    state.switching_gear = controller->GetTransmission().IsSwitchingGear();
    state.wheel_speed_at_clutch = controller->GetWheelSpeedAtClutch();
    return state;
}

void PhysicsVehicleJolt::setEnabled(bool enabled) {
    if (vehicle_ != nullptr) {
        vehicle_->SetEnabled(enabled);
    }
}

void PhysicsVehicleJolt::destroy() {
    if (world_ != nullptr && vehicle_ != nullptr) {
        world_->removeVehicle(vehicle_.GetPtr());
    }
    vehicle_ = nullptr;
    tester_ = nullptr;
    world_ = nullptr;
}

std::uintptr_t PhysicsVehicleJolt::nativeHandle() const {
    return vehicle_ != nullptr
        ? reinterpret_cast<std::uintptr_t>(vehicle_.GetPtr())
        : 0;
}

std::unique_ptr<PhysicsVehicleBackend> PhysicsWorldJolt::createVehicle(
    const karma::physics::PhysicsVehicleDesc& desc,
    std::uintptr_t body) {
    if (physicsSystem() == nullptr || body == 0 || desc.wheels.empty()) {
        return std::make_unique<PhysicsVehicleJolt>();
    }

    const JPH::BodyID body_id(static_cast<JPH::uint32>(body));
    if (body_id.IsInvalid()) {
        return std::make_unique<PhysicsVehicleJolt>();
    }

    JPH::VehicleConstraintSettings settings;
    settings.mEnabled = true;
    settings.mConstraintPriority = desc.priority;
    settings.mNumVelocityStepsOverride = static_cast<JPH::uint>(desc.velocity_solver_steps);
    settings.mNumPositionStepsOverride = static_cast<JPH::uint>(desc.position_solver_steps);
    settings.mDrawConstraintSize = desc.draw_size;
    settings.mUserData = desc.user_data;
    settings.mUp = toJph(desc.up);
    settings.mForward = toJph(desc.forward);
    settings.mMaxPitchRollAngle = desc.max_pitch_roll_angle;

    settings.mWheels.reserve(desc.wheels.size());
    for (const auto& wheel : desc.wheels) {
        settings.mWheels.push_back(makeWheelSettings(wheel, desc.controller));
    }

    settings.mAntiRollBars.reserve(desc.anti_roll_bars.size());
    for (const auto& anti_roll_bar_desc : desc.anti_roll_bars) {
        JPH::VehicleAntiRollBar anti_roll_bar;
        anti_roll_bar.mLeftWheel = anti_roll_bar_desc.left_wheel;
        anti_roll_bar.mRightWheel = anti_roll_bar_desc.right_wheel;
        anti_roll_bar.mStiffness = anti_roll_bar_desc.stiffness;
        settings.mAntiRollBars.push_back(anti_roll_bar);
    }

    settings.mController = makeControllerSettings(desc);

    JPH::VehicleConstraint* vehicle = nullptr;
    {
        JPH::BodyLockWrite lock(physicsSystem()->GetBodyLockInterface(), body_id);
        if (!lock.Succeeded()) {
            spdlog::warn("Failed to create Jolt vehicle: body handle is not valid");
            return std::make_unique<PhysicsVehicleJolt>();
        }
        vehicle = new JPH::VehicleConstraint(lock.GetBody(), settings);
    }

    auto* tester = makeCollisionTester(desc);
    vehicle->SetVehicleCollisionTester(tester);
    vehicle->SetNumStepsBetweenCollisionTestActive(desc.num_steps_between_collision_test_active);
    vehicle->SetNumStepsBetweenCollisionTestInactive(desc.num_steps_between_collision_test_inactive);
    if (desc.override_gravity) {
        vehicle->OverrideGravity(toJph(desc.gravity));
    }

    if (desc.controller == karma::physics::PhysicsVehicleControllerType::Motorcycle) {
        auto* controller = static_cast<JPH::MotorcycleController*>(vehicle->GetController());
        controller->EnableLeanController(desc.motorcycle.enable_lean_controller);
        controller->EnableLeanSteeringLimit(desc.motorcycle.enable_lean_steering_limit);
    }

    physicsSystem()->AddConstraint(vehicle);
    physicsSystem()->AddStepListener(vehicle);
    physicsSystem()->GetBodyInterface().ActivateBody(body_id);

    const bool manual =
        desc.transmission.mode == karma::physics::PhysicsVehicleTransmissionMode::Manual;
    return std::make_unique<PhysicsVehicleJolt>(this, vehicle, tester, desc.controller, manual);
}

void PhysicsWorldJolt::removeVehicle(JPH::VehicleConstraint* constraint) {
    if (physicsSystem() == nullptr || constraint == nullptr) {
        return;
    }
    physicsSystem()->RemoveStepListener(constraint);
    physicsSystem()->RemoveConstraint(constraint);
}

}  // namespace karma::physics_backend
