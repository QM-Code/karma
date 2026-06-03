#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace karma::physics {

/// \ingroup karma_physics
/// Basic physics material parameters passed to backend body creation.
struct PhysicsMaterial {
    float friction = 0.0f;
    float restitution = 0.0f;
    float rollingFriction = 0.0f;
    float spinningFriction = 0.0f;
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
