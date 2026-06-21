#pragma once

#include "karma/math.h"
#include "karma/world.h"
#include "karma/components.h"



#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace karma::physics {

/// \ingroup karma_physics
/// Basic physics material parameters passed to backend body creation.
struct PhysicsMaterial {
    float friction = 0.2f;
    float restitution = 0.0f;
};

/// \ingroup karma_physics
/// Collision layer/mask pair used by the backend collision filter.
struct PhysicsCollisionFilter {
    uint32_t layers = 1u;
    uint32_t collides_with = 0xFFFFFFFFu;
};

/// \ingroup karma_physics
/// Runtime shape kinds accepted by the physics backend.
enum class PhysicsShapeType : uint8_t {
    Box,
    Sphere,
    Capsule,
    Cylinder,
    TaperedCapsule,
    ConvexHull,
    Triangle,
    Mesh,
    HeightField,
    Compound,
};

/// \ingroup karma_physics
/// Rigid body motion model.
enum class PhysicsMotionType : uint8_t {
    Static,
    Kinematic,
    Dynamic,
};

/// \ingroup karma_physics
/// Continuous collision detection mode for moving bodies.
enum class PhysicsMotionQuality : uint8_t {
    Discrete,
    LinearCast,
};

/// \ingroup karma_physics
/// Back-face handling for ray and shape queries.
enum class PhysicsBackFaceMode : uint8_t {
    Ignore,
    Collide,
};

/// \ingroup karma_physics
/// Two-body constraint type.
enum class PhysicsConstraintType : uint8_t {
    Fixed,
    Point,
    Distance,
    Hinge,
    Slider,
    Cone,
    SwingTwist,
    SixDof,
};

/// \ingroup karma_physics
/// Vehicle controller model.
enum class PhysicsVehicleControllerType : uint8_t {
    Wheeled,
    Motorcycle,
    Tracked,
};

/// \ingroup karma_physics
/// Wheel-ground collision query used by a vehicle.
enum class PhysicsVehicleCollisionTesterType : uint8_t {
    Ray,
    SphereCast,
    CylinderCast,
};

/// \ingroup karma_physics
/// Transmission shifting mode.
enum class PhysicsVehicleTransmissionMode : uint8_t {
    Automatic,
    Manual,
};

/// \ingroup karma_physics
/// Generated bend constraint type for soft bodies.
enum class PhysicsSoftBodyBendType : uint8_t {
    None,
    Distance,
    Dihedral,
};

/// \ingroup karma_physics
/// Long-range attachment generation mode for soft-body vertices.
enum class PhysicsSoftBodyLraType : uint8_t {
    None,
    EuclideanDistance,
    GeodesicDistance,
};

/// \ingroup karma_physics
/// Optional procedural soft-body source.
enum class PhysicsSoftBodyPreset : uint8_t {
    Custom,
    Cloth,
    Cube,
    Sphere,
};

/// \ingroup karma_physics
/// Space for constraint frames.
enum class PhysicsConstraintSpace : uint8_t {
    World,
    LocalToBodyCenterOfMass,
};

/// \ingroup karma_physics
/// Spring mode used by constraint limits and motors.
enum class PhysicsSpringMode : uint8_t {
    FrequencyAndDamping,
    StiffnessAndDamping,
};

/// \ingroup karma_physics
/// Linear or angular spring settings.
struct PhysicsSpringSettings {
    PhysicsSpringMode mode = PhysicsSpringMode::FrequencyAndDamping;
    float frequency_or_stiffness = 0.0f;
    float damping = 0.0f;
};

/// \ingroup karma_physics
/// Point on a vehicle friction or torque curve.
struct PhysicsVehicleCurvePoint {
    float x = 0.0f;
    float y = 0.0f;
};

/// \ingroup karma_physics
/// Runtime driver input for vehicle controllers.
struct PhysicsVehicleInput {
    float forward = 0.0f;
    float right = 0.0f;
    float brake = 0.0f;
    float hand_brake = 0.0f;
    float left_ratio = 1.0f;
    float right_ratio = 1.0f;
    int current_gear = 0;
    float clutch_friction = 1.0f;
};

/// \ingroup karma_physics
/// Engine settings for wheeled, motorcycle, and tracked vehicles.
struct PhysicsVehicleEngineDesc {
    float max_torque = 500.0f;
    float min_rpm = 1000.0f;
    float max_rpm = 6000.0f;
    float inertia = 0.5f;
    float angular_damping = 0.2f;
    std::vector<PhysicsVehicleCurvePoint> normalized_torque;
};

/// \ingroup karma_physics
/// Gear-box settings for vehicles.
struct PhysicsVehicleTransmissionDesc {
    PhysicsVehicleTransmissionMode mode = PhysicsVehicleTransmissionMode::Automatic;
    std::vector<float> gear_ratios{2.66f, 1.78f, 1.3f, 1.0f, 0.74f};
    std::vector<float> reverse_gear_ratios{-2.9f};
    float switch_time = 0.5f;
    float clutch_release_time = 0.3f;
    float switch_latency = 0.5f;
    float shift_up_rpm = 4000.0f;
    float shift_down_rpm = 2000.0f;
    float clutch_strength = 10.0f;
};

/// \ingroup karma_physics
/// Wheeled-vehicle differential settings.
struct PhysicsVehicleDifferentialDesc {
    int left_wheel = -1;
    int right_wheel = -1;
    float differential_ratio = 3.42f;
    float left_right_split = 0.5f;
    float limited_slip_ratio = 1.4f;
    float engine_torque_ratio = 1.0f;
};

/// \ingroup karma_physics
/// Anti-roll bar connecting two vehicle wheels.
struct PhysicsVehicleAntiRollBarDesc {
    int left_wheel = 0;
    int right_wheel = 1;
    float stiffness = 1000.0f;
};

/// \ingroup karma_physics
/// Tracked-vehicle track settings.
struct PhysicsVehicleTrackDesc {
    uint32_t driven_wheel = 0;
    std::vector<uint32_t> wheels;
    float inertia = 10.0f;
    float angular_damping = 0.5f;
    float max_brake_torque = 15000.0f;
    float differential_ratio = 6.0f;
};

/// \ingroup karma_physics
/// Shared wheel settings for all vehicle controllers.
struct PhysicsVehicleWheelDesc {
    glm::vec3 position{0.0f};
    glm::vec3 suspension_force_point{0.0f};
    glm::vec3 suspension_direction{0.0f, -1.0f, 0.0f};
    glm::vec3 steering_axis{0.0f, 1.0f, 0.0f};
    glm::vec3 wheel_up{0.0f, 1.0f, 0.0f};
    glm::vec3 wheel_forward{0.0f, 0.0f, 1.0f};
    float suspension_min_length = 0.3f;
    float suspension_max_length = 0.5f;
    float suspension_preload_length = 0.0f;
    PhysicsSpringSettings suspension_spring{PhysicsSpringMode::FrequencyAndDamping, 1.5f, 0.5f};
    float radius = 0.3f;
    float width = 0.1f;
    bool enable_suspension_force_point = false;

    float inertia = 0.9f;
    float angular_damping = 0.2f;
    float max_steer_angle = 1.22173048f;
    std::vector<PhysicsVehicleCurvePoint> longitudinal_friction;
    std::vector<PhysicsVehicleCurvePoint> lateral_friction;
    float max_brake_torque = 1500.0f;
    float max_hand_brake_torque = 4000.0f;

    float tracked_longitudinal_friction = 4.0f;
    float tracked_lateral_friction = 2.0f;
};

/// \ingroup karma_physics
/// Motorcycle lean-controller settings.
struct PhysicsMotorcycleDesc {
    float max_lean_angle = 0.785398163f;
    float lean_spring_constant = 5000.0f;
    float lean_spring_damping = 1000.0f;
    float lean_spring_integration_coefficient = 0.0f;
    float lean_spring_integration_decay = 4.0f;
    float lean_smoothing_factor = 0.8f;
    bool enable_lean_controller = true;
    bool enable_lean_steering_limit = true;
};

/// \ingroup karma_physics
/// Vehicle constraint creation settings.
struct PhysicsVehicleDesc {
    PhysicsVehicleControllerType controller = PhysicsVehicleControllerType::Wheeled;
    PhysicsVehicleCollisionTesterType collision_tester = PhysicsVehicleCollisionTesterType::Ray;
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
    float max_pitch_roll_angle = 3.14159265358979323846f;
    float collision_test_sphere_radius = 0.3f;
    float collision_test_cylinder_convex_radius_fraction = 0.1f;
    float collision_test_max_slope_angle = 1.3962634f;
    uint32_t collision_test_layer = 1u;
    uint32_t num_steps_between_collision_test_active = 1;
    uint32_t num_steps_between_collision_test_inactive = 1;
    bool override_gravity = false;
    glm::vec3 gravity{0.0f, -9.8f, 0.0f};
    uint32_t priority = 0;
    uint32_t velocity_solver_steps = 0;
    uint32_t position_solver_steps = 0;
    float draw_size = 1.0f;
    uint64_t user_data = 0;

    std::vector<PhysicsVehicleWheelDesc> wheels;
    std::vector<PhysicsVehicleAntiRollBarDesc> anti_roll_bars;
    PhysicsVehicleEngineDesc engine{};
    PhysicsVehicleTransmissionDesc transmission{};
    std::vector<PhysicsVehicleDifferentialDesc> differentials;
    float differential_limited_slip_ratio = 1.4f;
    PhysicsMotorcycleDesc motorcycle{};
    std::array<PhysicsVehicleTrackDesc, 2> tracks{};
};

/// \ingroup karma_physics
/// Runtime wheel state.
struct PhysicsVehicleWheelState {
    bool has_contact = false;
    std::uintptr_t contact_body = 0;
    glm::vec3 contact_position{0.0f};
    glm::vec3 contact_normal{0.0f, 1.0f, 0.0f};
    glm::vec3 contact_longitudinal{0.0f, 0.0f, 1.0f};
    glm::vec3 contact_lateral{1.0f, 0.0f, 0.0f};
    float suspension_length = 0.0f;
    float suspension_lambda = 0.0f;
    float longitudinal_lambda = 0.0f;
    float lateral_lambda = 0.0f;
    float steer_angle = 0.0f;
    float rotation_angle = 0.0f;
    float angular_velocity = 0.0f;
};

/// \ingroup karma_physics
/// Runtime vehicle state snapshot.
struct PhysicsVehicleState {
    bool valid = false;
    bool active = false;
    std::uintptr_t handle = 0;
    float engine_rpm = 0.0f;
    int current_gear = 0;
    float clutch_friction = 0.0f;
    bool switching_gear = false;
    float wheel_speed_at_clutch = 0.0f;
    float tracked_left_angular_velocity = 0.0f;
    float tracked_right_angular_velocity = 0.0f;
    std::vector<PhysicsVehicleWheelState> wheels;
};

/// \ingroup karma_physics
/// Backend-neutral two-body constraint creation description.
struct PhysicsConstraintDesc {
    PhysicsConstraintType type = PhysicsConstraintType::Fixed;
    PhysicsConstraintSpace space = PhysicsConstraintSpace::World;
    bool enabled = true;
    uint32_t priority = 0;
    uint32_t velocity_solver_steps = 0;
    uint32_t position_solver_steps = 0;
    float draw_size = 1.0f;
    uint64_t user_data = 0;

    bool auto_detect_point = false;
    glm::vec3 point1{0.0f};
    glm::vec3 point2{0.0f};
    glm::vec3 axis1{0.0f, 1.0f, 0.0f};
    glm::vec3 axis2{0.0f, 1.0f, 0.0f};
    glm::vec3 normal1{1.0f, 0.0f, 0.0f};
    glm::vec3 normal2{1.0f, 0.0f, 0.0f};
    glm::vec3 plane_axis1{0.0f, 1.0f, 0.0f};
    glm::vec3 plane_axis2{0.0f, 1.0f, 0.0f};

    float min_distance = -1.0f;
    float max_distance = -1.0f;
    float limits_min = -3.14159265358979323846f;
    float limits_max = 3.14159265358979323846f;
    float half_cone_angle = 0.0f;
    float normal_half_cone_angle = 0.0f;
    float plane_half_cone_angle = 0.0f;
    float twist_min_angle = 0.0f;
    float twist_max_angle = 0.0f;
    float max_friction_force = 0.0f;
    float max_friction_torque = 0.0f;
    PhysicsSpringSettings limit_spring{};

    std::array<float, 6> six_dof_min_limits{{-3.402823466e+38F, -3.402823466e+38F,
                                             -3.402823466e+38F, -3.402823466e+38F,
                                             -3.402823466e+38F, -3.402823466e+38F}};
    std::array<float, 6> six_dof_max_limits{{3.402823466e+38F, 3.402823466e+38F,
                                             3.402823466e+38F, 3.402823466e+38F,
                                             3.402823466e+38F, 3.402823466e+38F}};
    std::array<float, 6> six_dof_max_friction{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
};

/// \ingroup karma_physics
/// Bit mask of unlocked world-space degrees of freedom.
enum PhysicsAllowedDof : uint8_t {
    PhysicsDofNone = 0,
    PhysicsDofTranslationX = 1u << 0u,
    PhysicsDofTranslationY = 1u << 1u,
    PhysicsDofTranslationZ = 1u << 2u,
    PhysicsDofRotationX = 1u << 3u,
    PhysicsDofRotationY = 1u << 4u,
    PhysicsDofRotationZ = 1u << 5u,
    PhysicsDofAll = PhysicsDofTranslationX | PhysicsDofTranslationY | PhysicsDofTranslationZ |
                    PhysicsDofRotationX | PhysicsDofRotationY | PhysicsDofRotationZ,
    PhysicsDofPlane2D = PhysicsDofTranslationX | PhysicsDofTranslationY | PhysicsDofRotationZ,
};

/// \ingroup karma_physics
/// Backend-neutral shape description. Fields that do not apply to `type` are ignored.
struct PhysicsShapeDesc {
    PhysicsShapeType type = PhysicsShapeType::Box;
    glm::vec3 center{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    glm::vec3 half_extents{0.5f};
    float radius = 0.5f;
    float height = 1.0f;
    float top_radius = 0.5f;
    float bottom_radius = 0.5f;
    float convex_radius = 0.0f;

    std::array<glm::vec3, 3> triangle{};
    std::vector<glm::vec3> points;

    std::string mesh_asset_key;
    std::vector<glm::vec3> mesh_vertices;
    std::vector<uint32_t> mesh_indices;
    std::vector<float> height_samples;
    uint32_t height_sample_count = 0;
    glm::vec3 height_offset{0.0f};
    glm::vec3 height_scale{1.0f};
    uint32_t height_block_size = 2;
    uint32_t height_bits_per_sample = 8;

    std::vector<PhysicsShapeDesc> children;
};

/// \ingroup karma_physics
/// Soft-body vertex authoring data.
struct PhysicsSoftBodyVertexDesc {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float inverse_mass = 1.0f;
};

/// \ingroup karma_physics
/// Soft-body triangle face.
struct PhysicsSoftBodyFaceDesc {
    uint32_t vertex0 = 0;
    uint32_t vertex1 = 0;
    uint32_t vertex2 = 0;
    uint32_t material_index = 0;
};

/// \ingroup karma_physics
/// Soft-body edge/spring constraint.
struct PhysicsSoftBodyEdgeDesc {
    uint32_t vertex0 = 0;
    uint32_t vertex1 = 0;
    float compliance = 0.0f;
};

/// \ingroup karma_physics
/// Soft-body volume constraint.
struct PhysicsSoftBodyVolumeDesc {
    uint32_t vertex0 = 0;
    uint32_t vertex1 = 0;
    uint32_t vertex2 = 0;
    uint32_t vertex3 = 0;
    float compliance = 0.0f;
};

/// \ingroup karma_physics
/// Soft-body constraint generation attributes.
struct PhysicsSoftBodyVertexAttributes {
    float compliance = 0.0f;
    float shear_compliance = 0.0f;
    float bend_compliance = 3.402823466e+38F;
    PhysicsSoftBodyLraType lra_type = PhysicsSoftBodyLraType::None;
    float lra_max_distance_multiplier = 1.0f;
};

/// \ingroup karma_physics
/// Soft-body creation settings.
struct PhysicsSoftBodyDesc {
    PhysicsSoftBodyPreset preset = PhysicsSoftBodyPreset::Custom;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    uint64_t user_data = 0;

    std::vector<PhysicsSoftBodyVertexDesc> vertices;
    std::vector<PhysicsSoftBodyFaceDesc> faces;
    std::vector<PhysicsSoftBodyEdgeDesc> edges;
    std::vector<PhysicsSoftBodyVolumeDesc> volumes;
    std::vector<uint32_t> pinned_vertices;

    uint32_t grid_size_x = 12;
    uint32_t grid_size_y = 12;
    uint32_t grid_size_z = 4;
    float grid_spacing = 0.5f;
    float radius = 1.0f;
    uint32_t sphere_theta = 16;
    uint32_t sphere_phi = 8;
    bool pin_cloth_corners = true;

    bool create_constraints = true;
    bool optimize = true;
    PhysicsSoftBodyBendType bend_type = PhysicsSoftBodyBendType::Distance;
    PhysicsSoftBodyVertexAttributes vertex_attributes{};
    float angle_tolerance = 0.13962634f;
    float vertex_radius = 0.0f;

    PhysicsMaterial material{};
    PhysicsCollisionFilter collision_filter{};
    uint32_t solver_iterations = 5;
    float linear_damping = 0.1f;
    float max_linear_velocity = 500.0f;
    float pressure = 0.0f;
    float gravity_factor = 1.0f;
    bool update_position = true;
    bool make_rotation_identity = true;
    bool allow_sleeping = true;
    bool activate = true;
};

/// \ingroup karma_physics
/// Runtime soft-body vertex state.
struct PhysicsSoftBodyVertexState {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float inverse_mass = 0.0f;
};

/// \ingroup karma_physics
/// Runtime soft-body state snapshot.
struct PhysicsSoftBodyState {
    bool valid = false;
    bool active = false;
    std::uintptr_t handle = 0;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float volume = 0.0f;
    uint32_t solver_iterations = 0;
    float pressure = 0.0f;
    bool update_position = true;
    std::vector<PhysicsSoftBodyVertexState> vertices;
    std::vector<uint32_t> indices;
};

/// \ingroup karma_physics
/// Backend-neutral rigid body creation description.
struct PhysicsBodyDesc {
    PhysicsShapeDesc shape{};
    PhysicsMotionType motion = PhysicsMotionType::Dynamic;
    PhysicsMotionQuality motion_quality = PhysicsMotionQuality::Discrete;
    uint8_t allowed_dofs = PhysicsDofAll;

    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 linear_velocity{0.0f};
    glm::vec3 angular_velocity{0.0f};

    float mass = 1.0f;
    float inertia_multiplier = 1.0f;
    float gravity_factor = 1.0f;
    float linear_damping = 0.05f;
    float angular_damping = 0.05f;
    float max_linear_velocity = 500.0f;
    float max_angular_velocity = 0.25f * 3.14159265358979323846f * 60.0f;
    uint32_t velocity_solver_steps = 0;
    uint32_t position_solver_steps = 0;

    PhysicsMaterial material{};
    PhysicsCollisionFilter collision_filter{};
    bool sensor = false;
    bool allow_sleeping = true;
    bool allow_dynamic_or_kinematic = false;
    bool collide_kinematic_vs_non_dynamic = false;
    bool use_manifold_reduction = true;
    bool apply_gyroscopic_force = false;
    bool enhanced_internal_edge_removal = false;
    bool activate = true;
    uint64_t user_data = 0;
};

/// \ingroup karma_physics
/// Backend collision query filter.
struct PhysicsQueryFilter {
    uint32_t collision_mask = 0xFFFFFFFFu;
    bool include_sensors = true;
    std::uintptr_t ignored_body = 0;
    std::vector<std::uintptr_t> ignored_bodies;
};

/// \ingroup karma_physics
/// Ray query settings.
struct PhysicsRaycastDesc {
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    PhysicsQueryFilter filter{};
    PhysicsBackFaceMode back_face_mode = PhysicsBackFaceMode::Ignore;
    bool treat_convex_as_solid = true;
};

/// \ingroup karma_physics
/// Shape overlap query settings.
struct PhysicsShapeQueryDesc {
    PhysicsShapeDesc shape{};
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    PhysicsQueryFilter filter{};
    PhysicsBackFaceMode back_face_mode = PhysicsBackFaceMode::Ignore;
    float max_separation_distance = 0.0f;
};

/// \ingroup karma_physics
/// Linear shape cast query settings.
struct PhysicsShapeCastDesc {
    PhysicsShapeDesc shape{};
    glm::vec3 from{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 translation{0.0f};
    glm::vec3 scale{1.0f};
    PhysicsQueryFilter filter{};
    PhysicsBackFaceMode back_face_mode_triangles = PhysicsBackFaceMode::Ignore;
    PhysicsBackFaceMode back_face_mode_convex = PhysicsBackFaceMode::Ignore;
    bool use_shrunken_shape_and_convex_radius = false;
    bool return_deepest_point = false;
};

/// \ingroup karma_physics
/// Parameters for applying an exact buoyancy impulse to a rigid body.
struct PhysicsBuoyancyDesc {
    glm::vec3 surface_position{0.0f};
    glm::vec3 surface_normal{0.0f, 1.0f, 0.0f};
    float buoyancy = 1.0f;
    float linear_drag = 0.0f;
    float angular_drag = 0.0f;
    glm::vec3 fluid_velocity{0.0f};
    glm::vec3 gravity{0.0f, -9.8f, 0.0f};
    float delta_time = 1.0f / 60.0f;
};

/// \ingroup karma_physics
/// Common result payload for ray, point, overlap, and shape-cast queries.
struct PhysicsQueryHit {
    std::uintptr_t body = 0;
    float fraction = 0.0f;
    float penetration_depth = 0.0f;
    glm::vec3 point{0.0f};
    glm::vec3 point_on_query{0.0f};
    glm::vec3 point_on_body{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool back_face = false;
};

/// \ingroup karma_physics
/// Backend contact pair with points and normal.
struct PhysicsContact {
    std::uintptr_t handle_a = 0;
    std::uintptr_t handle_b = 0;
    glm::vec3 point_a{0.0f};
    glm::vec3 point_b{0.0f};
    glm::vec3 normal_a_to_b{0.0f, 1.0f, 0.0f};
};

/// \ingroup karma_physics
/// Ground/support contact result.
struct PhysicsGroundContact {
    bool grounded = false;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    std::uintptr_t support_handle = 0;
};

} // namespace karma::physics


#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace karma::physics {

/// \ingroup karma_physics
/// Move-only wrapper around a backend dynamic rigid body.
class RigidBody {
public:
    struct Impl;

    RigidBody();
    RigidBody(const RigidBody&) = delete;
    RigidBody& operator=(const RigidBody&) = delete;
    RigidBody(RigidBody&& other) noexcept;
    RigidBody& operator=(RigidBody&& other) noexcept;
    ~RigidBody();

    /// Returns true when a backend body exists and is valid.
    bool isValid() const;

    glm::vec3 getPosition() const;
    glm::quat getRotation() const;
    glm::vec3 getVelocity() const;
    glm::vec3 getAngularVelocity() const;
    glm::vec3 getForwardVector() const;
    bool isActive() const;

    void setPosition(const glm::vec3& position);
    void setRotation(const glm::quat& rotation);
    void setPositionAndRotation(const glm::vec3& position, const glm::quat& rotation);
    void moveKinematic(const glm::vec3& targetPosition,
                       const glm::quat& targetRotation,
                       float deltaTime);
    void setVelocity(const glm::vec3& velocity);
    void setAngularVelocity(const glm::vec3& angularVelocity);
    void setLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                     const glm::vec3& angularVelocity);
    void addLinearVelocity(const glm::vec3& velocity);
    void addLinearAndAngularVelocity(const glm::vec3& linearVelocity,
                                     const glm::vec3& angularVelocity);
    glm::vec3 getPointVelocity(const glm::vec3& point) const;
    void setKinematic(bool kinematic);
    void setMotionQuality(PhysicsMotionQuality quality);
    void setUseGravity(bool useGravity);
    void setGravityFactor(float gravityFactor);
    float getGravityFactor() const;
    void setTrigger(bool trigger);
    void setFriction(float friction);
    float getFriction() const;
    void setRestitution(float restitution);
    float getRestitution() const;
    void setUseManifoldReduction(bool enabled);
    bool getUseManifoldReduction() const;
    void setUserData(uint64_t userData);
    uint64_t getUserData() const;
    void activate();
    void deactivate();
    void resetSleepTimer();
    bool setShape(const PhysicsShapeDesc& shape,
                  bool updateMassProperties = true,
                  bool activate = true);
    void addForce(const glm::vec3& force);
    void addForceAtPosition(const glm::vec3& force, const glm::vec3& position);
    void addTorque(const glm::vec3& torque);
    void addImpulse(const glm::vec3& impulse);
    void addImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& position);
    void addAngularImpulse(const glm::vec3& impulse);
    bool applyBuoyancyImpulse(const PhysicsBuoyancyDesc& desc);

    bool isGrounded(const glm::vec3& dimensions) const;

    /// Destroys the backend body.
    void destroy();

    /// Returns backend-native handle for diagnostics/contact mapping.
    std::uintptr_t nativeHandle() const;

private:
    friend class World;
    explicit RigidBody(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace karma::physics


#include <cstdint>
#include <memory>

namespace karma::physics {

/// \ingroup karma_physics
/// Move-only wrapper around a backend two-body constraint.
class Constraint {
public:
    struct Impl;

    Constraint();
    Constraint(const Constraint&) = delete;
    Constraint& operator=(const Constraint&) = delete;
    Constraint(Constraint&& other) noexcept;
    Constraint& operator=(Constraint&& other) noexcept;
    ~Constraint();

    bool isValid() const;
    void setEnabled(bool enabled);
    void destroy();
    std::uintptr_t nativeHandle() const;

private:
    friend class World;
    explicit Constraint(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace karma::physics


#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

namespace karma::physics {

/// \ingroup karma_physics
/// Move-only wrapper around a backend character-controller body.
class CharacterController {
public:
    struct Impl;

    CharacterController();
    CharacterController(const CharacterController&) = delete;
    CharacterController& operator=(const CharacterController&) = delete;
    CharacterController(CharacterController&& other) noexcept;
    CharacterController& operator=(CharacterController&& other) noexcept;
    ~CharacterController();

    glm::vec3 getPosition() const;
    glm::quat getRotation() const;
    glm::vec3 getVelocity() const;
    glm::vec3 getAngularVelocity() const;
    glm::vec3 getForwardVector() const;
    glm::vec3 getCenter() const { return center_; }
    void setHalfExtents(const glm::vec3& extents);
    void setCenter(const glm::vec3& center);

    /// Updates backend controller behavior for one step.
    void update(float dt);

    void setPosition(const glm::vec3& position);
    void setRotation(const glm::quat& rotation);
    void setVelocity(const glm::vec3& velocity);
    void setAngularVelocity(const glm::vec3& angularVelocity);

    /// Returns true when the controller is touching valid support.
    bool isGrounded() const;
    /// Returns current ground/support contact metadata.
    bool getGroundContact(PhysicsGroundContact& outContact) const;
    /// Collects controller contact pairs.
    void collectContacts(std::vector<PhysicsContact>& outContacts) const;
    /// Returns backend-native handle for diagnostics/contact mapping.
    std::uintptr_t nativeHandle() const;

    /// Destroys the backend controller.
    void destroy();

private:
    friend class World;
    explicit CharacterController(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
    glm::vec3 center_{0.0f, 0.0f, 0.0f};
};

} // namespace karma::physics


#include <cstdint>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>


namespace karma::physics {

/// \ingroup karma_physics
/// Move-only wrapper around a backend soft body.
class SoftBody {
public:
    struct Impl;

    SoftBody();
    SoftBody(const SoftBody&) = delete;
    SoftBody& operator=(const SoftBody&) = delete;
    SoftBody(SoftBody&& other) noexcept;
    SoftBody& operator=(SoftBody&& other) noexcept;
    ~SoftBody();

    bool isValid() const;
    glm::vec3 getPosition() const;
    glm::quat getRotation() const;
    bool isActive() const;
    void setPressure(float pressure);
    void setUpdatePosition(bool updatePosition);
    void setEnableSkinConstraints(bool enabled);
    void setSkinnedMaxDistanceMultiplier(float multiplier);
    void setVertexPosition(uint32_t vertex, const glm::vec3& position, bool hardSkin = true);
    PhysicsSoftBodyState getState() const;
    void activate();
    void deactivate();
    void destroy();
    std::uintptr_t nativeHandle() const;

private:
    friend class World;
    explicit SoftBody(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace karma::physics


#include <cstdint>
#include <memory>


namespace karma::physics {

/// \ingroup karma_physics
/// Move-only wrapper around a backend vehicle constraint/controller.
class Vehicle {
public:
    struct Impl;

    Vehicle();
    Vehicle(const Vehicle&) = delete;
    Vehicle& operator=(const Vehicle&) = delete;
    Vehicle(Vehicle&& other) noexcept;
    Vehicle& operator=(Vehicle&& other) noexcept;
    ~Vehicle();

    bool isValid() const;
    void setInput(const PhysicsVehicleInput& input);
    PhysicsVehicleState getState() const;
    void setEnabled(bool enabled);
    void destroy();
    std::uintptr_t nativeHandle() const;

private:
    friend class World;
    explicit Vehicle(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace karma::physics


#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace karma::physics {

/// \ingroup karma_physics
/// High-level physics world facade owned by `EngineApp`.
class World {
public:
    struct Impl;

    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    /// Steps the physics backend.
    void update(float deltaTime);

    /// Sets world gravity.
    void setGravity(float gravity);

    /// Creates a rigid body from a backend-neutral body description.
    RigidBody createBody(const PhysicsBodyDesc& desc);

    /// Creates a two-body constraint from backend-native body handles.
    Constraint createConstraint(const PhysicsConstraintDesc& desc,
                                std::uintptr_t bodyA,
                                std::uintptr_t bodyB);
    /// Creates a vehicle constraint/controller attached to a rigid body handle.
    Vehicle createVehicle(const PhysicsVehicleDesc& desc, std::uintptr_t body);
    /// Creates a soft body from backend-neutral creation settings.
    SoftBody createSoftBody(const PhysicsSoftBodyDesc& desc);

    /// Creates a dynamic box body.
    RigidBody createBoxBody(const glm::vec3& halfExtents,
                            float mass,
                            const glm::vec3& position,
                            const PhysicsMaterial& material);

    /// Creates a character-controller body with an explicit size.
    CharacterController createCharacterController(const glm::vec3& size);

    /// Performs a simple raycast.
    bool raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& hitPoint, glm::vec3& hitNormal) const;
    /// Performs a raycast with support/contact metadata.
    bool raycastDetailed(const glm::vec3& from, const glm::vec3& to, PhysicsGroundContact& outHit) const;
    /// Performs a filtered raycast and returns the closest hit.
    bool castRay(const PhysicsRaycastDesc& desc, PhysicsQueryHit& outHit) const;
    /// Performs a filtered raycast and appends all hits sorted nearest first.
    void castRayAll(const PhysicsRaycastDesc& desc, std::vector<PhysicsQueryHit>& outHits) const;
    /// Appends all bodies containing `point`.
    void collidePoint(const glm::vec3& point,
                      const PhysicsQueryFilter& filter,
                      std::vector<PhysicsQueryHit>& outHits) const;
    /// Appends all bodies overlapping `desc.shape`.
    void collideShape(const PhysicsShapeQueryDesc& desc, std::vector<PhysicsQueryHit>& outHits) const;
    /// Appends all bodies hit by a linear shape cast.
    void castShape(const PhysicsShapeCastDesc& desc, std::vector<PhysicsQueryHit>& outHits) const;
    /// Collects backend contacts for this step.
    void collectContacts(std::vector<PhysicsContact>& outContacts) const;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace karma::physics


#include <string_view>
#include <unordered_map>


namespace karma::physics {

/// \ingroup karma_simulation
/// ECS overlap/trigger event system.
///
/// The system compares collider query results frame-to-frame and writes
/// `CollisionEventsComponent` buffers for entities with
/// `CollisionListenerComponent`.
class CollisionEventSystem : public world::ISystem {
 public:
  /// Persisted overlap used to compute enter/stay/exit transitions.
  struct TrackedContact {
    world::Entity other{};
    components::ColliderShapeType other_shape = components::ColliderShapeType::Box;
    bool other_is_trigger = false;
  };

  using ContactMap = std::unordered_map<uint64_t, TrackedContact>;

  void update(world::World& world, float dt) override;
  std::string_view name() const override { return "CollisionEventSystem"; }

 private:
  static uint64_t entityKey(world::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  void cleanupStale(world::World& world);

  std::unordered_map<uint64_t, ContactMap> previous_contacts_;
};

}  // namespace karma::physics


#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>


namespace karma::physics {

/// \ingroup karma_physics
/// Local-space geometry used when a mesh collider refers to external mesh content.
struct MeshColliderGeometry {
  std::vector<math::Vec3> vertices;
  std::vector<uint32_t> indices;
};

using MeshColliderGeometryProvider =
    std::function<std::optional<MeshColliderGeometry>(std::string_view mesh_key)>;

/// \ingroup karma_physics
/// Syncs ECS physics components with the configured physics backend.
///
/// The system creates/destroys backend bodies for ECS rigid bodies, applies
/// character-controller intent, steps physics, writes transforms, and emits
/// contact/ground-state components.
class PhysicsSystem : public world::ISystem {
 public:
  explicit PhysicsSystem(World& physics) : physics_(physics) {}

  void setMeshColliderGeometryProvider(MeshColliderGeometryProvider provider) {
    mesh_collider_geometry_provider_ = std::move(provider);
    mesh_collider_geometry_cache_.clear();
  }

  void update(world::World& world, float dt) override;
  std::string_view name() const override { return "PhysicsSystem"; }

 private:
  static uint64_t entityKey(world::Entity entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  void syncSimulationObjects(world::World& world);
  void applySimulationInputs(world::World& world, float dt);
  void stepSimulation(float dt);
  void publishSimulationResults(world::World& world);
  void publishSimulationEvents(world::World& world);
  void syncRigidBodies(world::World& world);
  void syncVehicles(world::World& world);
  void syncSoftBodies(world::World& world);
  void syncConstraints(world::World& world);
  void applyBodyForces(world::World& world);
  void syncDynamicBodies(world::World& world);
  void syncCharacterControllerObject(world::World& world);
  void applyCharacterControllerInput(world::World& world, float dt);
  void syncCharacterControllerTransform(world::World& world);
  void syncContactEvents(world::World& world);
  void syncGroundContacts(world::World& world);
  void cleanupStale(world::World& world);
  const MeshColliderGeometry* resolveMeshColliderGeometry(std::string_view mesh_key);

  struct TrackedContact {
    world::Entity other{};
    components::ColliderShapeType other_shape = components::ColliderShapeType::Box;
    math::Vec3 point{};
    math::Vec3 normal{0.0f, 1.0f, 0.0f};
  };

  using ContactMap = std::unordered_map<uint64_t, TrackedContact>;

  World& physics_;
  std::unordered_map<uint64_t, RigidBody> rigid_bodies_;
  struct BodyState {
    std::size_t signature = 0;
  };
  std::unordered_map<uint64_t, BodyState> body_state_;
  std::unordered_map<uint64_t, RigidBody> static_bodies_;
  std::unordered_map<uint64_t, Constraint> constraints_;
  std::unordered_map<uint64_t, std::size_t> constraint_signatures_;
  std::unordered_map<uint64_t, Vehicle> vehicles_;
  std::unordered_map<uint64_t, std::size_t> vehicle_signatures_;
  std::unordered_map<uint64_t, SoftBody> soft_bodies_;
  std::unordered_map<uint64_t, std::size_t> soft_body_signatures_;
  struct CharacterControllerState {
    CharacterController controller{};
    math::Vec3 half_extents{-1.0f, -1.0f, -1.0f};
    math::Vec3 center{};
    std::uintptr_t native_handle = 0;
    int shape_kind = -1;
  };
  std::unordered_map<uint64_t, CharacterControllerState> character_controllers_;
  std::unordered_map<std::uintptr_t, world::Entity> physics_entities_by_handle_;
  std::unordered_map<uint64_t, ContactMap> previous_contacts_;
  MeshColliderGeometryProvider mesh_collider_geometry_provider_;
  std::unordered_map<std::string, MeshColliderGeometry> mesh_collider_geometry_cache_;
};

}  // namespace karma::physics
