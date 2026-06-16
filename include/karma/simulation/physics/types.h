#pragma once

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

    std::string mesh_path;
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
