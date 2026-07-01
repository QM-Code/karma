#pragma once

#include <cstddef>

namespace karma::rendering::backend {

struct alignas(16) LineConstants {
  float view_proj[16];
};

struct alignas(16) ParticleBeamConstants {
  float view_proj[16];
};

struct alignas(16) ParticleConstants {
  float view_proj[16];
  float camera_right[4];
  float camera_up[4];
  float camera_forward[4];
  float params[4];
  float screen_params[4];
  float camera_params[4];
  float camera_position[4];
  float shading_params[4];
  float presentation_params[4];
  float atlas_params0[4];
  float atlas_params1[4];
  float atlas_params2[4];
};

struct alignas(16) ParticleSimComputeConstants {
  float position_time[4];
  float rotation[4];
  float scale_seed[4];
  float playback[4];
  float emission[4];
  float lifetime[4];
  float size[4];
  float rotation_params[4];
  float spawn_box[4];
  float spawn_sphere[4];
  float source_params0[4];
  float source_params1[4];
  float source_params2[4];
  float source_mesh[4];
  float source_path0[4];
  float source_path1[4];
  float source_path2[4];
  float source_path3[4];
  float source_path4[4];
  float source_path5[4];
  float source_path6[4];
  float source_path7[4];
  float velocity_min[4];
  float velocity_max[4];
  float acceleration_drag[4];
  float orbit[4];
  float collision[4];
  float color_start[4];
  float color_end[4];
  float output[4];
};

struct ParticleVertex {
  float corner[2] = {0.0f, 0.0f};
  float uv[2] = {0.0f, 0.0f};
};

using ParticleInstanceGpu = rendering::ParticlePackedInstance;

struct alignas(16) ParticleGpuMeshSample {
  float p0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float p1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float p2[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct alignas(16) MorphTargetDeltaGpu {
  float position[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float normal[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float tangent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct alignas(16) ParticleGpuEmitterDesc {
  float position[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float scale_time[4] = {1.0f, 1.0f, 1.0f, 0.0f};
  float playback[4] = {0.0f, 1.0f, 0.0f, 0.0f};
  float emission[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float lifetime[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float size[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float rotation_params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float spawn_box[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float spawn_sphere[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float source_params0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float source_params1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float source_params2[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float source_mesh[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float source_path0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float source_path1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float source_path2[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float source_path3[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float source_path4[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float source_path5[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float source_path6[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float source_path7[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float velocity_min[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float velocity_max[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float acceleration_drag[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float orbit[4] = {0.0f, 1.0f, 0.0f, 0.0f};
  float collision[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float color_start[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float color_end[4] = {1.0f, 1.0f, 1.0f, 0.0f};
  uint32_t slot_offset = 0u;
  uint32_t slot_capacity = 0u;
  uint32_t group_index = 0u;
  uint32_t restart_count = 0u;
  uint32_t seed = 1u;
  uint32_t flags = 0u;
  uint32_t emitter_index = 0u;
  uint32_t emitter_state_index = 0u;
  uint32_t material_id = 0u;
  uint32_t source_mesh_sample_offset = 0u;
  uint32_t source_mesh_sample_count = 0u;
  uint32_t pad0 = 0u;
};

struct alignas(16) ParticleGpuEmitterState {
  float elapsed_seconds = 0.0f;
  float previous_elapsed_seconds = 0.0f;
  float spawn_accumulator = 0.0f;
  float pad0 = 0.0f;
  uint32_t restart_count = 0u;
  uint32_t burst_emitted = 0u;
  uint32_t spawn_budget = 0u;
  uint32_t spawned_cursor = 0u;
};

struct alignas(16) ParticleGpuState {
  float position_lifetime[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float velocity_age[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float color_start[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float color_end[4] = {1.0f, 1.0f, 1.0f, 0.0f};
  float rotation_size[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  uint32_t emitter_index = 0u;
  uint32_t group_index = 0u;
  uint32_t flags = 0u;
  uint32_t frame_offset = 0u;
};

struct alignas(16) ParticleGpuMaterialGroup {
  uint32_t instance_base = 0u;
  uint32_t sort_base = 0u;
  uint32_t max_particles = 0u;
  uint32_t sort_capacity = 0u;
  uint32_t flags = 0u;
  uint32_t pad0 = 0u;
  uint32_t pad1 = 0u;
  uint32_t pad2 = 0u;
};

struct alignas(16) ParticleGpuMaterialRecord {
  uint32_t texture_index = 0u;
  uint32_t alignment = 0u;
  uint32_t shading_mode = 0u;
  uint32_t presentation_mode = 0u;
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
  uint32_t animate_over_lifetime = 0u;
  uint32_t atlas_frame_width = 0u;
  uint32_t atlas_frame_height = 0u;
  uint32_t atlas_border_x = 0u;
  uint32_t atlas_border_y = 0u;
  float atlas_spacing_x = 0.0f;
  float atlas_spacing_y = 0.0f;
  float animation_fps = 0.0f;
  float use_soft_mask = 1.0f;
};

struct alignas(16) ParticleGpuFrameConstants {
  uint32_t emitter_count = 0u;
  uint32_t particle_capacity = 0u;
  uint32_t group_count = 0u;
  uint32_t sort_capacity = 0u;
  uint32_t global_sort_active = 0u;
  uint32_t grouped_sort_fallback = 0u;
  uint32_t pad0 = 0u;
  uint32_t pad1 = 0u;
  float camera_position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float camera_forward[4] = {0.0f, 0.0f, -1.0f, 0.0f};
};

struct alignas(16) ParticleGpuSortConstants {
  uint32_t sort_base = 0u;
  uint32_t sort_count_power2 = 0u;
  uint32_t k = 0u;
  uint32_t j = 0u;
};

struct alignas(16) ParticleGpuIndirectArgs {
  uint32_t num_vertices = 0u;
  uint32_t num_instances = 0u;
  uint32_t start_vertex = 0u;
  uint32_t first_instance = 0u;
};

struct alignas(16) ParticleGpuSortItem {
  uint32_t key = 0xFFFFFFFFu;
  uint32_t state_index = 0xFFFFFFFFu;
  uint32_t group_index = 0u;
  uint32_t material_id = 0u;
};

struct alignas(16) ParticleGpuStatsReadback {
  uint32_t particle_capacity = 0u;
  uint32_t alive_particles = 0u;
  uint32_t dead_particles = 0u;
  uint32_t spawned_particles = 0u;
  uint32_t killed_particles = 0u;
  uint32_t compacted_particles = 0u;
  uint32_t indirect_draws = 0u;
  uint32_t indirect_dispatches = 0u;
  uint32_t sort_key_count = 0u;
  uint32_t sort_overflow = 0u;
  uint32_t fallback_active = 0u;
  uint32_t culled_particles = 0u;
  uint32_t culling_dispatches = 0u;
  uint32_t global_sort_active = 0u;
  uint32_t grouped_sort_fallback = 0u;
  uint32_t pad = 0u;
};

constexpr std::size_t kParticleQuadVertexCount = 6;
constexpr std::size_t kParticleGpuTextureTableSize = 128;

}  // namespace karma::rendering::backend
