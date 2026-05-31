#pragma once

#include "karma/world/ecs/world.h"
#include "karma/world/scene/scene.h"

namespace karma::scene {

void updateWorldTransforms(ecs::World& world, const Scene& scene);

}  // namespace karma::scene
