#pragma once

#include "karma/physics/backend.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <optional>

namespace karma::physics_backend {

class PhysicsWorldJolt;

class PhysicsStaticBodyJolt final : public PhysicsStaticBodyBackend {
public:
    PhysicsStaticBodyJolt() = default;
    PhysicsStaticBodyJolt(PhysicsWorldJolt* world, const JPH::BodyID& bodyId);
    ~PhysicsStaticBodyJolt() override;

    bool isValid() const override;
    glm::vec3 getPosition() const override;
    glm::quat getRotation() const override;
    void destroy() override;
    std::uintptr_t nativeHandle() const override;

    static std::unique_ptr<PhysicsStaticBodyBackend> fromMesh(PhysicsWorldJolt* world, const std::string& meshPath);

private:
    PhysicsWorldJolt* world_ = nullptr;
    std::optional<JPH::BodyID> body_;
};

} // namespace karma::physics_backend
