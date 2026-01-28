#pragma once

#include <string>

#include "karma/ecs/component.h"

namespace karma::components {

struct CameraComponent : ecs::ComponentTag {
  float fov_y_degrees = 60.0f;
  float near_clip = 0.1f;
  float far_clip = 1000.0f;
  bool is_primary = false;
  bool render_to_texture = false;
  std::string render_target_key;
};

}  // namespace karma::components
