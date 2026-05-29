#pragma once

#include "karma/ecs/world.h"
#include "karma/scene/scene.h"

namespace karma::scene {

void updateWorldTransforms(ecs::World& world, const Scene& scene);

}  // namespace karma::scene
