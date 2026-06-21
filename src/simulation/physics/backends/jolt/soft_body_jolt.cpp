#include "private/physics/backends/jolt/soft_body_jolt.hpp"

#include "private/physics/backends/jolt/physics_world_jolt.hpp"
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace {

using ObjectLayer = JPH::ObjectLayer;
constexpr ObjectLayer Moving = 1;
constexpr float Pi = 3.14159265358979323846f;

JPH::Vec3 toJph(const glm::vec3& value) {
    return JPH::Vec3(value.x, value.y, value.z);
}

JPH::Quat toJph(const glm::quat& value) {
    return JPH::Quat(value.x, value.y, value.z, value.w);
}

glm::vec3 toGlm(const JPH::Vec3& value) {
    return {value.GetX(), value.GetY(), value.GetZ()};
}

glm::vec3 toGlmRVec(const JPH::RVec3& value) {
    return {
        static_cast<float>(value.GetX()),
        static_cast<float>(value.GetY()),
        static_cast<float>(value.GetZ()),
    };
}

glm::quat toGlm(const JPH::Quat& value) {
    return {value.GetW(), value.GetX(), value.GetY(), value.GetZ()};
}

JPH::Float3 toFloat3(const glm::vec3& value) {
    return {value.x, value.y, value.z};
}

JPH::SoftBodySharedSettings::ELRAType toJph(karma::physics::PhysicsSoftBodyLraType value) {
    switch (value) {
    case karma::physics::PhysicsSoftBodyLraType::None:
        return JPH::SoftBodySharedSettings::ELRAType::None;
    case karma::physics::PhysicsSoftBodyLraType::EuclideanDistance:
        return JPH::SoftBodySharedSettings::ELRAType::EuclideanDistance;
    case karma::physics::PhysicsSoftBodyLraType::GeodesicDistance:
        return JPH::SoftBodySharedSettings::ELRAType::GeodesicDistance;
    }
    return JPH::SoftBodySharedSettings::ELRAType::None;
}

JPH::SoftBodySharedSettings::EBendType toJph(karma::physics::PhysicsSoftBodyBendType value) {
    switch (value) {
    case karma::physics::PhysicsSoftBodyBendType::None:
        return JPH::SoftBodySharedSettings::EBendType::None;
    case karma::physics::PhysicsSoftBodyBendType::Distance:
        return JPH::SoftBodySharedSettings::EBendType::Distance;
    case karma::physics::PhysicsSoftBodyBendType::Dihedral:
        return JPH::SoftBodySharedSettings::EBendType::Dihedral;
    }
    return JPH::SoftBodySharedSettings::EBendType::Distance;
}

void addVertex(JPH::SoftBodySharedSettings& settings,
               const glm::vec3& position,
               const glm::vec3& velocity = glm::vec3(0.0f),
               float inverse_mass = 1.0f) {
    settings.mVertices.emplace_back(toFloat3(position), toFloat3(velocity), inverse_mass);
}

bool validTriangle(uint32_t a, uint32_t b, uint32_t c, size_t vertex_count) {
    return a < vertex_count && b < vertex_count && c < vertex_count &&
           a != b && a != c && b != c;
}

void addFace(JPH::SoftBodySharedSettings& settings,
             uint32_t a,
             uint32_t b,
             uint32_t c,
             uint32_t material = 0) {
    if (validTriangle(a, b, c, settings.mVertices.size())) {
        settings.AddFace({a, b, c, material});
    }
}

void addQuad(JPH::SoftBodySharedSettings& settings,
             uint32_t a,
             uint32_t b,
             uint32_t c,
             uint32_t d) {
    addFace(settings, a, b, c);
    addFace(settings, a, c, d);
}

void appendCustom(JPH::SoftBodySharedSettings& settings,
                  const karma::physics::PhysicsSoftBodyDesc& desc) {
    settings.mVertices.reserve(desc.vertices.size());
    for (const auto& vertex : desc.vertices) {
        addVertex(settings, vertex.position, vertex.velocity, vertex.inverse_mass);
    }

    settings.mFaces.reserve(desc.faces.size());
    for (const auto& face : desc.faces) {
        addFace(settings, face.vertex0, face.vertex1, face.vertex2, face.material_index);
    }

    settings.mEdgeConstraints.reserve(desc.edges.size());
    for (const auto& edge : desc.edges) {
        if (edge.vertex0 < settings.mVertices.size() && edge.vertex1 < settings.mVertices.size() &&
            edge.vertex0 != edge.vertex1) {
            settings.mEdgeConstraints.emplace_back(edge.vertex0, edge.vertex1, edge.compliance);
        }
    }

    settings.mVolumeConstraints.reserve(desc.volumes.size());
    for (const auto& volume : desc.volumes) {
        if (volume.vertex0 < settings.mVertices.size() &&
            volume.vertex1 < settings.mVertices.size() &&
            volume.vertex2 < settings.mVertices.size() &&
            volume.vertex3 < settings.mVertices.size()) {
            settings.mVolumeConstraints.emplace_back(volume.vertex0,
                                                     volume.vertex1,
                                                     volume.vertex2,
                                                     volume.vertex3,
                                                     volume.compliance);
        }
    }
}

void appendCloth(JPH::SoftBodySharedSettings& settings,
                 const karma::physics::PhysicsSoftBodyDesc& desc) {
    const uint32_t nx = std::max(desc.grid_size_x, 2u);
    const uint32_t ny = std::max(desc.grid_size_y, 2u);
    const float spacing = std::max(desc.grid_spacing, 0.001f);
    const glm::vec3 offset{
        -0.5f * spacing * static_cast<float>(nx - 1u),
        0.0f,
        -0.5f * spacing * static_cast<float>(ny - 1u),
    };

    settings.mVertices.reserve(static_cast<size_t>(nx) * ny);
    for (uint32_t y = 0; y < ny; ++y) {
        for (uint32_t x = 0; x < nx; ++x) {
            addVertex(settings, offset + glm::vec3(x * spacing, 0.0f, y * spacing));
        }
    }

    for (uint32_t y = 0; y + 1u < ny; ++y) {
        for (uint32_t x = 0; x + 1u < nx; ++x) {
            const uint32_t a = y * nx + x;
            const uint32_t b = y * nx + x + 1u;
            const uint32_t c = (y + 1u) * nx + x + 1u;
            const uint32_t d = (y + 1u) * nx + x;
            addQuad(settings, a, b, c, d);
        }
    }

    if (desc.pin_cloth_corners) {
        settings.mVertices[0].mInvMass = 0.0f;
        settings.mVertices[nx - 1u].mInvMass = 0.0f;
        settings.mVertices[(ny - 1u) * nx].mInvMass = 0.0f;
        settings.mVertices[ny * nx - 1u].mInvMass = 0.0f;
    }
}

void appendCube(JPH::SoftBodySharedSettings& settings,
                const karma::physics::PhysicsSoftBodyDesc& desc) {
    const uint32_t nx = std::max(desc.grid_size_x, 2u);
    const uint32_t ny = std::max(desc.grid_size_y, 2u);
    const uint32_t nz = std::max(desc.grid_size_z, 2u);
    const float spacing = std::max(desc.grid_spacing, 0.001f);
    const glm::vec3 offset{
        -0.5f * spacing * static_cast<float>(nx - 1u),
        -0.5f * spacing * static_cast<float>(ny - 1u),
        -0.5f * spacing * static_cast<float>(nz - 1u),
    };

    auto index = [nx, ny](uint32_t x, uint32_t y, uint32_t z) {
        return z * nx * ny + y * nx + x;
    };

    settings.mVertices.reserve(static_cast<size_t>(nx) * ny * nz);
    for (uint32_t z = 0; z < nz; ++z) {
        for (uint32_t y = 0; y < ny; ++y) {
            for (uint32_t x = 0; x < nx; ++x) {
                addVertex(settings, offset + glm::vec3(x * spacing, y * spacing, z * spacing));
            }
        }
    }

    for (uint32_t z = 0; z + 1u < nz; ++z) {
        for (uint32_t y = 0; y + 1u < ny; ++y) {
            addQuad(settings, index(0, y, z), index(0, y + 1u, z), index(0, y + 1u, z + 1u), index(0, y, z + 1u));
            addQuad(settings, index(nx - 1u, y, z), index(nx - 1u, y, z + 1u), index(nx - 1u, y + 1u, z + 1u), index(nx - 1u, y + 1u, z));
        }
    }
    for (uint32_t z = 0; z + 1u < nz; ++z) {
        for (uint32_t x = 0; x + 1u < nx; ++x) {
            addQuad(settings, index(x, 0, z), index(x, 0, z + 1u), index(x + 1u, 0, z + 1u), index(x + 1u, 0, z));
            addQuad(settings, index(x, ny - 1u, z), index(x + 1u, ny - 1u, z), index(x + 1u, ny - 1u, z + 1u), index(x, ny - 1u, z + 1u));
        }
    }
    for (uint32_t y = 0; y + 1u < ny; ++y) {
        for (uint32_t x = 0; x + 1u < nx; ++x) {
            addQuad(settings, index(x, y, 0), index(x + 1u, y, 0), index(x + 1u, y + 1u, 0), index(x, y + 1u, 0));
            addQuad(settings, index(x, y, nz - 1u), index(x, y + 1u, nz - 1u), index(x + 1u, y + 1u, nz - 1u), index(x + 1u, y, nz - 1u));
        }
    }

    for (uint32_t z = 0; z + 1u < nz; ++z) {
        for (uint32_t y = 0; y + 1u < ny; ++y) {
            for (uint32_t x = 0; x + 1u < nx; ++x) {
                const uint32_t v000 = index(x, y, z);
                const uint32_t v100 = index(x + 1u, y, z);
                const uint32_t v010 = index(x, y + 1u, z);
                const uint32_t v110 = index(x + 1u, y + 1u, z);
                const uint32_t v001 = index(x, y, z + 1u);
                const uint32_t v101 = index(x + 1u, y, z + 1u);
                const uint32_t v011 = index(x, y + 1u, z + 1u);
                const uint32_t v111 = index(x + 1u, y + 1u, z + 1u);
                settings.mVolumeConstraints.emplace_back(v000, v100, v010, v001, 0.0f);
                settings.mVolumeConstraints.emplace_back(v100, v110, v010, v111, 0.0f);
                settings.mVolumeConstraints.emplace_back(v100, v010, v001, v111, 0.0f);
                settings.mVolumeConstraints.emplace_back(v010, v001, v011, v111, 0.0f);
                settings.mVolumeConstraints.emplace_back(v100, v001, v101, v111, 0.0f);
            }
        }
    }
}

void appendSphere(JPH::SoftBodySharedSettings& settings,
                  const karma::physics::PhysicsSoftBodyDesc& desc) {
    const uint32_t theta = std::max(desc.sphere_theta, 3u);
    const uint32_t phi = std::max(desc.sphere_phi, 3u);
    const float radius = std::max(desc.radius, 0.001f);

    const uint32_t top = static_cast<uint32_t>(settings.mVertices.size());
    addVertex(settings, glm::vec3(0.0f, radius, 0.0f));

    auto ring_index = [theta](uint32_t p, uint32_t t) {
        return 1u + (p - 1u) * theta + (t % theta);
    };

    for (uint32_t p = 1; p < phi; ++p) {
        const float v = static_cast<float>(p) / static_cast<float>(phi);
        const float polar = v * Pi;
        const float y = std::cos(polar) * radius;
        const float ring_radius = std::sin(polar) * radius;
        for (uint32_t t = 0; t < theta; ++t) {
            const float u = static_cast<float>(t) / static_cast<float>(theta);
            const float azimuth = u * Pi * 2.0f;
            addVertex(settings, {std::cos(azimuth) * ring_radius, y, std::sin(azimuth) * ring_radius});
        }
    }

    const uint32_t bottom = static_cast<uint32_t>(settings.mVertices.size());
    addVertex(settings, glm::vec3(0.0f, -radius, 0.0f));

    for (uint32_t t = 0; t < theta; ++t) {
        addFace(settings, top, ring_index(1, t + 1u), ring_index(1, t));
    }
    for (uint32_t p = 1; p + 1u < phi; ++p) {
        for (uint32_t t = 0; t < theta; ++t) {
            addQuad(settings,
                    ring_index(p, t),
                    ring_index(p, t + 1u),
                    ring_index(p + 1u, t + 1u),
                    ring_index(p + 1u, t));
        }
    }
    for (uint32_t t = 0; t < theta; ++t) {
        addFace(settings, bottom, ring_index(phi - 1u, t), ring_index(phi - 1u, t + 1u));
    }

    const uint32_t center = static_cast<uint32_t>(settings.mVertices.size());
    addVertex(settings, glm::vec3(0.0f));
    for (const auto& face : settings.mFaces) {
        settings.mVolumeConstraints.emplace_back(face.mVertex[0], face.mVertex[1], face.mVertex[2], center, 0.0f);
    }
}

void applyPinnedVertices(JPH::SoftBodySharedSettings& settings,
                         const std::vector<uint32_t>& pinned_vertices) {
    for (uint32_t vertex : pinned_vertices) {
        if (vertex < settings.mVertices.size()) {
            settings.mVertices[vertex].mInvMass = 0.0f;
        }
    }
}

JPH::Ref<JPH::SoftBodySharedSettings> makeSharedSettings(
    const karma::physics::PhysicsSoftBodyDesc& desc) {
    JPH::Ref<JPH::SoftBodySharedSettings> settings = new JPH::SoftBodySharedSettings();

    switch (desc.preset) {
    case karma::physics::PhysicsSoftBodyPreset::Custom:
        appendCustom(*settings, desc);
        break;
    case karma::physics::PhysicsSoftBodyPreset::Cloth:
        appendCloth(*settings, desc);
        break;
    case karma::physics::PhysicsSoftBodyPreset::Cube:
        appendCube(*settings, desc);
        break;
    case karma::physics::PhysicsSoftBodyPreset::Sphere:
        appendSphere(*settings, desc);
        break;
    }

    applyPinnedVertices(*settings, desc.pinned_vertices);

    if (settings->mVertices.empty()) {
        return nullptr;
    }

    if (desc.create_constraints && !settings->mFaces.empty()) {
        const JPH::SoftBodySharedSettings::VertexAttributes attributes(
            desc.vertex_attributes.compliance,
            desc.vertex_attributes.shear_compliance,
            desc.vertex_attributes.bend_compliance,
            toJph(desc.vertex_attributes.lra_type),
            desc.vertex_attributes.lra_max_distance_multiplier);
        settings->CreateConstraints(&attributes, 1u, toJph(desc.bend_type), desc.angle_tolerance);
    }

    if (!settings->mEdgeConstraints.empty()) {
        settings->CalculateEdgeLengths();
    }
    if (!settings->mVolumeConstraints.empty()) {
        settings->CalculateVolumeConstraintVolumes();
    }
    if (desc.optimize) {
        settings->Optimize();
    }

    return settings;
}

JPH::SoftBodyMotionProperties* softBodyMotion(JPH::Body& body) {
    if (!body.IsSoftBody()) {
        return nullptr;
    }
    return static_cast<JPH::SoftBodyMotionProperties*>(body.GetMotionProperties());
}

const JPH::SoftBodyMotionProperties* softBodyMotion(const JPH::Body& body) {
    if (!body.IsSoftBody()) {
        return nullptr;
    }
    return static_cast<const JPH::SoftBodyMotionProperties*>(body.GetMotionProperties());
}

}  // namespace

namespace karma::physics::backend {

PhysicsSoftBodyJolt::PhysicsSoftBodyJolt(PhysicsWorldJolt* world, JPH::BodyID body)
    : world_(world), body_(body) {}

PhysicsSoftBodyJolt::~PhysicsSoftBodyJolt() {
    destroy();
}

bool PhysicsSoftBodyJolt::isValid() const {
    return world_ != nullptr && body_.has_value() && !body_->IsInvalid();
}

glm::vec3 PhysicsSoftBodyJolt::getPosition() const {
    if (!isValid() || world_->physicsSystem() == nullptr) {
        return glm::vec3(0.0f);
    }
    JPH::BodyLockRead lock(world_->physicsSystem()->GetBodyLockInterface(), *body_);
    return lock.Succeeded() ? toGlmRVec(lock.GetBody().GetPosition()) : glm::vec3(0.0f);
}

glm::quat PhysicsSoftBodyJolt::getRotation() const {
    if (!isValid() || world_->physicsSystem() == nullptr) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    JPH::BodyLockRead lock(world_->physicsSystem()->GetBodyLockInterface(), *body_);
    return lock.Succeeded() ? toGlm(lock.GetBody().GetRotation())
                            : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

bool PhysicsSoftBodyJolt::isActive() const {
    if (!isValid() || world_->physicsSystem() == nullptr) {
        return false;
    }
    return world_->physicsSystem()->GetBodyInterface().IsActive(*body_);
}

void PhysicsSoftBodyJolt::setPressure(float pressure) {
    if (!isValid() || world_->physicsSystem() == nullptr) {
        return;
    }
    JPH::BodyLockWrite lock(world_->physicsSystem()->GetBodyLockInterface(), *body_);
    if (lock.Succeeded()) {
        if (auto* motion = softBodyMotion(lock.GetBody())) {
            motion->SetPressure(pressure);
        }
    }
}

void PhysicsSoftBodyJolt::setUpdatePosition(bool updatePosition) {
    if (!isValid() || world_->physicsSystem() == nullptr) {
        return;
    }
    JPH::BodyLockWrite lock(world_->physicsSystem()->GetBodyLockInterface(), *body_);
    if (lock.Succeeded()) {
        if (auto* motion = softBodyMotion(lock.GetBody())) {
            motion->SetUpdatePosition(updatePosition);
        }
    }
}

void PhysicsSoftBodyJolt::setEnableSkinConstraints(bool enabled) {
    if (!isValid() || world_->physicsSystem() == nullptr) {
        return;
    }
    JPH::BodyLockWrite lock(world_->physicsSystem()->GetBodyLockInterface(), *body_);
    if (lock.Succeeded()) {
        if (auto* motion = softBodyMotion(lock.GetBody())) {
            motion->SetEnableSkinConstraints(enabled);
        }
    }
}

void PhysicsSoftBodyJolt::setSkinnedMaxDistanceMultiplier(float multiplier) {
    if (!isValid() || world_->physicsSystem() == nullptr) {
        return;
    }
    JPH::BodyLockWrite lock(world_->physicsSystem()->GetBodyLockInterface(), *body_);
    if (lock.Succeeded()) {
        if (auto* motion = softBodyMotion(lock.GetBody())) {
            motion->SetSkinnedMaxDistanceMultiplier(multiplier);
        }
    }
}

void PhysicsSoftBodyJolt::setVertexPosition(uint32_t vertex, const glm::vec3& position, bool hardSkin) {
    if (!isValid() || world_->physicsSystem() == nullptr) {
        return;
    }
    bool edited = false;
    {
        JPH::BodyLockWrite lock(world_->physicsSystem()->GetBodyLockInterface(), *body_);
        if (!lock.Succeeded()) {
            return;
        }
        JPH::Body& body = lock.GetBody();
        auto* motion = softBodyMotion(body);
        if (motion == nullptr || vertex >= motion->GetVertices().size()) {
            return;
        }

        JPH::SoftBodyVertex& v = motion->GetVertex(vertex);
        const JPH::RVec3 world_position(position.x, position.y, position.z);
        const JPH::Vec3 local = body.GetRotation().Conjugated() *
                                JPH::Vec3(world_position - body.GetCenterOfMassPosition());
        v.mPosition = local;
        if (hardSkin) {
            v.mPreviousPosition = local;
            v.mVelocity = JPH::Vec3::sZero();
        }
        edited = true;
    }
    if (edited) {
        world_->physicsSystem()->GetBodyInterface().ActivateBody(*body_);
    }
}

karma::physics::PhysicsSoftBodyState PhysicsSoftBodyJolt::getState() const {
    karma::physics::PhysicsSoftBodyState state;
    if (!isValid() || world_->physicsSystem() == nullptr) {
        return state;
    }

    JPH::BodyLockRead lock(world_->physicsSystem()->GetBodyLockInterface(), *body_);
    if (!lock.Succeeded()) {
        return state;
    }

    const JPH::Body& body = lock.GetBody();
    const auto* motion = softBodyMotion(body);
    if (motion == nullptr) {
        return state;
    }

    state.valid = true;
    state.active = body.IsActive();
    state.handle = nativeHandle();
    state.position = toGlmRVec(body.GetPosition());
    state.rotation = toGlm(body.GetRotation());
    state.volume = motion->GetVolume();
    state.solver_iterations = motion->GetNumIterations();
    state.pressure = motion->GetPressure();
    state.update_position = motion->GetUpdatePosition();

    const JPH::RMat44 transform = body.GetCenterOfMassTransform();
    const auto& vertices = motion->GetVertices();
    state.vertices.reserve(vertices.size());
    for (const JPH::SoftBodyVertex& vertex : vertices) {
        state.vertices.push_back({
            .position = toGlmRVec(transform * vertex.mPosition),
            .velocity = toGlm(body.GetRotation() * vertex.mVelocity),
            .inverse_mass = vertex.mInvMass,
        });
    }

    const auto& faces = motion->GetFaces();
    state.indices.reserve(faces.size() * 3u);
    for (const auto& face : faces) {
        state.indices.push_back(face.mVertex[0]);
        state.indices.push_back(face.mVertex[1]);
        state.indices.push_back(face.mVertex[2]);
    }

    return state;
}

void PhysicsSoftBodyJolt::activate() {
    if (isValid() && world_->physicsSystem() != nullptr) {
        world_->physicsSystem()->GetBodyInterface().ActivateBody(*body_);
    }
}

void PhysicsSoftBodyJolt::deactivate() {
    if (isValid() && world_->physicsSystem() != nullptr) {
        world_->physicsSystem()->GetBodyInterface().DeactivateBody(*body_);
    }
}

void PhysicsSoftBodyJolt::destroy() {
    if (world_ != nullptr && body_.has_value()) {
        world_->removeBody(*body_);
    }
    body_.reset();
    world_ = nullptr;
}

std::uintptr_t PhysicsSoftBodyJolt::nativeHandle() const {
    return isValid() ? static_cast<std::uintptr_t>(body_->GetIndexAndSequenceNumber()) : 0;
}

std::unique_ptr<PhysicsSoftBodyBackend> PhysicsWorldJolt::createSoftBody(
    const karma::physics::PhysicsSoftBodyDesc& desc) {
    if (physicsSystem() == nullptr) {
        return std::make_unique<PhysicsSoftBodyJolt>();
    }

    JPH::Ref<JPH::SoftBodySharedSettings> shared = makeSharedSettings(desc);
    if (shared == nullptr) {
        spdlog::warn("Failed to create Jolt soft body: no vertices");
        return std::make_unique<PhysicsSoftBodyJolt>();
    }

    JPH::SoftBodyCreationSettings settings(shared.GetPtr(),
                                           JPH::RVec3(desc.position.x, desc.position.y, desc.position.z),
                                           toJph(desc.rotation),
                                           Moving);
    settings.mUserData = desc.user_data;
    settings.mNumIterations = desc.solver_iterations;
    settings.mLinearDamping = desc.linear_damping;
    settings.mMaxLinearVelocity = desc.max_linear_velocity;
    settings.mRestitution = desc.material.restitution;
    settings.mFriction = desc.material.friction;
    settings.mPressure = desc.pressure;
    settings.mGravityFactor = desc.gravity_factor;
    settings.mVertexRadius = desc.vertex_radius;
    settings.mUpdatePosition = desc.update_position;
    settings.mMakeRotationIdentity = desc.make_rotation_identity;
    settings.mAllowSleeping = desc.allow_sleeping;

    JPH::BodyID body = physicsSystem()->GetBodyInterface().CreateAndAddSoftBody(
        settings,
        desc.activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    if (body.IsInvalid()) {
        spdlog::error("Failed to create Jolt soft body");
        return std::make_unique<PhysicsSoftBodyJolt>();
    }

    registerBodyFilter(body, desc.collision_filter);
    return std::make_unique<PhysicsSoftBodyJolt>(this, body);
}

}  // namespace karma::physics::backend
