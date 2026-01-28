#pragma once

#include "karma/physics/backend.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

namespace karma::physics_backend {

class PhysicsWorldJolt;

class PhysicsPlayerControllerJolt final : public PhysicsPlayerControllerBackend {
public:
    PhysicsPlayerControllerJolt(PhysicsWorldJolt* world,
                                const glm::vec3& halfExtents,
                                const glm::vec3& startPosition);
    ~PhysicsPlayerControllerJolt() override;

    glm::vec3 getPosition() const override;
    glm::quat getRotation() const override;
    glm::vec3 getVelocity() const override;
    glm::vec3 getAngularVelocity() const override;
    glm::vec3 getForwardVector() const override;
    void setHalfExtents(const glm::vec3& extents) override;
    void update(float dt) override;
    void setPosition(const glm::vec3& position) override;
    void setRotation(const glm::quat& rotation) override;
    void setVelocity(const glm::vec3& velocity) override;
    void setAngularVelocity(const glm::vec3& angularVelocity) override;
    bool isGrounded() const override;
    void destroy() override;

private:
    PhysicsWorldJolt* world_ = nullptr;
    JPH::Ref<JPH::CharacterVirtual> character_;
    glm::vec3 halfExtents{0.5f, 1.0f, 0.5f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    float gravity = -9.8f;
    float characterPadding = 0.05f;
    float groundSupportBand = 0.1f;
};

} // namespace karma::physics_backend
