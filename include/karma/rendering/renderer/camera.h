#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "karma/core/math/types.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace karma::renderer {

/// \ingroup karma_rendering
/// Maximum number of camera shader color parameters.
static constexpr uint32_t kCameraShaderUserParamCapacity = 32;

/// Hashes a camera shader parameter key with FNV-1a.
inline uint32_t cameraShaderParamKeyHash(std::string_view key) {
  uint32_t hash = 2166136261u;
  for (char c : key) {
    hash ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
    hash *= 16777619u;
  }
  return hash;
}

/// \ingroup karma_rendering
/// One hashed camera shader parameter.
struct CameraShaderUserParam {
  uint32_t key_hash = 0u;
  math::Color value{};
};

/// \ingroup karma_rendering
/// Renderer-facing camera state extracted from ECS camera/transform data.
struct CameraData {
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  bool perspective = true;
  bool render_shadows = true;
  float fov_y_degrees = 60.0f;
  float aspect = 1.0f;
  float near_clip = 0.1f;
  float far_clip = 1000.0f;
  float ortho_left = -1.0f;
  float ortho_right = 1.0f;
  float ortho_top = 1.0f;
  float ortho_bottom = -1.0f;
  std::filesystem::path shader_override_vertex_path;
  std::filesystem::path shader_override_fragment_path;
  std::array<CameraShaderUserParam, kCameraShaderUserParamCapacity> shader_user_params{};
  uint32_t shader_user_param_count = 0;
};

}  // namespace karma::renderer
