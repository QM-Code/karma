#pragma once

#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Marks an entity transform as the active audio listener.
struct AudioListenerComponent : ecs::ComponentTag {};

}  // namespace karma::components
