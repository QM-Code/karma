#include "karma/physics.h"
#include "karma/physics.h"
#include "private/physics/objects.hpp"

namespace karma::physics {

World::World()
    : impl_(std::make_unique<Impl>()) {
    impl_->backend = karma::physics::backend::CreatePhysicsWorldBackend();
}

World::~World() = default;

void World::update(float deltaTime) {
    if (impl_ && impl_->backend) {
        impl_->backend->update(deltaTime);
    }
}

void World::setGravity(float gravity) {
    if (impl_ && impl_->backend) {
        impl_->backend->setGravity(gravity);
    }
}

RigidBody World::createBody(const PhysicsBodyDesc& desc) {
    if (!impl_ || !impl_->backend) {
        return RigidBody();
    }
    return RigidBody(std::make_unique<RigidBody::Impl>(impl_->backend->createBody(desc)));
}

Constraint World::createConstraint(const PhysicsConstraintDesc& desc,
                                   std::uintptr_t bodyA,
                                   std::uintptr_t bodyB) {
    if (!impl_ || !impl_->backend) {
        return Constraint();
    }
    return Constraint(std::make_unique<Constraint::Impl>(
        impl_->backend->createConstraint(desc, bodyA, bodyB)));
}

Vehicle World::createVehicle(const PhysicsVehicleDesc& desc, std::uintptr_t body) {
    if (!impl_ || !impl_->backend) {
        return Vehicle();
    }
    return Vehicle(std::make_unique<Vehicle::Impl>(impl_->backend->createVehicle(desc, body)));
}

SoftBody World::createSoftBody(const PhysicsSoftBodyDesc& desc) {
    if (!impl_ || !impl_->backend) {
        return SoftBody();
    }
    return SoftBody(std::make_unique<SoftBody::Impl>(impl_->backend->createSoftBody(desc)));
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

CharacterController World::createCharacterController(const glm::vec3& size) {
    if (!impl_ || !impl_->backend) {
        return CharacterController();
    }
    return CharacterController(
        std::make_unique<CharacterController::Impl>(impl_->backend->createCharacterController(size)));
}

bool World::raycast(const glm::vec3& from,
                    const glm::vec3& to,
                    glm::vec3& hitPoint,
                    glm::vec3& hitNormal) const {
    if (!impl_ || !impl_->backend) {
        return false;
    }
    return impl_->backend->raycast(from, to, hitPoint, hitNormal);
}

bool World::raycastDetailed(const glm::vec3& from,
                            const glm::vec3& to,
                            PhysicsGroundContact& outHit) const {
    if (!impl_ || !impl_->backend) {
        return false;
    }
    return impl_->backend->raycastDetailed(from, to, outHit);
}

bool World::castRay(const PhysicsRaycastDesc& desc, PhysicsQueryHit& outHit) const {
    if (!impl_ || !impl_->backend) {
        return false;
    }
    return impl_->backend->castRay(desc, outHit);
}

void World::castRayAll(const PhysicsRaycastDesc& desc, std::vector<PhysicsQueryHit>& outHits) const {
    if (impl_ && impl_->backend) {
        impl_->backend->castRayAll(desc, outHits);
    }
}

void World::collidePoint(const glm::vec3& point,
                         const PhysicsQueryFilter& filter,
                         std::vector<PhysicsQueryHit>& outHits) const {
    if (impl_ && impl_->backend) {
        impl_->backend->collidePoint(point, filter, outHits);
    }
}

void World::collideShape(const PhysicsShapeQueryDesc& desc, std::vector<PhysicsQueryHit>& outHits) const {
    if (impl_ && impl_->backend) {
        impl_->backend->collideShape(desc, outHits);
    }
}

void World::castShape(const PhysicsShapeCastDesc& desc, std::vector<PhysicsQueryHit>& outHits) const {
    if (impl_ && impl_->backend) {
        impl_->backend->castShape(desc, outHits);
    }
}

void World::collectContacts(std::vector<PhysicsContact>& outContacts) const {
    if (impl_ && impl_->backend) {
        impl_->backend->collectContacts(outContacts);
    }
}

} // namespace karma::physics
