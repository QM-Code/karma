#pragma once

#include "private/physics/backend.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <mutex>
#include <unordered_map>
#include <memory>

namespace JPH {
class Body;
class Constraint;
class VehicleConstraint;
class PhysicsSystem;
class JobSystem;
class TempAllocator;
class ContactListener;
}

namespace karma::physics::backend {

class PhysicsWorldJoltContactListener;
class PhysicsWorldJoltQueryBodyFilter;

/// \ingroup karma_physics
/// Jolt implementation of `PhysicsWorldBackend`.
class PhysicsWorldJolt final : public PhysicsWorldBackend {
public:
    PhysicsWorldJolt();
    ~PhysicsWorldJolt() override;

    void update(float deltaTime) override;
    void setGravity(float gravity) override;
    std::unique_ptr<PhysicsRigidBodyBackend> createBody(const karma::physics::PhysicsBodyDesc& desc) override;
    std::unique_ptr<PhysicsConstraintBackend> createConstraint(
        const karma::physics::PhysicsConstraintDesc& desc,
        std::uintptr_t bodyA,
        std::uintptr_t bodyB) override;
    std::unique_ptr<PhysicsVehicleBackend> createVehicle(
        const karma::physics::PhysicsVehicleDesc& desc,
        std::uintptr_t body) override;
    std::unique_ptr<PhysicsSoftBodyBackend> createSoftBody(
        const karma::physics::PhysicsSoftBodyDesc& desc) override;
    std::unique_ptr<PhysicsRigidBodyBackend> createBoxBody(const glm::vec3& halfExtents,
                                                           float mass,
                                                           const glm::vec3& position,
                                                           const karma::physics::PhysicsMaterial& material) override;
    std::unique_ptr<PhysicsCharacterControllerBackend> createCharacterController(const glm::vec3& size) override;
    bool raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& hitPoint, glm::vec3& hitNormal) const override;
    bool raycastDetailed(const glm::vec3& from,
                         const glm::vec3& to,
                         karma::physics::PhysicsGroundContact& outHit) const override;
    bool castRay(const karma::physics::PhysicsRaycastDesc& desc,
                 karma::physics::PhysicsQueryHit& outHit) const override;
    void castRayAll(const karma::physics::PhysicsRaycastDesc& desc,
                    std::vector<karma::physics::PhysicsQueryHit>& outHits) const override;
    void collidePoint(const glm::vec3& point,
                      const karma::physics::PhysicsQueryFilter& filter,
                      std::vector<karma::physics::PhysicsQueryHit>& outHits) const override;
    void collideShape(const karma::physics::PhysicsShapeQueryDesc& desc,
                      std::vector<karma::physics::PhysicsQueryHit>& outHits) const override;
    void castShape(const karma::physics::PhysicsShapeCastDesc& desc,
                   std::vector<karma::physics::PhysicsQueryHit>& outHits) const override;
    void collectContacts(std::vector<karma::physics::PhysicsContact>& outContacts) const override;

    JPH::PhysicsSystem* physicsSystem() { return physicsSystem_.get(); }
    const JPH::PhysicsSystem* physicsSystem() const { return physicsSystem_.get(); }
    JPH::TempAllocator* tempAllocator() { return tempAllocator_.get(); }
    void removeBody(const JPH::BodyID& id);
    void removeConstraint(JPH::Constraint* constraint) const;
    void removeVehicle(JPH::VehicleConstraint* constraint);

private:
    friend class PhysicsWorldJoltContactListener;
    friend class PhysicsWorldJoltQueryBodyFilter;

    struct ContactKey {
        uint32_t a = 0;
        uint32_t b = 0;

        bool operator==(const ContactKey& other) const {
            return a == other.a && b == other.b;
        }
    };

    struct ContactKeyHasher {
        size_t operator()(const ContactKey& key) const {
            return (static_cast<size_t>(key.a) << 32) ^ static_cast<size_t>(key.b);
        }
    };

    bool bodiesShouldCollide(const JPH::Body& bodyA, const JPH::Body& bodyB) const;
    bool queryAllowsBody(const JPH::Body& body,
                         const karma::physics::PhysicsQueryFilter& filter) const;
    void registerBodyFilter(const JPH::BodyID& id, karma::physics::PhysicsCollisionFilter filter);
    void unregisterBodyFilter(const JPH::BodyID& id);

    mutable std::mutex body_filters_mutex_;
    std::unordered_map<uint32_t, karma::physics::PhysicsCollisionFilter> body_filters_;
    std::unique_ptr<JPH::ContactListener> contactListener_;
    mutable std::mutex contacts_mutex_;
    std::unordered_map<ContactKey, karma::physics::PhysicsContact, ContactKeyHasher> contacts_;
    std::unique_ptr<JPH::TempAllocator> tempAllocator_;
    std::unique_ptr<JPH::JobSystem> jobSystem_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;
};

} // namespace karma::physics::backend
