#pragma once

#include "karma/core.h"
#include "karma/rendering.h"
#include "karma/world.h"

#include <filesystem>
#include <vector>

#include <Primitives/interface/BasicTypes.h>
#include <Platforms/interface/NativeWindow.h>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

struct aiScene;

struct GLFWwindow;

namespace karma::rendering::backend {

struct LoadedImage {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> pixels;
};

struct LoadedImageHDR {
  int width = 0;
  int height = 0;
  std::vector<float> pixels;
};

struct SubmeshInfo {
  Diligent::Uint32 index_offset = 0;
  Diligent::Uint32 index_count = 0;
  unsigned int material_index = 0;
};

struct DrawConstants {
  float mvp[16];
  float model[16];
  float light_view_proj[16];
  float shadow_uv_proj[16];
  float shadow_cascade_uv_proj[4][16];
  float point_shadow_uv_proj[96][16];
  float base_color_factor[4];
  float emissive_factor[4];
  float pbr_params[4];
  float env_params[4];
  float shadow_params[4];
  float point_shadow_params[4];
  float local_light_params[4];
  float point_shadow_tuning[4];
  float shadow_bias_params[4];
  float shadow_cascade_splits[4];
  float shadow_cascade_world_texel[4];
  float shadow_cascade_params[4];
  float light_dir[4];
  float light_color[4];
  float camera_pos[4];
  float camera_forward[4];
  float screen_params[4];
  float camera_clip_params[4];
  float forward_plus_params[4];
  float local_light_position_range[64][4];
  float local_light_direction_type[64][4];
  float local_light_color_intensity[64][4];
  float local_light_spot_params[64][4];
  float local_light_meta[4];
  float instance_params[4];
  float material_params0[4];
  float material_params1[4];
  float material_params2[4];
  float material_params3[4];
  float material_params4[4];
  float material_params5[4];
  float material_params6[4];
  float volume_params0[4];
  float volume_params1[4];
  float volume_params2[4];
  float volume_params3[4];
  float volume_params4[4];
  float texcoord_row0[12][4];
  float texcoord_row1[12][4];
};

struct DeformationConstants {
  float params[4];
};

struct ForwardPlusComputeConstants {
  float view_proj[16];
  float forward_plus_params[4];
  float screen_params[4];
};

struct InstancedGpuCullingConstants {
  float view_proj[16];
  float mesh_bounds[4];
  float camera_position[4];
  float distance_params[4];
  uint32_t params[4];
};

struct InstancedIndexedIndirectArgs {
  uint32_t num_indices = 0u;
  uint32_t num_instances = 0u;
  uint32_t first_index_location = 0u;
  uint32_t base_vertex = 0u;
  uint32_t first_instance_location = 0u;
};

struct alignas(16) CameraOverrideUserConstants {
  uint32_t user_key_hashes[rendering::kCameraShaderUserParamCapacity][4];
  float user_values[rendering::kCameraShaderUserParamCapacity][4];
  float user_meta[4];
};

bool isValidSize(int width, int height);
bool startupDiagnosticsEnabled();
bool renderResourceDiagnosticsEnabled();
bool renderPipelineDiagnosticsEnabled();
bool renderTextureImportDiagnosticsEnabled();
void logStartupDiag(const char* area,
                    const char* stage,
                    core::SteadyClock::time_point start,
                    core::SteadyClock::time_point end);
void logRenderResourceDiag(const char* area,
                           const char* stage,
                           core::SteadyClock::time_point start,
                           core::SteadyClock::time_point end);
void logRenderPipelineDiag(const char* area,
                           const char* stage,
                           core::SteadyClock::time_point start,
                           core::SteadyClock::time_point end);
void logRenderTextureImportDiag(const char* area,
                                const char* stage,
                                core::SteadyClock::time_point start,
                                core::SteadyClock::time_point end);
std::vector<unsigned char> readFileBytes(const std::filesystem::path& path);
LoadedImage loadImageFromMemory(const unsigned char* data, size_t size);
LoadedImage loadImageFromFile(const std::filesystem::path& path);
LoadedImageHDR loadImageFromFileHDR(const std::filesystem::path& path);

#if !defined(KARMA_WINDOW_BACKEND_SDL)
Diligent::NativeWindow toNativeWindow(GLFWwindow* window);
#endif

world::MeshData combineMeshes(const aiScene& scene,
                                 glm::vec4& out_color,
                                 std::vector<SubmeshInfo>& out_submeshes);
void copyMat4(float out[16], const glm::mat4& m);
std::vector<float> buildInterleavedVertices(const world::MeshData& mesh);

}  // namespace karma::rendering::backend
