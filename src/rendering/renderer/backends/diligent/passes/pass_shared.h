#pragma once

#include <cstddef>

namespace karma::renderer_backend {

struct alignas(16) LineConstants {
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

struct ParticleVertex {
  float corner[2] = {0.0f, 0.0f};
  float uv[2] = {0.0f, 0.0f};
};

using ParticleInstanceGpu = renderer::ParticlePackedInstance;

constexpr std::size_t kParticleQuadVertexCount = 6;

}  // namespace karma::renderer_backend
