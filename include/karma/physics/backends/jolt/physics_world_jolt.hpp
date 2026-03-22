#pragma once

#include "karma/physics/backend.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <mutex>
#include <unordered_map>
#include <memory>

namespace JPH {
class PhysicsSystem;
class JobSystem;
class TempAllocator;
class ContactListener;
}

namespace karma::physics_backend {

class PhysicsWorldJoltContactListener;

class PhysicsWorldJolt final : public PhysicsWorldBackend {
public:
    PhysicsWorldJolt();
    ~PhysicsWorldJolt() override;

    void update(float deltaTime) override;
    void setGravity(float gravity) override;
    std::unique_ptr<PhysicsRigidBodyBackend> createBoxBody(const glm::vec3& halfExtents,
                                                           float mass,
                                                           const glm::vec3& position,
                                                           const karma::physics::PhysicsMaterial& material) override;
    std::unique_ptr<PhysicsPlayerControllerBackend> createPlayer(const glm::vec3& size) override;
    std::unique_ptr<PhysicsStaticBodyBackend> createStaticMesh(const std::string& meshPath) override;
    bool raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& hitPoint, glm::vec3& hitNormal) const override;
    bool raycastDetailed(const glm::vec3& from,
                         const glm::vec3& to,
                         karma::physics::PhysicsGroundContact& outHit) const override;
    void collectContacts(std::vector<karma::physics::PhysicsContact>& outContacts) const override;

    JPH::PhysicsSystem* physicsSystem() { return physicsSystem_.get(); }
    const JPH::PhysicsSystem* physicsSystem() const { return physicsSystem_.get(); }
    JPH::TempAllocator* tempAllocator() { return tempAllocator_.get(); }
    void removeBody(const JPH::BodyID& id) const;

private:
    friend class PhysicsWorldJoltContactListener;

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

    std::unique_ptr<JPH::ContactListener> contactListener_;
    mutable std::mutex contacts_mutex_;
    std::unordered_map<ContactKey, karma::physics::PhysicsContact, ContactKeyHasher> contacts_;
    std::unique_ptr<JPH::TempAllocator> tempAllocator_;
    std::unique_ptr<JPH::JobSystem> jobSystem_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;
};

} // namespace karma::physics_backend
