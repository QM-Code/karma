#include "karma/physics/physics_world.hpp"
#include "karma/physics/backend.hpp"
#include "karma/physics/player_controller.hpp"

namespace karma::physics {

World::World()
    : backend_(karma::physics_backend::CreatePhysicsWorldBackend()) {}

World::~World() = default;

void World::update(float deltaTime) {
    if (backend_) {
        backend_->update(deltaTime);
    }
    if (playerController_) {
        playerController_->update(deltaTime);
    }
}

void World::setGravity(float gravity) {
    if (backend_) {
        backend_->setGravity(gravity);
    }
}

RigidBody World::createBoxBody(const glm::vec3& halfExtents,
                               float mass,
                               const glm::vec3& position,
                               const PhysicsMaterial& material) {
    if (!backend_) {
        return RigidBody();
    }
    return RigidBody(backend_->createBoxBody(halfExtents, mass, position, material));
}

PlayerController& World::createPlayer(const glm::vec3& size) {
    if (!backend_) {
        playerController_ = std::make_unique<PlayerController>();
        return *playerController_;
    }
    playerController_ = std::make_unique<PlayerController>(backend_->createPlayer(size));
    return *playerController_;
}

PlayerController& World::createPlayer() {
    return createPlayer(glm::vec3(1.0f, 2.0f, 1.0f));
}

StaticBody World::createStaticMesh(const std::string& meshPath) {
    if (!backend_) {
        return StaticBody();
    }
    return StaticBody(backend_->createStaticMesh(meshPath));
}

bool World::raycast(const glm::vec3& from,
                    const glm::vec3& to,
                    glm::vec3& hitPoint,
                    glm::vec3& hitNormal) const {
    if (!backend_) {
        return false;
    }
    return backend_->raycast(from, to, hitPoint, hitNormal);
}

} // namespace karma::physics
