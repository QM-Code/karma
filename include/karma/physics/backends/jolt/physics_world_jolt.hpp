#pragma once

#include "karma/physics/backend.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <memory>

namespace JPH {
class PhysicsSystem;
class JobSystem;
class TempAllocator;
}

namespace karma::physics_backend {

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

    JPH::PhysicsSystem* physicsSystem() { return physicsSystem_.get(); }
    const JPH::PhysicsSystem* physicsSystem() const { return physicsSystem_.get(); }
    JPH::TempAllocator* tempAllocator() { return tempAllocator_.get(); }
    void removeBody(const JPH::BodyID& id) const;

private:
    std::unique_ptr<JPH::TempAllocator> tempAllocator_;
    std::unique_ptr<JPH::JobSystem> jobSystem_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;
};

} // namespace karma::physics_backend
