#pragma once

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/simulation/physics/types.h"

namespace karma::physics_backend {
class PhysicsSoftBodyBackend;
}

namespace karma::physics {

/// \ingroup karma_physics
/// Move-only wrapper around a backend soft body.
class SoftBody {
public:
    SoftBody() = default;
    explicit SoftBody(std::unique_ptr<karma::physics_backend::PhysicsSoftBodyBackend> backend);
    SoftBody(const SoftBody&) = delete;
    SoftBody& operator=(const SoftBody&) = delete;
    SoftBody(SoftBody&& other) noexcept = default;
    SoftBody& operator=(SoftBody&& other) noexcept = default;
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
    std::unique_ptr<karma::physics_backend::PhysicsSoftBodyBackend> backend_;
};

}  // namespace karma::physics
