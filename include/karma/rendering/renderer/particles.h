#pragma once

#include <cstdint>
#include <vector>

#include "karma/core/math/types.h"
#include "karma/rendering/renderer/ids.h"

#include <glm/glm.hpp>

namespace karma::renderer {

/// \ingroup karma_rendering
/// Particle blend path selected by particle emitters/batches.
enum class ParticleBlendMode : uint32_t {
  Additive = 0,
  Alpha = 1,
  Distortion = 2,
};

/// \ingroup karma_rendering
/// Particle orientation mode.
enum class ParticleAlignment : uint32_t {
  Billboard = 0,
  Ground = 1,
};

/// \ingroup karma_rendering
/// Particle shader family.
enum class ParticleShadingMode : uint32_t {
  Standard = 0,
  Shell = 1,
};

/// \ingroup karma_rendering
/// Particle source shape for renderer-owned particle simulation.
enum class ParticleSourceShape : uint32_t {
  Box = 0,
  Sphere = 1,
  SphereSurface = 2,
  Disc = 3,
  Ring = 4,
  Cylinder = 5,
  Capsule = 6,
  Cone = 7,
  Line = 8,
  Path = 9,
  TrailPath = 10,
  MeshSurface = 11,
};

/// \ingroup karma_rendering
/// Particle source sampling policy.
enum class ParticleSourceSamplingMode : uint32_t {
  Random = 0,
  Sequential = 1,
  Vertices = 2,
};

/// \ingroup karma_rendering
/// Particle source emission distribution.
enum class ParticleSourceDistribution : uint32_t {
  Uniform = 0,
  Surface = 1,
  Edge = 2,
};

/// \ingroup karma_rendering
/// Whether particle presentation was pre-baked or should be evaluated on GPU.
enum class ParticlePresentationMode : uint32_t {
  Baked = 0,
  Simulated = 1,
};

/// \ingroup karma_rendering
/// Compatibility particle instance with fully evaluated presentation values.
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

/// \ingroup karma_rendering
/// Packed particle instance uploaded to the renderer's particle path.
struct alignas(16) ParticlePackedInstance {
  float position_age[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float color_start[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float color_end[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float rotation_size[4] = {1.0f, 0.0f, 1.0f, 1.0f};
  float uv_rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  float uv_rect_next[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

/// \ingroup karma_rendering
/// Compatibility particle batch submitted by non-simulated producers.
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

/// \ingroup karma_rendering
/// Packed particle batch submitted by `ParticleSystem`.
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

/// \ingroup karma_rendering
/// Renderer-facing particle emitter submission for GPU-first effects.
///
/// This is intentionally plain data: feature systems resolve ECS bindings,
/// library assets, overrides, and transforms, then submit this descriptor to the
/// renderer backend. Backends own live particle state for the descriptor.
struct ParticleEmitterGpuDesc {
  uint64_t instance_id = 0;
  uint32_t restart_count = 0;
  float delta_seconds = 0.0f;
  bool visible = true;

  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};

  bool enabled = true;
  bool playing = true;
  bool loop = true;
  bool emit_burst_on_start = true;
  bool local_space = false;
  LayerId layer = 0;
  bool depth_test = true;
  ParticleBlendMode blend_mode = ParticleBlendMode::Additive;
  ParticleAlignment alignment = ParticleAlignment::Billboard;
  ParticleShadingMode shading_mode = ParticleShadingMode::Standard;
  bool use_soft_mask = true;
  float soft_particle_distance = 0.0f;
  float distortion_strength = 0.0f;
  float fresnel_power = 4.0f;
  float fresnel_strength = 1.0f;
  float refraction_strength = 0.0f;
  float interior_glow = 0.0f;
  TextureId texture = kInvalidTexture;
  uint32_t atlas_columns = 1;
  uint32_t atlas_rows = 1;
  uint32_t atlas_frame_count = 0;
  uint32_t atlas_frame_width = 0;
  uint32_t atlas_frame_height = 0;
  uint32_t atlas_border_x = 0;
  uint32_t atlas_border_y = 0;
  uint32_t atlas_spacing_x = 0;
  uint32_t atlas_spacing_y = 0;
  float animation_fps = 0.0f;
  bool animate_over_lifetime = false;
  bool random_start_frame = false;
  uint32_t max_particles = 256;
  uint32_t burst_count = 0;
  uint32_t seed = 0;
  float time_scale = 1.0f;
  float start_delay = 0.0f;
  float duration = 0.0f;
  float spawn_rate = 32.0f;
  float particle_lifetime_min = 0.65f;
  float particle_lifetime_max = 1.15f;
  float start_size_min = 0.18f;
  float start_size_max = 0.32f;
  float end_size_min = 0.03f;
  float end_size_max = 0.10f;
  float size_curve_exponent = 1.0f;
  float alpha_curve_exponent = 1.0f;
  float initial_rotation_min = 0.0f;
  float initial_rotation_max = 6.2831853f;
  float angular_velocity_min = -1.5f;
  float angular_velocity_max = 1.5f;
  ParticleSourceShape source_shape = ParticleSourceShape::Box;
  math::Vec3 source_box_extents{0.0f, 0.0f, 0.0f};
  math::Vec3 source_dimensions{0.0f, 0.0f, 0.0f};
  float source_radius_min = 0.0f;
  float source_radius_max = 0.0f;
  float source_inner_radius = 0.0f;
  float source_outer_radius = 0.0f;
  float source_height = 0.0f;
  float source_angle = 0.0f;
  std::vector<math::Vec3> source_path_points;
  bool source_closed_loop = false;
  ParticleSourceSamplingMode source_sampling = ParticleSourceSamplingMode::Random;
  float source_jitter_radius = 0.0f;
  MeshId source_mesh = kInvalidMesh;
  math::Vec3 source_mesh_bounds_center{0.0f, 0.0f, 0.0f};
  float source_mesh_bounds_radius = 0.0f;
  ParticleSourceDistribution source_distribution = ParticleSourceDistribution::Uniform;
  float radial_speed_min = 0.0f;
  float radial_speed_max = 0.0f;
  math::Vec3 velocity_min{-0.6f, 2.5f, -0.6f};
  math::Vec3 velocity_max{0.6f, 4.5f, 0.6f};
  math::Vec3 acceleration{0.0f, -3.5f, 0.0f};
  float drag = 0.0f;
  bool collide_with_ground = false;
  float ground_height = 0.0f;
  float bounce_damping = 0.35f;
  float collision_friction = 0.25f;
  float rest_speed_threshold = 0.35f;
  math::Color start_color{1.0f, 0.8f, 0.35f, 0.9f};
  math::Color end_color{1.0f, 0.15f, 0.05f, 0.0f};
};

}  // namespace karma::renderer
