#include "karma/physics/backends/jolt/player_controller_jolt.hpp"
#include "karma/physics/backends/jolt/physics_world_jolt.hpp"
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Geometry/Plane.h>
#include <glm/gtc/quaternion.hpp>
#include <cfloat>
#include <algorithm>

namespace {
using namespace JPH;

inline Vec3 toJph(const glm::vec3& v) { return Vec3(v.x, v.y, v.z); }
inline Quat toJph(const glm::quat& q) { return Quat(q.x, q.y, q.z, q.w); }
inline glm::vec3 toGlm(const Vec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
inline glm::vec3 toGlmRVec(const RVec3& v) { return glm::vec3(static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())); }

RefConst<Shape> makeBoxFromHalfExtents(const glm::vec3& halfExtents) {
    return new BoxShape(toJph(halfExtents));
}
} // namespace

namespace karma::physics_backend {

PhysicsPlayerControllerJolt::PhysicsPlayerControllerJolt(PhysicsWorldJolt* world,
                                                         const glm::vec3& halfExtents,
                                                         const glm::vec3& startPosition)
    : world_(world), halfExtents(halfExtents) {
    if (!world_ || !world_->physicsSystem()) return;

    CharacterVirtualSettings settings;
    settings.mShape = makeBoxFromHalfExtents(halfExtents);
    settings.mMaxSlopeAngle = DegreesToRadians(50.0f);
    settings.mBackFaceMode = EBackFaceMode::IgnoreBackFaces;
    settings.mCharacterPadding = characterPadding;
    settings.mUp = Vec3::sAxisY();

    RVec3 start = RVec3(startPosition.x, startPosition.y + halfExtents.y, startPosition.z);
    character_ = new CharacterVirtual(&settings, start, Quat::sIdentity(), world_->physicsSystem());
}

PhysicsPlayerControllerJolt::~PhysicsPlayerControllerJolt() {
    destroy();
}

glm::vec3 PhysicsPlayerControllerJolt::getPosition() const {
    if (!character_) return glm::vec3(0.0f);
    auto pos = character_->GetPosition();
    float offsetY = halfExtents.y + characterPadding;
    return glm::vec3(static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()), static_cast<float>(pos.GetZ())) - glm::vec3(0.0f, offsetY, 0.0f);
}

glm::quat PhysicsPlayerControllerJolt::getRotation() const { return rotation; }
glm::vec3 PhysicsPlayerControllerJolt::getVelocity() const { return velocity; }
glm::vec3 PhysicsPlayerControllerJolt::getAngularVelocity() const { return angularVelocity; }

glm::vec3 PhysicsPlayerControllerJolt::getForwardVector() const {
    return rotation * glm::vec3(0, 0, -1);
}

void PhysicsPlayerControllerJolt::setHalfExtents(const glm::vec3& extents) {
    halfExtents = extents;

    if (!character_) return;

    RefConst<Shape> newShape = makeBoxFromHalfExtents(extents);
    JPH::TempAllocator* allocator = world_ ? world_->tempAllocator() : nullptr;
    if (!allocator) return;

    BroadPhaseLayerFilter bpFilter;
    ObjectLayerFilter objFilter;
    BodyFilter bodyFilter;
    ShapeFilter shapeFilter;
    character_->SetShape(newShape.GetPtr(), FLT_MAX, bpFilter, objFilter, bodyFilter, shapeFilter, *allocator);
}

void PhysicsPlayerControllerJolt::setPosition(const glm::vec3& p) {
    if (!character_) return;
    float offsetY = halfExtents.y + characterPadding;
    character_->SetPosition(RVec3(p.x, p.y + offsetY, p.z));
}

void PhysicsPlayerControllerJolt::setRotation(const glm::quat& r) {
    rotation = glm::normalize(r);
    if (character_) character_->SetRotation(toJph(rotation));
}

void PhysicsPlayerControllerJolt::setVelocity(const glm::vec3& v) { velocity = v; }
void PhysicsPlayerControllerJolt::setAngularVelocity(const glm::vec3& w) { angularVelocity = w; }

bool PhysicsPlayerControllerJolt::isGrounded() const {
    if (!character_) return false;
    using Ground = JPH::CharacterBase::EGroundState;
    Ground state = character_->GetGroundState();
    if (state != Ground::OnGround) return false;

    RVec3 groundPos = character_->GetGroundPosition();
    RVec3 charPos = character_->GetPosition();
    glm::mat3 invRot = glm::mat3_cast(glm::conjugate(rotation));
    glm::vec3 local = invRot * toGlmRVec(groundPos - charPos);
    float supportCeiling = -halfExtents.y + groundSupportBand;
    return local.y <= supportCeiling;
}

void PhysicsPlayerControllerJolt::update(float dt) {
    if (!world_ || !character_ || dt <= 0.f) return;

    Vec3 gravityVec = world_->physicsSystem() ? world_->physicsSystem()->GetGravity() : Vec3(0, gravity, 0);
    const bool grounded = isGrounded();
    if (!(grounded && velocity.y <= 0.0f)) {
        velocity += toGlm(gravityVec) * dt;
    } else if (velocity.y < 0.0f) {
        velocity.y = 0.0f;
    }

    character_->SetRotation(toJph(rotation));
    glm::vec3 preUpdateVelocity = velocity;
    character_->SetLinearVelocity(toJph(velocity));

    CharacterVirtual::ExtendedUpdateSettings updateSettings;
    if (!grounded) {
        updateSettings.mWalkStairsStepUp = Vec3::sZero();
    }
    BroadPhaseLayerFilter bpFilter;
    ObjectLayerFilter objFilter;
    BodyFilter bodyFilter;
    ShapeFilter shapeFilter;
    JPH::TempAllocator* allocator = world_->tempAllocator();
    if (!allocator) return;
    character_->ExtendedUpdate(dt,
                               gravityVec,
                               updateSettings,
                               bpFilter,
                               objFilter,
                               bodyFilter,
                               shapeFilter,
                               *allocator);

    velocity = toGlm(character_->GetLinearVelocity());

    if (!grounded) {
        glm::vec3 preH = glm::vec3(preUpdateVelocity.x, 0.0f, preUpdateVelocity.z);
        glm::vec3 postH = glm::vec3(velocity.x, 0.0f, velocity.z);
        float preLen = glm::length(preH);
        float postLen = glm::length(postH);
        if (preLen > 1e-4f && postLen > 1e-4f) {
            float align = glm::dot(preH / preLen, postH / postLen);
            if (align < 0.8f || postLen < preLen * 0.5f) {
                angularVelocity = glm::vec3(0.0f);
            }
        } else if (preLen > 1e-3f && postLen < 1e-3f) {
            angularVelocity = glm::vec3(0.0f);
        }
    }

    if (glm::dot(angularVelocity, angularVelocity) > 0.f) {
        glm::quat dq = glm::quat(0, angularVelocity.x, angularVelocity.y, angularVelocity.z) * rotation;
        rotation = glm::normalize(rotation + 0.5f * dq * dt);
    }
}

void PhysicsPlayerControllerJolt::destroy() {
    character_ = nullptr;
    world_ = nullptr;
}

} // namespace karma::physics_backend
