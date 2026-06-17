#include "backend.hpp"

#include "karma/platform/window/window.h"

#include "backend_internal.h"

#include "karma/core/time.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <string_view>

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
#include <spdlog/spdlog.h>

#include "../../../../../third_party/stb_image.h"
#if !defined(KARMA_WINDOW_BACKEND_SDL)
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

namespace {
struct FileInfo {
  bool exists = false;
  std::uintmax_t size = 0;
};

std::filesystem::path defaultShaderCachePath(std::uint32_t version) {
  const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
  const char* home = std::getenv("HOME");
  std::filesystem::path base;
  if (xdg_cache && xdg_cache[0] != '\0') {
    base = xdg_cache;
  } else if (home && home[0] != '\0') {
    base = std::filesystem::path(home) / ".cache";
  } else {
    base = "cache";
  }
  const std::string filename = "karma_shader_cache_v" + std::to_string(version) + ".diligentcache";
  return base / "karma" / filename;
}

bool envFlagEnabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "FALSE") != 0 && std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "OFF") != 0;
}

FileInfo inspectFile(const std::filesystem::path& path) {
  FileInfo info{};
  if (path.empty()) {
    return info;
  }
  std::error_code ec;
  info.exists = std::filesystem::exists(path, ec);
  if (!info.exists || ec) {
    return info;
  }
  info.size = std::filesystem::file_size(path, ec);
  if (ec) {
    info.size = 0;
  }
  return info;
}
}  // namespace

bool isValidSize(int width, int height) {
  return width > 0 && height > 0;
}

bool startupDiagnosticsEnabled() {
  static const bool enabled =
      envFlagEnabled(std::getenv("KARMA_ENGINE_STARTUP_DIAG")) ||
      envFlagEnabled(std::getenv("KARMA_RENDER_STARTUP_DIAG"));
  return enabled;
}

bool renderResourceDiagnosticsEnabled() {
  static const bool enabled =
      startupDiagnosticsEnabled() || envFlagEnabled(std::getenv("KARMA_RENDER_RESOURCE_DIAG"));
  return enabled;
}

bool renderPipelineDiagnosticsEnabled() {
  static const bool enabled =
      startupDiagnosticsEnabled() || envFlagEnabled(std::getenv("KARMA_RENDER_PIPELINE_DIAG"));
  return enabled;
}

bool renderTextureImportDiagnosticsEnabled() {
  static const bool enabled =
      renderResourceDiagnosticsEnabled() ||
      envFlagEnabled(std::getenv("KARMA_RENDER_TEXTURE_IMPORT_DIAG"));
  return enabled;
}

void logStartupDiag(const char* area,
                    const char* stage,
                    core::SteadyClock::time_point start,
                    core::SteadyClock::time_point end) {
  if (!startupDiagnosticsEnabled()) {
    return;
  }
  spdlog::info("Engine startup diag: area={} stage={} ms={:.2f}",
               area ? area : "unknown",
               stage ? stage : "unknown",
               core::elapsedMilliseconds(start, end));
}

void logRenderResourceDiag(const char* area,
                           const char* stage,
                           core::SteadyClock::time_point start,
                           core::SteadyClock::time_point end) {
  if (!renderResourceDiagnosticsEnabled()) {
    return;
  }
  spdlog::info("Render resource diag: area={} stage={} ms={:.2f}",
               area ? area : "unknown",
               stage ? stage : "unknown",
               core::elapsedMilliseconds(start, end));
}

void logRenderPipelineDiag(const char* area,
                           const char* stage,
                           core::SteadyClock::time_point start,
                           core::SteadyClock::time_point end) {
  if (!renderPipelineDiagnosticsEnabled()) {
    return;
  }
  spdlog::info("Render pipeline diag: area={} stage={} ms={:.2f}",
               area ? area : "unknown",
               stage ? stage : "unknown",
               core::elapsedMilliseconds(start, end));
}

void logRenderTextureImportDiag(const char* area,
                                const char* stage,
                                core::SteadyClock::time_point start,
                                core::SteadyClock::time_point end) {
  if (!renderTextureImportDiagnosticsEnabled()) {
    return;
  }
  spdlog::info("Render texture import diag: area={} stage={} ms={:.2f}",
               area ? area : "unknown",
               stage ? stage : "unknown",
               core::elapsedMilliseconds(start, end));
}

std::vector<unsigned char> readFileBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  if (size <= 0 ||
      static_cast<unsigned long long>(size) >
          static_cast<unsigned long long>(std::numeric_limits<size_t>::max()) ||
      static_cast<unsigned long long>(size) >
          static_cast<unsigned long long>(std::numeric_limits<std::streamsize>::max())) {
    return {};
  }
  file.seekg(0, std::ios::beg);

  std::vector<unsigned char> bytes(static_cast<size_t>(size));
  file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    return {};
  }
  return bytes;
}

LoadedImage loadImageFromMemory(const unsigned char* data, size_t size) {
  LoadedImage image{};
  int w = 0;
  int h = 0;
  int comp = 0;
  stbi_set_flip_vertically_on_load_thread(1);
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

#if !defined(KARMA_WINDOW_BACKEND_SDL)
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

geometry::MeshData combineMeshes(const aiScene& scene,
                                 glm::vec4& out_color,
                                 std::vector<SubmeshInfo>& out_submeshes) {
  geometry::MeshData combined;
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
    combined.uvs1.reserve(base_vertex + mesh->mNumVertices);
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

      if (mesh->HasTextureCoords(1)) {
        const auto& uv = mesh->mTextureCoords[1][v];
        combined.uvs1.emplace_back(uv.x, uv.y);
      } else {
        combined.uvs1.emplace_back(combined.uvs.back());
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

std::vector<float> buildInterleavedVertices(const geometry::MeshData& mesh) {
  const bool has_normals = mesh.normals.size() == mesh.vertices.size();
  const bool has_uvs = mesh.uvs.size() == mesh.vertices.size();
  const bool has_uvs1 = mesh.uvs1.size() == mesh.vertices.size();
  const bool has_tangents = mesh.tangents.size() == mesh.vertices.size();
  const bool has_joint_indices = mesh.joint_indices.size() == mesh.vertices.size();
  const bool has_joint_weights = mesh.joint_weights.size() == mesh.vertices.size();
  const size_t stride = 22;
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
    const glm::vec2 uv1 = has_uvs1 ? mesh.uvs1[i] : uv;
    data.push_back(uv1.x);
    data.push_back(uv1.y);
    const glm::uvec4 joints = has_joint_indices ? mesh.joint_indices[i] : glm::uvec4(0u);
    data.push_back(static_cast<float>(joints.x));
    data.push_back(static_cast<float>(joints.y));
    data.push_back(static_cast<float>(joints.z));
    data.push_back(static_cast<float>(joints.w));
    const glm::vec4 weights = has_joint_weights ? mesh.joint_weights[i] : glm::vec4(0.0f);
    data.push_back(weights.x);
    data.push_back(weights.y);
    data.push_back(weights.z);
    data.push_back(weights.w);
  }
  return data;
}

DiligentBackend::DiligentBackend(karma::platform::Window& window)
    : window_(&window) {
  const auto constructor_start = core::SteadyClock::now();
  auto stage_start = constructor_start;
  if (const char* env = std::getenv("KARMA_ENV_DEBUG")) {
    env_debug_mode_ = std::atoi(env);
  }
  if (const char* env = std::getenv("KARMA_SHADER_CACHE")) {
    shader_cache_enabled_ = envFlagEnabled(env);
  }
  if (const char* env = std::getenv("KARMA_SHADER_CACHE_LOG")) {
    shader_cache_log_ = envFlagEnabled(env);
  }
  if (const char* env = std::getenv("KARMA_SHADER_CACHE_FLUSH")) {
    shader_cache_flush_ = envFlagEnabled(env);
  }
  if (const char* env = std::getenv("KARMA_SHADER_CACHE_VERSION")) {
    const int version = std::atoi(env);
    if (version > 0) {
      shader_cache_version_ = static_cast<std::uint32_t>(version);
    }
  }
  if (const char* env = std::getenv("KARMA_SHADER_CACHE_PATH")) {
    if (env[0] != '\0') {
      render_state_cache_path_ = env;
    }
  }
  if (render_state_cache_path_.empty()) {
    render_state_cache_path_ = defaultShaderCachePath(shader_cache_version_);
  }
  if (shader_cache_log_) {
    spdlog::info("Render state cache config: enabled={} path='{}' version={} flush={}",
                 shader_cache_enabled_,
                 render_state_cache_path_.string(),
                 shader_cache_version_,
                 shader_cache_flush_);
  }
  auto stage_end = core::SteadyClock::now();
  logStartupDiag("diligent_backend", "constructor env parse", stage_start, stage_end);

  stage_start = stage_end;
  for (auto& m : cached_cascade_light_view_proj_) {
    m = glm::mat4(1.0f);
  }
  for (auto& m : cached_cascade_shadow_uv_proj_) {
    m = glm::mat4(1.0f);
  }
  for (auto& m : cached_point_shadow_uv_proj_) {
    m = glm::mat4(1.0f);
  }
  point_shadow_slot_source_index_.fill(-1);
  point_shadow_slot_valid_.fill(false);
  point_shadow_face_dirty_.fill(1u);
  stage_end = core::SteadyClock::now();
  logStartupDiag("diligent_backend", "constructor state init", stage_start, stage_end);

  stage_start = stage_end;
  int fb_width = 800;
  int fb_height = 600;
  window_->getFramebufferSize(fb_width, fb_height);
  if (fb_height == 0) {
    fb_height = 1;
  }
  current_width_ = fb_width;
  current_height_ = fb_height;
  stage_end = core::SteadyClock::now();
  logStartupDiag("diligent_backend", "framebuffer query", stage_start, stage_end);

  stage_start = stage_end;
  initializeDevice();
  stage_end = core::SteadyClock::now();
  logStartupDiag("diligent_backend", "initialize device", stage_start, stage_end);
  logStartupDiag("diligent_backend", "constructor total", constructor_start, stage_end);
}

DiligentBackend::RenderStateCacheFileInfo DiligentBackend::renderStateCacheFileInfo() const {
  const FileInfo info = inspectFile(render_state_cache_path_);
  return RenderStateCacheFileInfo{info.exists, info.size};
}

void DiligentBackend::saveRenderStateCache(std::string_view reason) {
  if (shader_cache_enabled_ && !render_state_cache_path_.empty() && device_with_cache_.GetCache()) {
    const std::string stage_name =
        reason.empty() ? std::string("shader cache save") : std::string(reason);
    const auto before = renderStateCacheFileInfo();
    const auto start = core::SteadyClock::now();
    device_with_cache_.SaveCache(render_state_cache_path_.string().c_str());
    const auto end = core::SteadyClock::now();
    const auto after = renderStateCacheFileInfo();
    logStartupDiag("diligent_backend", stage_name.c_str(), start, end);
    if (shader_cache_log_) {
      spdlog::info(
          "Render state cache save: reason='{}' path='{}' existed_before={} bytes_before={} "
          "existed_after={} bytes_after={} ms={:.2f}",
          stage_name,
          render_state_cache_path_.string(),
          before.exists,
          before.size,
          after.exists,
          after.size,
          core::elapsedMilliseconds(start, end));
    }
  }
}

void DiligentBackend::flushRenderStateCache() {
  if (!shader_cache_flush_) {
    return;
  }
  saveRenderStateCache("renderer warm-up shader cache flush");
}

DiligentBackend::~DiligentBackend() {
  saveRenderStateCache("destructor shader cache save");
}

}  // namespace karma::renderer_backend
