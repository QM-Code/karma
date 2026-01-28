#pragma once

#include "karma/physics/types.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>

namespace karma::physics_backend {

class PhysicsRigidBodyBackend {
public:
    virtual ~PhysicsRigidBodyBackend() = default;
    virtual bool isValid() const = 0;
    virtual glm::vec3 getPosition() const = 0;
    virtual glm::quat getRotation() const = 0;
    virtual glm::vec3 getVelocity() const = 0;
    virtual glm::vec3 getAngularVelocity() const = 0;
    virtual glm::vec3 getForwardVector() const = 0;
    virtual void setPosition(const glm::vec3& position) = 0;
    virtual void setRotation(const glm::quat& rotation) = 0;
    virtual void setVelocity(const glm::vec3& velocity) = 0;
    virtual void setAngularVelocity(const glm::vec3& angularVelocity) = 0;
    virtual bool isGrounded(const glm::vec3& dimensions) const = 0;
    virtual void destroy() = 0;
    virtual std::uintptr_t nativeHandle() const = 0;
};

class PhysicsStaticBodyBackend {
public:
    virtual ~PhysicsStaticBodyBackend() = default;
    virtual bool isValid() const = 0;
    virtual glm::vec3 getPosition() const = 0;
    virtual glm::quat getRotation() const = 0;
    virtual void destroy() = 0;
    virtual std::uintptr_t nativeHandle() const = 0;
};

class PhysicsPlayerControllerBackend {
public:
    virtual ~PhysicsPlayerControllerBackend() = default;
    virtual glm::vec3 getPosition() const = 0;
    virtual glm::quat getRotation() const = 0;
    virtual glm::vec3 getVelocity() const = 0;
    virtual glm::vec3 getAngularVelocity() const = 0;
    virtual glm::vec3 getForwardVector() const = 0;
    virtual void setHalfExtents(const glm::vec3& extents) = 0;
    virtual void update(float dt) = 0;
    virtual void setPosition(const glm::vec3& position) = 0;
    virtual void setRotation(const glm::quat& rotation) = 0;
    virtual void setVelocity(const glm::vec3& velocity) = 0;
    virtual void setAngularVelocity(const glm::vec3& angularVelocity) = 0;
    virtual bool isGrounded() const = 0;
    virtual void destroy() = 0;
};

class PhysicsWorldBackend {
public:
    virtual ~PhysicsWorldBackend() = default;
    virtual void update(float deltaTime) = 0;
    virtual void setGravity(float gravity) = 0;
    virtual std::unique_ptr<PhysicsRigidBodyBackend> createBoxBody(const glm::vec3& halfExtents,
                                                                   float mass,
                                                                   const glm::vec3& position,
                                                                   const karma::physics::PhysicsMaterial& material) = 0;
    virtual std::unique_ptr<PhysicsPlayerControllerBackend> createPlayer(const glm::vec3& size) = 0;
    virtual std::unique_ptr<PhysicsStaticBodyBackend> createStaticMesh(const std::string& meshPath) = 0;
    virtual bool raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& hitPoint, glm::vec3& hitNormal) const = 0;
};

std::unique_ptr<PhysicsWorldBackend> CreatePhysicsWorldBackend();

} // namespace karma::physics_backend
