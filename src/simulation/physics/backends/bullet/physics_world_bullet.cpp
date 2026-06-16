#include "karma/simulation/physics/backends/bullet/physics_world_bullet.hpp"
#include "karma/simulation/physics/backends/bullet/player_controller_bullet.hpp"
#include "karma/simulation/physics/backends/bullet/rigid_body_bullet.hpp"
#include "karma/simulation/physics/backends/bullet/static_body_bullet.hpp"
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <cmath>

namespace karma::physics_backend {

namespace {

class NullConstraintBackend final : public PhysicsConstraintBackend {
public:
    bool isValid() const override { return false; }
    void setEnabled(bool) override {}
    void destroy() override {}
    std::uintptr_t nativeHandle() const override { return 0; }
};

}  // namespace

PhysicsWorldBullet::PhysicsWorldBullet() {
    collisionConfig_ = std::make_unique<btDefaultCollisionConfiguration>();
    dispatcher_ = std::make_unique<btCollisionDispatcher>(collisionConfig_.get());
    broadphase_ = std::make_unique<btDbvtBroadphase>();
    solver_ = std::make_unique<btSequentialImpulseConstraintSolver>();
    dynamicsWorld_ = std::make_unique<btDiscreteDynamicsWorld>(dispatcher_.get(),
                                                               broadphase_.get(),
                                                               solver_.get(),
                                                               collisionConfig_.get());
    dynamicsWorld_->setGravity(btVector3(0.0f, gravity_, 0.0f));

    ghostPairCallback_ = std::make_unique<btGhostPairCallback>();
    dynamicsWorld_->getBroadphase()->getOverlappingPairCache()->setInternalGhostPairCallback(ghostPairCallback_.get());
}

PhysicsWorldBullet::~PhysicsWorldBullet() {
    dynamicsWorld_.reset();
    solver_.reset();
    broadphase_.reset();
    dispatcher_.reset();
    collisionConfig_.reset();
    ghostPairCallback_.reset();
}

void PhysicsWorldBullet::update(float deltaTime) {
    if (!dynamicsWorld_) return;
    dynamicsWorld_->stepSimulation(deltaTime, 1, 1.0f / 60.0f);
}

void PhysicsWorldBullet::setGravity(float gravity) {
    gravity_ = gravity;
    if (dynamicsWorld_) {
        dynamicsWorld_->setGravity(btVector3(0.0f, gravity_, 0.0f));
    }
}

std::unique_ptr<PhysicsRigidBodyBackend> PhysicsWorldBullet::createBody(
    const karma::physics::PhysicsBodyDesc& desc) {
    if (!dynamicsWorld_) return std::make_unique<PhysicsRigidBodyBullet>();

    std::unique_ptr<btCollisionShape> shape;
    switch (desc.shape.type) {
    case karma::physics::PhysicsShapeType::Sphere:
        shape = std::make_unique<btSphereShape>(std::max(desc.shape.radius, 0.001f));
        break;
    case karma::physics::PhysicsShapeType::Capsule:
        shape = std::make_unique<btCapsuleShape>(
            std::max(desc.shape.radius, 0.001f),
            std::max(desc.shape.height - desc.shape.radius * 2.0f, 0.0f));
        break;
    case karma::physics::PhysicsShapeType::Cylinder:
        shape = std::make_unique<btCylinderShape>(
            btVector3(std::max(desc.shape.radius, 0.001f),
                      std::max(desc.shape.height * 0.5f, 0.001f),
                      std::max(desc.shape.radius, 0.001f)));
        break;
    case karma::physics::PhysicsShapeType::Box:
    default:
        shape = std::make_unique<btBoxShape>(
            btVector3(std::max(std::abs(desc.shape.half_extents.x), 0.001f),
                      std::max(std::abs(desc.shape.half_extents.y), 0.001f),
                      std::max(std::abs(desc.shape.half_extents.z), 0.001f)));
        break;
    }

    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(btVector3(desc.position.x, desc.position.y, desc.position.z));
    transform.setRotation(btQuaternion(desc.rotation.x, desc.rotation.y, desc.rotation.z, desc.rotation.w));
    auto motionState = std::make_unique<btDefaultMotionState>(transform);

    const bool dynamic = desc.motion == karma::physics::PhysicsMotionType::Dynamic && desc.mass > 0.0f;
    const float mass = dynamic ? desc.mass : 0.0f;
    btVector3 inertia(0, 0, 0);
    if (mass > 0.0f) {
        shape->calculateLocalInertia(mass, inertia);
    }

    btRigidBody::btRigidBodyConstructionInfo info(mass, motionState.get(), shape.get(), inertia);
    info.m_friction = desc.material.friction;
    info.m_restitution = desc.material.restitution;
    info.m_linearDamping = desc.linear_damping;
    info.m_angularDamping = desc.angular_damping;

    auto body = std::make_unique<btRigidBody>(info);
    body->setLinearVelocity(btVector3(desc.linear_velocity.x, desc.linear_velocity.y, desc.linear_velocity.z));
    body->setAngularVelocity(btVector3(desc.angular_velocity.x, desc.angular_velocity.y, desc.angular_velocity.z));
    body->setGravity(btVector3(0.0f, gravity_ * desc.gravity_factor, 0.0f));
    if (desc.motion == karma::physics::PhysicsMotionType::Kinematic) {
        body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        body->setActivationState(DISABLE_DEACTIVATION);
    }
    if (desc.sensor) {
        body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
    }
    dynamicsWorld_->addRigidBody(body.get());

    return std::make_unique<PhysicsRigidBodyBullet>(this, std::move(body), std::move(shape), std::move(motionState));
}

std::unique_ptr<PhysicsConstraintBackend> PhysicsWorldBullet::createConstraint(
    const karma::physics::PhysicsConstraintDesc& /*desc*/,
    std::uintptr_t /*bodyA*/,
    std::uintptr_t /*bodyB*/) {
    return std::make_unique<NullConstraintBackend>();
}

std::unique_ptr<PhysicsRigidBodyBackend> PhysicsWorldBullet::createBoxBody(const glm::vec3& halfExtents,
                                                                           float mass,
                                                                           const glm::vec3& position,
                                                                           const karma::physics::PhysicsMaterial& material) {
    karma::physics::PhysicsBodyDesc desc;
    desc.shape.type = karma::physics::PhysicsShapeType::Box;
    desc.shape.half_extents = halfExtents;
    desc.position = position;
    desc.mass = mass;
    desc.material = material;
    return createBody(desc);
}

std::unique_ptr<PhysicsPlayerControllerBackend> PhysicsWorldBullet::createPlayer(const glm::vec3& size) {
    const glm::vec3 halfExtents = size * 0.5f;
    return std::make_unique<PhysicsPlayerControllerBullet>(this, halfExtents, glm::vec3(0.0f, 2.0f, 0.0f));
}

std::unique_ptr<PhysicsStaticBodyBackend> PhysicsWorldBullet::createStaticMesh(const std::string& meshPath) {
    return PhysicsStaticBodyBullet::fromMesh(this, meshPath);
}

bool PhysicsWorldBullet::raycast(const glm::vec3& from,
                                 const glm::vec3& to,
                                 glm::vec3& hitPoint,
                                 glm::vec3& hitNormal) const {
    if (!dynamicsWorld_) return false;

    btVector3 btFrom(from.x, from.y, from.z);
    btVector3 btTo(to.x, to.y, to.z);
    btCollisionWorld::ClosestRayResultCallback callback(btFrom, btTo);
    dynamicsWorld_->rayTest(btFrom, btTo, callback);
    if (!callback.hasHit()) {
        return false;
    }

    const btVector3& hit = callback.m_hitPointWorld;
    const btVector3& normal = callback.m_hitNormalWorld;
    hitPoint = glm::vec3(hit.x(), hit.y(), hit.z());
    hitNormal = glm::vec3(normal.x(), normal.y(), normal.z());
    return true;
}

bool PhysicsWorldBullet::raycastDetailed(const glm::vec3& from,
                                         const glm::vec3& to,
                                         karma::physics::PhysicsGroundContact& outHit) const {
    if (!dynamicsWorld_) return false;

    btVector3 btFrom(from.x, from.y, from.z);
    btVector3 btTo(to.x, to.y, to.z);
    btCollisionWorld::ClosestRayResultCallback callback(btFrom, btTo);
    dynamicsWorld_->rayTest(btFrom, btTo, callback);
    if (!callback.hasHit()) {
        return false;
    }

    const btVector3& hit = callback.m_hitPointWorld;
    const btVector3& normal = callback.m_hitNormalWorld;
    outHit.grounded = true;
    outHit.point = glm::vec3(hit.x(), hit.y(), hit.z());
    outHit.normal = glm::vec3(normal.x(), normal.y(), normal.z());
    outHit.support_handle = reinterpret_cast<std::uintptr_t>(callback.m_collisionObject);
    return true;
}

bool PhysicsWorldBullet::castRay(const karma::physics::PhysicsRaycastDesc& desc,
                                 karma::physics::PhysicsQueryHit& outHit) const {
    glm::vec3 point{};
    glm::vec3 normal{};
    if (!raycast(desc.from, desc.to, point, normal)) {
        return false;
    }
    outHit.point = point;
    outHit.point_on_body = point;
    outHit.normal = normal;
    const glm::vec3 ray = desc.to - desc.from;
    const float len_sq = glm::dot(ray, ray);
    outHit.fraction = len_sq > 0.0f ? glm::dot(point - desc.from, ray) / len_sq : 0.0f;
    return true;
}

void PhysicsWorldBullet::castRayAll(const karma::physics::PhysicsRaycastDesc& desc,
                                    std::vector<karma::physics::PhysicsQueryHit>& outHits) const {
    karma::physics::PhysicsQueryHit hit{};
    if (castRay(desc, hit)) {
        outHits.push_back(hit);
    }
}

void PhysicsWorldBullet::collidePoint(const glm::vec3& /*point*/,
                                      const karma::physics::PhysicsQueryFilter& /*filter*/,
                                      std::vector<karma::physics::PhysicsQueryHit>& /*outHits*/) const {}

void PhysicsWorldBullet::collideShape(const karma::physics::PhysicsShapeQueryDesc& /*desc*/,
                                      std::vector<karma::physics::PhysicsQueryHit>& /*outHits*/) const {}

void PhysicsWorldBullet::castShape(const karma::physics::PhysicsShapeCastDesc& /*desc*/,
                                   std::vector<karma::physics::PhysicsQueryHit>& /*outHits*/) const {}

void PhysicsWorldBullet::collectContacts(std::vector<karma::physics::PhysicsContact>& outContacts) const {
    if (!dynamicsWorld_ || !dispatcher_) {
        return;
    }

    const int manifold_count = dispatcher_->getNumManifolds();
    for (int i = 0; i < manifold_count; ++i) {
        btPersistentManifold* manifold = dispatcher_->getManifoldByIndexInternal(i);
        if (!manifold || manifold->getNumContacts() <= 0) {
            continue;
        }

        const btCollisionObject* object_a = manifold->getBody0();
        const btCollisionObject* object_b = manifold->getBody1();
        if (!object_a || !object_b) {
            continue;
        }

        const btManifoldPoint& point = manifold->getContactPoint(0);
        const btVector3 point_a = point.getPositionWorldOnA();
        const btVector3 point_b = point.getPositionWorldOnB();
        const btVector3 normal_on_b = point.m_normalWorldOnB;

        outContacts.push_back(karma::physics::PhysicsContact{
            .handle_a = reinterpret_cast<std::uintptr_t>(object_a),
            .handle_b = reinterpret_cast<std::uintptr_t>(object_b),
            .point_a = glm::vec3(point_a.x(), point_a.y(), point_a.z()),
            .point_b = glm::vec3(point_b.x(), point_b.y(), point_b.z()),
            .normal_a_to_b = glm::vec3(-normal_on_b.x(), -normal_on_b.y(), -normal_on_b.z()),
        });
    }
}

} // namespace karma::physics_backend
