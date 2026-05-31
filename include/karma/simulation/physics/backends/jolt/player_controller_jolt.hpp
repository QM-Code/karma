#pragma once

#include "karma/simulation/physics/backend.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <mutex>
#include <unordered_map>

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
    bool getGroundContact(karma::physics::PhysicsGroundContact& outContact) const override;
    void collectContacts(std::vector<karma::physics::PhysicsContact>& outContacts) const override;
    void destroy() override;
    std::uintptr_t nativeHandle() const override;

private:
    struct CharacterContactKey {
        uint32_t body = 0;
        bool operator==(const CharacterContactKey& other) const { return body == other.body; }
    };

    struct CharacterContactKeyHasher {
        size_t operator()(const CharacterContactKey& key) const {
            return static_cast<size_t>(key.body);
        }
    };

    class ContactListener;

    PhysicsWorldJolt* world_ = nullptr;
    JPH::Ref<JPH::CharacterVirtual> character_;
    std::unique_ptr<ContactListener> contactListener_;
    mutable std::mutex contacts_mutex_;
    std::unordered_map<CharacterContactKey,
                       karma::physics::PhysicsContact,
                       CharacterContactKeyHasher> contacts_;
    glm::vec3 halfExtents{0.5f, 1.0f, 0.5f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    float gravity = -9.8f;
    float characterPadding = 0.05f;
    float groundSupportBand = 0.1f;
};

} // namespace karma::physics_backend
