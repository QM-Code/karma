#pragma once

#include "karma/physics/backend.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace karma::physics {

// Lightweight wrapper for immovable physics geometry (e.g., level meshes)
class StaticBody {
public:
    StaticBody() = default;
    explicit StaticBody(std::unique_ptr<karma::physics_backend::PhysicsStaticBodyBackend> backend);
    StaticBody(const StaticBody&) = delete;
    StaticBody& operator=(const StaticBody&) = delete;
    StaticBody(StaticBody&& other) noexcept = default;
    StaticBody& operator=(StaticBody&& other) noexcept = default;
    ~StaticBody();

    bool isValid() const;

    glm::vec3 getPosition() const;
    glm::quat getRotation() const;

    void destroy();

    std::uintptr_t nativeHandle() const;

private:
    std::unique_ptr<karma::physics_backend::PhysicsStaticBodyBackend> backend_;
};

} // namespace karma::physics
