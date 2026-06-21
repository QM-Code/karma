#include "private/physics/backend.hpp"

#if defined(KARMA_PHYSICS_BACKEND_JOLT)
#include "private/physics/backends/jolt/physics_world_jolt.hpp"
#elif defined(KARMA_PHYSICS_BACKEND_BULLET)
#include "private/physics/backends/bullet/physics_world_bullet.hpp"
#endif

namespace karma::physics::backend {

std::unique_ptr<PhysicsWorldBackend> CreatePhysicsWorldBackend() {
#if defined(KARMA_PHYSICS_BACKEND_JOLT)
    return std::make_unique<PhysicsWorldJolt>();
#elif defined(KARMA_PHYSICS_BACKEND_BULLET)
    return std::make_unique<PhysicsWorldBullet>();
#else
    return nullptr;
#endif
}

} // namespace karma::physics::backend
