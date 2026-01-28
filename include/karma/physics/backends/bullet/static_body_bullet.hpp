#pragma once

#include "karma/physics/backend.hpp"
#include <memory>

class btCollisionShape;
class btRigidBody;
class btTriangleMesh;
class btMotionState;

namespace karma::physics_backend {

class PhysicsWorldBullet;

class PhysicsStaticBodyBullet final : public PhysicsStaticBodyBackend {
public:
    PhysicsStaticBodyBullet() = default;
    PhysicsStaticBodyBullet(PhysicsWorldBullet* world,
                            std::unique_ptr<btRigidBody> body,
                            std::unique_ptr<btCollisionShape> shape,
                            std::unique_ptr<btTriangleMesh> mesh,
                            std::unique_ptr<btMotionState> motionState);
    ~PhysicsStaticBodyBullet() override;

    bool isValid() const override;
    glm::vec3 getPosition() const override;
    glm::quat getRotation() const override;
    void destroy() override;
    std::uintptr_t nativeHandle() const override;

    static std::unique_ptr<PhysicsStaticBodyBackend> fromMesh(PhysicsWorldBullet* world, const std::string& meshPath);

private:
    PhysicsWorldBullet* world_ = nullptr;
    std::unique_ptr<btRigidBody> body_;
    std::unique_ptr<btCollisionShape> shape_;
    std::unique_ptr<btTriangleMesh> mesh_;
    std::unique_ptr<btMotionState> motionState_;
};

} // namespace karma::physics_backend
