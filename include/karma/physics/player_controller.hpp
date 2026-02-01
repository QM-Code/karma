#pragma once

#include "karma/physics/backend.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace karma::physics {

class PlayerController {
public:
    PlayerController() = default;
    explicit PlayerController(std::unique_ptr<karma::physics_backend::PhysicsPlayerControllerBackend> backend);
    PlayerController(const PlayerController&) = delete;
    PlayerController& operator=(const PlayerController&) = delete;
    PlayerController(PlayerController&& other) noexcept = default;
    PlayerController& operator=(PlayerController&& other) noexcept = default;
    ~PlayerController();

    glm::vec3 getPosition() const;
    glm::quat getRotation() const;
    glm::vec3 getVelocity() const;
    glm::vec3 getAngularVelocity() const;
    glm::vec3 getForwardVector() const;
    glm::vec3 getCenter() const { return center_; }
    void setHalfExtents(const glm::vec3& extents);
    void setCenter(const glm::vec3& center);

    void update(float dt);

    void setPosition(const glm::vec3& position);
    void setRotation(const glm::quat& rotation);
    void setVelocity(const glm::vec3& velocity);
    void setAngularVelocity(const glm::vec3& angularVelocity);

    bool isGrounded() const;

    void destroy();

private:
    std::unique_ptr<karma::physics_backend::PhysicsPlayerControllerBackend> backend_;
    glm::vec3 center_{0.0f, 0.0f, 0.0f};
};

} // namespace karma::physics
