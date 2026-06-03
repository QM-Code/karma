#pragma once

#include "karma/simulation/physics/backend.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace karma::physics {

/// \ingroup karma_physics
/// Lightweight wrapper for immovable physics geometry.
class StaticBody {
public:
    StaticBody() = default;
    explicit StaticBody(std::unique_ptr<karma::physics_backend::PhysicsStaticBodyBackend> backend);
    StaticBody(const StaticBody&) = delete;
    StaticBody& operator=(const StaticBody&) = delete;
    StaticBody(StaticBody&& other) noexcept = default;
    StaticBody& operator=(StaticBody&& other) noexcept = default;
    ~StaticBody();

    /// Returns true when a backend body exists and is valid.
    bool isValid() const;

    glm::vec3 getPosition() const;
    glm::quat getRotation() const;

    /// Destroys the backend body.
    void destroy();

    /// Returns backend-native handle for diagnostics/contact mapping.
    std::uintptr_t nativeHandle() const;

private:
    std::unique_ptr<karma::physics_backend::PhysicsStaticBodyBackend> backend_;
};

} // namespace karma::physics
