#pragma once

#include "karma/physics/types.h"
#include "karma/physics/player_controller.hpp"
#include "karma/physics/rigid_body.hpp"
#include "karma/physics/static_body.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>

namespace karma::physics_backend {
class PhysicsWorldBackend;
}

namespace karma::physics {

class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void update(float deltaTime);

    void setGravity(float gravity);

    RigidBody createBoxBody(const glm::vec3& halfExtents,
                            float mass,
                            const glm::vec3& position,
                            const PhysicsMaterial& material);

    PlayerController& createPlayer();
    PlayerController& createPlayer(const glm::vec3& size);

    PlayerController* playerController() { return playerController_.get(); }

    StaticBody createStaticMesh(const std::string& meshPath);

    bool raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& hitPoint, glm::vec3& hitNormal) const;

private:
    std::unique_ptr<karma::physics_backend::PhysicsWorldBackend> backend_;
    std::unique_ptr<PlayerController> playerController_;
};

} // namespace karma::physics
