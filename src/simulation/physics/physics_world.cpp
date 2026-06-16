#include "karma/simulation/physics/physics_world.hpp"
#include "karma/simulation/physics/backend.hpp"
#include "karma/simulation/physics/player_controller.hpp"

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

RigidBody World::createBody(const PhysicsBodyDesc& desc) {
    if (!backend_) {
        return RigidBody();
    }
    return RigidBody(backend_->createBody(desc));
}

Constraint World::createConstraint(const PhysicsConstraintDesc& desc,
                                   std::uintptr_t bodyA,
                                   std::uintptr_t bodyB) {
    if (!backend_) {
        return Constraint();
    }
    return Constraint(backend_->createConstraint(desc, bodyA, bodyB));
}

RigidBody World::createBoxBody(const glm::vec3& halfExtents,
                               float mass,
                               const glm::vec3& position,
                               const PhysicsMaterial& material) {
    PhysicsBodyDesc desc;
    desc.shape.type = PhysicsShapeType::Box;
    desc.shape.half_extents = halfExtents;
    desc.mass = mass;
    desc.position = position;
    desc.material = material;
    return createBody(desc);
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

bool World::raycastDetailed(const glm::vec3& from,
                            const glm::vec3& to,
                            PhysicsGroundContact& outHit) const {
    if (!backend_) {
        return false;
    }
    return backend_->raycastDetailed(from, to, outHit);
}

bool World::castRay(const PhysicsRaycastDesc& desc, PhysicsQueryHit& outHit) const {
    if (!backend_) {
        return false;
    }
    return backend_->castRay(desc, outHit);
}

void World::castRayAll(const PhysicsRaycastDesc& desc, std::vector<PhysicsQueryHit>& outHits) const {
    if (backend_) {
        backend_->castRayAll(desc, outHits);
    }
}

void World::collidePoint(const glm::vec3& point,
                         const PhysicsQueryFilter& filter,
                         std::vector<PhysicsQueryHit>& outHits) const {
    if (backend_) {
        backend_->collidePoint(point, filter, outHits);
    }
}

void World::collideShape(const PhysicsShapeQueryDesc& desc, std::vector<PhysicsQueryHit>& outHits) const {
    if (backend_) {
        backend_->collideShape(desc, outHits);
    }
}

void World::castShape(const PhysicsShapeCastDesc& desc, std::vector<PhysicsQueryHit>& outHits) const {
    if (backend_) {
        backend_->castShape(desc, outHits);
    }
}

void World::collectContacts(std::vector<PhysicsContact>& outContacts) const {
    if (backend_) {
        backend_->collectContacts(outContacts);
    }
}

} // namespace karma::physics
