#pragma once

#include "karma/simulation/physics/backend.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace karma::physics {

/// \ingroup karma_physics
/// Move-only wrapper around a backend player-controller body.
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
    std::unique_ptr<karma::physics_backend::PhysicsPlayerControllerBackend> backend_;
    glm::vec3 center_{0.0f, 0.0f, 0.0f};
};

} // namespace karma::physics
