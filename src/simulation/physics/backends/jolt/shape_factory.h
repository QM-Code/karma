#pragma once

#include <string>

#include "karma/physics.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>

namespace JPH {
class Shape;
class TempAllocator;
}

namespace karma::physics::backend::jolt {

JPH::RefConst<JPH::Shape> createShape(const karma::physics::PhysicsShapeDesc& desc,
                                      JPH::TempAllocator& temp_allocator,
                                      std::string& error);

}  // namespace karma::physics::backend::jolt
