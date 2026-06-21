#include "private/physics/backends/jolt/character_controller_jolt.hpp"
#include "private/physics/backends/jolt/physics_world_jolt.hpp"
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

namespace karma::physics::backend {

class PhysicsCharacterControllerJolt::ContactListener final : public JPH::CharacterContactListener {
public:
    explicit ContactListener(PhysicsCharacterControllerJolt& owner) : owner_(owner) {}

    void OnContactAdded(const JPH::CharacterVirtual* /*inCharacter*/,
                        const JPH::BodyID& inBodyID2,
                        const JPH::SubShapeID& /*inSubShapeID2*/,
                        JPH::RVec3Arg inContactPosition,
                        JPH::Vec3Arg inContactNormal,
                        JPH::CharacterContactSettings& /*ioSettings*/) override {
        const auto key = CharacterContactKey{.body = inBodyID2.GetIndexAndSequenceNumber()};
        std::lock_guard<std::mutex> lock(owner_.contacts_mutex_);
        owner_.contacts_[key] = karma::physics::PhysicsContact{
            .handle_a = owner_.nativeHandle(),
            .handle_b = static_cast<std::uintptr_t>(key.body),
            .point_a = glm::vec3(owner_.getPosition()),
            .point_b = glm::vec3(static_cast<float>(inContactPosition.GetX()),
                                 static_cast<float>(inContactPosition.GetY()),
                                 static_cast<float>(inContactPosition.GetZ())),
            .normal_a_to_b = glm::vec3(static_cast<float>(inContactNormal.GetX()),
                                       static_cast<float>(inContactNormal.GetY()),
                                       static_cast<float>(inContactNormal.GetZ())),
        };
    }

private:
    PhysicsCharacterControllerJolt& owner_;
};

PhysicsCharacterControllerJolt::PhysicsCharacterControllerJolt(PhysicsWorldJolt* world,
                                                         const glm::vec3& halfExtents,
                                                         const glm::vec3& startPosition)
    : world_(world), halfExtents(halfExtents) {
    if (!world_ || !world_->physicsSystem()) return;

    CharacterVirtualSettings settings;
    center = glm::vec3(0.0f, halfExtents.y, 0.0f);
    settings.mShape = makeBoxFromHalfExtents(halfExtents);
    settings.mMaxSlopeAngle = DegreesToRadians(50.0f);
    settings.mBackFaceMode = EBackFaceMode::IgnoreBackFaces;
    settings.mCharacterPadding = characterPadding;
    settings.mUp = Vec3::sAxisY();

    RVec3 start = RVec3(startPosition.x + center.x,
                        startPosition.y + center.y,
                        startPosition.z + center.z);
    character_ = new CharacterVirtual(&settings, start, Quat::sIdentity(), world_->physicsSystem());
    contactListener_ = std::make_unique<ContactListener>(*this);
    character_->SetListener(contactListener_.get());
}

PhysicsCharacterControllerJolt::~PhysicsCharacterControllerJolt() {
    destroy();
}

glm::vec3 PhysicsCharacterControllerJolt::getPosition() const {
    if (!character_) return glm::vec3(0.0f);
    auto pos = character_->GetPosition();
    return glm::vec3(static_cast<float>(pos.GetX()),
                     static_cast<float>(pos.GetY()),
                     static_cast<float>(pos.GetZ())) - center;
}

glm::quat PhysicsCharacterControllerJolt::getRotation() const { return rotation; }
glm::vec3 PhysicsCharacterControllerJolt::getVelocity() const { return velocity; }
glm::vec3 PhysicsCharacterControllerJolt::getAngularVelocity() const { return angularVelocity; }

glm::vec3 PhysicsCharacterControllerJolt::getForwardVector() const {
    return rotation * glm::vec3(0, 0, -1);
}

void PhysicsCharacterControllerJolt::setHalfExtents(const glm::vec3& extents) {
    const glm::vec3 position = getPosition();
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
    setPosition(position);
}

void PhysicsCharacterControllerJolt::setCenter(const glm::vec3& newCenter) {
    const glm::vec3 position = getPosition();
    center = newCenter;
    setPosition(position);
}

void PhysicsCharacterControllerJolt::setPosition(const glm::vec3& p) {
    if (!character_) return;
    character_->SetPosition(RVec3(p.x + center.x, p.y + center.y, p.z + center.z));
}

void PhysicsCharacterControllerJolt::setRotation(const glm::quat& r) {
    rotation = glm::normalize(r);
    if (character_) character_->SetRotation(toJph(rotation));
}

void PhysicsCharacterControllerJolt::setVelocity(const glm::vec3& v) { velocity = v; }
void PhysicsCharacterControllerJolt::setAngularVelocity(const glm::vec3& w) { angularVelocity = w; }

bool PhysicsCharacterControllerJolt::isGrounded() const {
    if (!character_) return false;
    using Ground = JPH::CharacterBase::EGroundState;
    Ground state = character_->GetGroundState();
    return state == Ground::OnGround || state == Ground::OnSteepGround;
}

bool PhysicsCharacterControllerJolt::getGroundContact(karma::physics::PhysicsGroundContact& outContact) const {
    if (!character_ || !isGrounded()) return false;

    outContact.grounded = true;
    outContact.point = toGlmRVec(character_->GetGroundPosition());
    outContact.normal = toGlm(character_->GetGroundNormal());
    outContact.support_handle =
        static_cast<std::uintptr_t>(character_->GetGroundBodyID().GetIndexAndSequenceNumber());
    return true;
}

void PhysicsCharacterControllerJolt::collectContacts(std::vector<karma::physics::PhysicsContact>& outContacts) const {
    std::lock_guard<std::mutex> lock(contacts_mutex_);
    outContacts.reserve(outContacts.size() + contacts_.size());
    for (const auto& [key, contact] : contacts_) {
        (void)key;
        outContacts.push_back(contact);
    }
}

std::uintptr_t PhysicsCharacterControllerJolt::nativeHandle() const {
    return reinterpret_cast<std::uintptr_t>(character_.GetPtr());
}

void PhysicsCharacterControllerJolt::update(float dt) {
    if (!world_ || !character_ || dt <= 0.f) return;

    {
        std::lock_guard<std::mutex> lock(contacts_mutex_);
        contacts_.clear();
    }

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

void PhysicsCharacterControllerJolt::destroy() {
    contactListener_.reset();
    character_ = nullptr;
    world_ = nullptr;
}

} // namespace karma::physics::backend
