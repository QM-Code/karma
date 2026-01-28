#pragma once

namespace karma::physics {

struct PhysicsMaterial {
    float friction = 0.0f;
    float restitution = 0.0f;
    float rollingFriction = 0.0f;
    float spinningFriction = 0.0f;
};

} // namespace karma::physics
