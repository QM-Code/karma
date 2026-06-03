#pragma once

#include "karma/world/ecs/world.h"
#include "karma/world/scene/scene.h"

namespace karma::scene {

/// \ingroup karma_scene
/// Composes local scene transforms into world-space `TransformComponent` values.
///
/// Entities without a scene node keep their authored world transform. Entities
/// with `LocalTransformComponent` inherit their parent node transform when the
/// scene hierarchy is updated.
void updateWorldTransforms(ecs::World& world, const Scene& scene);

}  // namespace karma::scene
