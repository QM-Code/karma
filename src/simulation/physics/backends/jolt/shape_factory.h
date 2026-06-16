#pragma once

#include <string>

#include "karma/simulation/physics/types.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>

namespace JPH {
class Shape;
class TempAllocator;
}

namespace karma::physics_backend::jolt {

JPH::RefConst<JPH::Shape> createShape(const karma::physics::PhysicsShapeDesc& desc,
                                      JPH::TempAllocator& temp_allocator,
                                      std::string& error);

}  // namespace karma::physics_backend::jolt
