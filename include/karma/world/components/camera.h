#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "karma/world/ecs/component.h"
#include "karma/rendering/renderer/ids.h"

namespace karma::components {

/// \ingroup karma_components
/// Camera authoring data extracted by `RenderSystem`.
///
/// A scene should usually have one primary camera. Cameras can render to the
/// default target or to named render targets, and may provide shader override
/// paths plus small color parameter payloads for custom camera effects.
struct CameraComponent : ecs::ComponentTag {
  bool perspective = true;
  bool render_shadows = true;
  float fov_y_degrees = 60.0f;
  float near_clip = 0.1f;
  float far_clip = 1000.0f;
  float ortho_left = -1.0f;
  float ortho_right = 1.0f;
  float ortho_top = 1.0f;
  float ortho_bottom = -1.0f;
  bool is_primary = false;
  bool render_to_texture = false;
  renderer::RenderTargetId render_target = renderer::kDefaultRenderTarget;
  std::string render_target_key;
  std::filesystem::path shader_override_vertex_path;
  std::filesystem::path shader_override_fragment_path;
  std::unordered_map<std::string, math::Color> shader_user_params;
};

}  // namespace karma::components
