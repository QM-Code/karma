#pragma once

#include "karma/renderer/types.h"

#include <filesystem>
#include <vector>

#include <Primitives/interface/BasicTypes.h>
#include <Platforms/interface/NativeWindow.h>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

struct aiScene;

struct GLFWwindow;

namespace karma::renderer_backend {

struct LoadedImage {
  int width = 0;
  int height = 0;
  std::vector<unsigned char> pixels;
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
  float base_color_factor[4];
  float emissive_factor[4];
  float pbr_params[4];
  float env_params[4];
  float shadow_params[4];
  float light_dir[4];
  float light_color[4];
  float camera_pos[4];
};

bool isValidSize(int width, int height);
std::vector<unsigned char> readFileBytes(const std::filesystem::path& path);
LoadedImage loadImageFromMemory(const unsigned char* data, size_t size);
LoadedImage loadImageFromFile(const std::filesystem::path& path);

#if !defined(BZ3_WINDOW_BACKEND_SDL)
Diligent::NativeWindow toNativeWindow(GLFWwindow* window);
#endif

renderer::MeshData combineMeshes(const aiScene& scene,
                                 glm::vec4& out_color,
                                 std::vector<SubmeshInfo>& out_submeshes);
void copyMat4(float out[16], const glm::mat4& m);
std::vector<float> buildInterleavedVertices(const renderer::MeshData& mesh);

}  // namespace karma::renderer_backend
