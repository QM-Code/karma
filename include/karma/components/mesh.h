#pragma once

#include <string>

#include "karma/ecs/component.h"

namespace karma::components {

struct MeshComponent : ecs::ComponentTag {
  std::string mesh_key;
  std::string material_key;
  std::string texture_key;
  bool visible = true;
};

}  // namespace karma::components
