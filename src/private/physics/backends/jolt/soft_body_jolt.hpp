#pragma once

#include "private/physics/backend.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <optional>

namespace karma::physics::backend {

class PhysicsWorldJolt;

/// \ingroup karma_physics
/// Jolt-backed soft body.
class PhysicsSoftBodyJolt final : public PhysicsSoftBodyBackend {
public:
    PhysicsSoftBodyJolt() = default;
    PhysicsSoftBodyJolt(PhysicsWorldJolt* world, JPH::BodyID body);
    ~PhysicsSoftBodyJolt() override;

    bool isValid() const override;
    glm::vec3 getPosition() const override;
    glm::quat getRotation() const override;
    bool isActive() const override;
    void setPressure(float pressure) override;
    void setUpdatePosition(bool updatePosition) override;
    void setEnableSkinConstraints(bool enabled) override;
    void setSkinnedMaxDistanceMultiplier(float multiplier) override;
    void setVertexPosition(uint32_t vertex, const glm::vec3& position, bool hardSkin) override;
    karma::physics::PhysicsSoftBodyState getState() const override;
    void activate() override;
    void deactivate() override;
    void destroy() override;
    std::uintptr_t nativeHandle() const override;

private:
    PhysicsWorldJolt* world_ = nullptr;
    std::optional<JPH::BodyID> body_;
};

}  // namespace karma::physics::backend
