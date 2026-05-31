#pragma once

#include <cstdint>
#include <vector>

#include "karma/core/math/types.h"
#include "karma/rendering/renderer/ids.h"

#include <glm/glm.hpp>

namespace karma::renderer {

enum class ParticleBlendMode : uint32_t {
  Additive = 0,
  Alpha = 1,
  Distortion = 2,
};

enum class ParticleAlignment : uint32_t {
  Billboard = 0,
  Ground = 1,
};

enum class ParticleShadingMode : uint32_t {
  Standard = 0,
  Shell = 1,
};

enum class ParticlePresentationMode : uint32_t {
  Baked = 0,
  Simulated = 1,
};

struct ParticleInstance {
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  float size = 1.0f;
  math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float rotation_radians = 0.0f;
  glm::vec2 uv_min{0.0f, 0.0f};
  glm::vec2 uv_max{1.0f, 1.0f};
  glm::vec2 uv_min_next{0.0f, 0.0f};
  glm::vec2 uv_max_next{1.0f, 1.0f};
  float frame_blend = 0.0f;
  math::Color color_end{1.0f, 1.0f, 1.0f, 1.0f};
  float size_end = 1.0f;
  float normalized_age = 0.0f;
  float age_seconds = 0.0f;
  uint32_t frame_offset = 0u;
};

struct alignas(16) ParticlePackedInstance {
  float position_age[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float color_start[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float color_end[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float rotation_size[4] = {1.0f, 0.0f, 1.0f, 1.0f};
  float uv_rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  float uv_rect_next[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct ParticleBatch {
  LayerId layer = 0;
  bool depth_test = true;
  TextureId texture = kInvalidTexture;
  ParticleBlendMode blend_mode = ParticleBlendMode::Additive;
  ParticleAlignment alignment = ParticleAlignment::Billboard;
  ParticleShadingMode shading_mode = ParticleShadingMode::Standard;
  ParticlePresentationMode presentation_mode = ParticlePresentationMode::Baked;
  bool use_soft_mask = true;
  float soft_particle_distance = 0.0f;
  float distortion_strength = 0.0f;
  float fresnel_power = 4.0f;
  float fresnel_strength = 1.0f;
  float refraction_strength = 0.0f;
  float interior_glow = 0.0f;
  float size_curve_exponent = 1.0f;
  float alpha_curve_exponent = 1.0f;
  uint32_t atlas_columns = 1u;
  uint32_t atlas_rows = 1u;
  uint32_t atlas_frame_count = 1u;
  bool animate_over_lifetime = false;
  uint32_t atlas_frame_width = 0u;
  uint32_t atlas_frame_height = 0u;
  uint32_t atlas_border_x = 0u;
  uint32_t atlas_border_y = 0u;
  uint32_t atlas_spacing_x = 0u;
  uint32_t atlas_spacing_y = 0u;
  float animation_fps = 0.0f;
  std::vector<ParticleInstance> particles;
};

struct PackedParticleBatch {
  LayerId layer = 0;
  bool depth_test = true;
  TextureId texture = kInvalidTexture;
  ParticleBlendMode blend_mode = ParticleBlendMode::Additive;
  ParticleAlignment alignment = ParticleAlignment::Billboard;
  ParticleShadingMode shading_mode = ParticleShadingMode::Standard;
  ParticlePresentationMode presentation_mode = ParticlePresentationMode::Baked;
  bool use_soft_mask = true;
  float soft_particle_distance = 0.0f;
  float distortion_strength = 0.0f;
  float fresnel_power = 4.0f;
  float fresnel_strength = 1.0f;
  float refraction_strength = 0.0f;
  float interior_glow = 0.0f;
  float size_curve_exponent = 1.0f;
  float alpha_curve_exponent = 1.0f;
  uint32_t atlas_columns = 1u;
  uint32_t atlas_rows = 1u;
  uint32_t atlas_frame_count = 1u;
  bool animate_over_lifetime = false;
  uint32_t atlas_frame_width = 0u;
  uint32_t atlas_frame_height = 0u;
  uint32_t atlas_border_x = 0u;
  uint32_t atlas_border_y = 0u;
  uint32_t atlas_spacing_x = 0u;
  uint32_t atlas_spacing_y = 0u;
  float animation_fps = 0.0f;
  std::vector<ParticlePackedInstance> particles;
};

}  // namespace karma::renderer
