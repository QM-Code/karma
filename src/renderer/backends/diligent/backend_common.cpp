#include "karma/renderer/backends/diligent/backend.hpp"

#include "karma/platform/window.h"

#include "backend_internal.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#include <Primitives/interface/BasicTypes.h>
#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h>
#include <Platforms/interface/NativeWindow.h>

#include <Graphics/GraphicsTools/interface/MapHelper.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "../../../third_party/stb_image.h"
#if !defined(BZ3_WINDOW_BACKEND_SDL)
  #if defined(PLATFORM_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
    #define GLFW_EXPOSE_NATIVE_WGL
  #elif defined(PLATFORM_LINUX)
    #define GLFW_EXPOSE_NATIVE_X11
    #define GLFW_EXPOSE_NATIVE_GLX
  #elif defined(PLATFORM_MACOS)
    #define GLFW_EXPOSE_NATIVE_COCOA
    #define GLFW_EXPOSE_NATIVE_NSGL
  #endif
  #include <GLFW/glfw3.h>
  #include <GLFW/glfw3native.h>
#endif

namespace karma::renderer_backend {

bool isValidSize(int width, int height) {
  return width > 0 && height > 0;
}

std::vector<unsigned char> readFileBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

LoadedImage loadImageFromMemory(const unsigned char* data, size_t size) {
  LoadedImage image{};
  int w = 0;
  int h = 0;
  int comp = 0;
  stbi_set_flip_vertically_on_load(0);
  stbi_uc* decoded = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
  if (!decoded) {
    return image;
  }
  image.width = w;
  image.height = h;
  image.pixels.assign(decoded, decoded + (w * h * 4));
  stbi_image_free(decoded);
  return image;
}

LoadedImage loadImageFromFile(const std::filesystem::path& path) {
  const auto bytes = readFileBytes(path);
  if (bytes.empty()) {
    return {};
  }
  return loadImageFromMemory(bytes.data(), bytes.size());
}

LoadedImageHDR loadImageFromFileHDR(const std::filesystem::path& path) {
  LoadedImageHDR image{};
  int w = 0;
  int h = 0;
  int comp = 0;
  stbi_set_flip_vertically_on_load(1);
  float* decoded = stbi_loadf(path.string().c_str(), &w, &h, &comp, 4);
  if (!decoded) {
    return image;
  }
  image.width = w;
  image.height = h;
  image.pixels.assign(decoded, decoded + (w * h * 4));
  stbi_image_free(decoded);
  return image;
}

#if !defined(BZ3_WINDOW_BACKEND_SDL)
Diligent::NativeWindow toNativeWindow(GLFWwindow* window) {
#if defined(PLATFORM_WIN32)
  return Diligent::Win32NativeWindow{glfwGetWin32Window(window)};
#elif defined(PLATFORM_LINUX)
  Diligent::LinuxNativeWindow native_window{};
  native_window.WindowId = glfwGetX11Window(window);
  native_window.pDisplay = glfwGetX11Display();
  return native_window;
#elif defined(PLATFORM_MACOS)
  return Diligent::MacOSNativeWindow{glfwGetCocoaWindow(window)};
#else
  (void)window;
  return Diligent::NativeWindow{};
#endif
}
#endif

renderer::MeshData combineMeshes(const aiScene& scene,
                                 glm::vec4& out_color,
                                 std::vector<SubmeshInfo>& out_submeshes) {
  renderer::MeshData combined;
  out_color = glm::vec4(1.0f);
  bool has_color = false;
  out_submeshes.clear();

  for (unsigned int i = 0; i < scene.mNumMeshes; ++i) {
    const aiMesh* mesh = scene.mMeshes[i];
    if (!mesh) {
      continue;
    }

    if (!has_color && mesh->mMaterialIndex < scene.mNumMaterials && scene.mMaterials[mesh->mMaterialIndex]) {
      aiColor4D base_color(1.0f, 1.0f, 1.0f, 1.0f);
      if (scene.mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_BASE_COLOR, base_color) == AI_SUCCESS) {
        out_color = glm::vec4(base_color.r, base_color.g, base_color.b, base_color.a);
        has_color = true;
      } else {
        aiColor3D diffuse(1.0f, 1.0f, 1.0f);
        if (scene.mMaterials[mesh->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
          out_color = glm::vec4(diffuse.r, diffuse.g, diffuse.b, 1.0f);
          has_color = true;
        }
      }
    }

    const size_t base_vertex = combined.vertices.size();
    combined.vertices.reserve(base_vertex + mesh->mNumVertices);
    combined.normals.reserve(base_vertex + mesh->mNumVertices);
    combined.uvs.reserve(base_vertex + mesh->mNumVertices);
    combined.tangents.reserve(base_vertex + mesh->mNumVertices);

    for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
      const auto& vert = mesh->mVertices[v];
      combined.vertices.emplace_back(vert.x, vert.y, vert.z);

      if (mesh->HasNormals()) {
        const auto& n = mesh->mNormals[v];
        combined.normals.emplace_back(n.x, n.y, n.z);
      } else {
        combined.normals.emplace_back(0.0f, 1.0f, 0.0f);
      }

      if (mesh->HasTextureCoords(0)) {
        const auto& uv = mesh->mTextureCoords[0][v];
        combined.uvs.emplace_back(uv.x, uv.y);
      } else {
        combined.uvs.emplace_back(0.0f, 0.0f);
      }

      if (mesh->HasTangentsAndBitangents()) {
        const auto& t = mesh->mTangents[v];
        const auto& b = mesh->mBitangents[v];
        const glm::vec3 tangent(t.x, t.y, t.z);
        const glm::vec3 bitangent(b.x, b.y, b.z);
        const auto& n = mesh->mNormals[v];
        const glm::vec3 normal(n.x, n.y, n.z);
        const float sign = (glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f) ? -1.0f : 1.0f;
        combined.tangents.emplace_back(tangent.x, tangent.y, tangent.z, sign);
      } else {
        combined.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
      }
    }

    const Diligent::Uint32 index_offset = static_cast<Diligent::Uint32>(combined.indices.size());
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
      const aiFace& face = mesh->mFaces[f];
      for (unsigned int idx = 0; idx < face.mNumIndices; ++idx) {
        combined.indices.push_back(static_cast<uint32_t>(base_vertex + face.mIndices[idx]));
      }
    }
    const Diligent::Uint32 index_count =
        static_cast<Diligent::Uint32>(combined.indices.size()) - index_offset;
    if (index_count > 0) {
      SubmeshInfo info{};
      info.index_offset = index_offset;
      info.index_count = index_count;
      info.material_index = mesh->mMaterialIndex;
      out_submeshes.push_back(info);
    }
  }

  return combined;
}

void copyMat4(float out[16], const glm::mat4& m) {
  const float* ptr = glm::value_ptr(m);
  for (int i = 0; i < 16; ++i) {
    out[i] = ptr[i];
  }
}

std::vector<float> buildInterleavedVertices(const renderer::MeshData& mesh) {
  const bool has_normals = mesh.normals.size() == mesh.vertices.size();
  const bool has_uvs = mesh.uvs.size() == mesh.vertices.size();
  const bool has_tangents = mesh.tangents.size() == mesh.vertices.size();
  const size_t stride = 12;
  std::vector<float> data;
  data.reserve(mesh.vertices.size() * stride);
  for (size_t i = 0; i < mesh.vertices.size(); ++i) {
    const auto& v = mesh.vertices[i];
    data.push_back(v.x);
    data.push_back(v.y);
    data.push_back(v.z);
    const glm::vec3 n = has_normals ? mesh.normals[i] : glm::vec3(0.0f, 1.0f, 0.0f);
    data.push_back(n.x);
    data.push_back(n.y);
    data.push_back(n.z);
    const glm::vec4 t = has_tangents ? mesh.tangents[i] : glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    data.push_back(t.x);
    data.push_back(t.y);
    data.push_back(t.z);
    data.push_back(t.w);
    const glm::vec2 uv = has_uvs ? mesh.uvs[i] : glm::vec2(0.0f, 0.0f);
    data.push_back(uv.x);
    data.push_back(uv.y);
  }
  return data;
}

DiligentBackend::DiligentBackend(karma::platform::Window& window)
    : window_(&window) {
  if (const char* env = std::getenv("KARMA_SHADOW_DEBUG")) {
    shadow_debug_ = std::string(env) != "0";
  }
  int fb_width = 800;
  int fb_height = 600;
  window_->getFramebufferSize(fb_width, fb_height);
  if (fb_height == 0) {
    fb_height = 1;
  }
  current_width_ = fb_width;
  current_height_ = fb_height;
  initializeDevice();
}

DiligentBackend::~DiligentBackend() {
}

}  // namespace karma::renderer_backend
