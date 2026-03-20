#pragma once

#include <cstdint>
#include <limits>
#include <filesystem>
#include <array>
#include <string_view>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

#include "karma/math/types.h"

namespace karma::renderer {

using InstanceId = uint64_t;
using MeshId = uint32_t;
using MaterialId = uint32_t;
using MaterialSetId = uint32_t;
using TextureId = uint32_t;
using RenderTargetId = uint32_t;
using LayerId = uint32_t;

constexpr RenderTargetId kDefaultRenderTarget = 0;
constexpr MaterialId kInvalidMaterial = 0;
constexpr MaterialSetId kInvalidMaterialSet = 0;
constexpr MeshId kInvalidMesh = 0;
constexpr TextureId kInvalidTexture = 0;
constexpr InstanceId kInvalidInstance = std::numeric_limits<InstanceId>::max();

struct MeshData {
  std::vector<glm::vec3> vertices;
  std::vector<glm::vec3> normals;
  std::vector<glm::vec2> uvs;
  std::vector<glm::vec4> tangents;
  std::vector<uint32_t> indices;
};

struct MaterialDesc {
  std::filesystem::path vertex_shader_path;
  std::filesystem::path fragment_shader_path;
  math::Color base_color{1.0f, 1.0f, 1.0f, 1.0f};
  math::Color emissive_color{0.0f, 0.0f, 0.0f, 1.0f};
  float metallic = 1.0f;
  float roughness = 1.0f;
  float normal_scale = 1.0f;
  float occlusion_strength = 1.0f;
  enum class ShadingModel : uint32_t {
    Standard = 0,
    EnergyShell = 1,
  };
  ShadingModel shading_model = ShadingModel::Standard;
  float shell_fresnel_power = 5.0f;
  float shell_fresnel_strength = 1.0f;
  float shell_refraction_strength = 0.08f;
  float shell_interior_strength = 0.4f;
  float shell_highlight_strength = 1.0f;
  float shell_alpha_boost = 0.0f;
  float shell_swirl_strength = 0.0f;
  TextureId base_color_texture = kInvalidTexture;
  bool unlit = false;
  bool transparent = false;
  bool depth_test = true;
  bool depth_write = true;
  bool wireframe = false;
  bool double_sided = false;
};

static constexpr uint32_t kCameraShaderUserParamCapacity = 32;

inline uint32_t cameraShaderParamKeyHash(std::string_view key) {
  uint32_t hash = 2166136261u;
  for (char c : key) {
    hash ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
    hash *= 16777619u;
  }
  return hash;
}

struct CameraShaderUserParam {
  uint32_t key_hash = 0u;
  math::Color value{};
};

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

struct DirectionalLightData {
  glm::vec3 direction{0.0f, -1.0f, 0.0f};
  math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  float shadow_extent = 0.0f;
};

enum class LightType : uint32_t {
  Directional = 0,
  Point = 1,
  Spot = 2
};

struct LightData {
  LightType type = LightType::Point;
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::vec3 direction{0.0f, -1.0f, 0.0f};
  math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float range = 10.0f;
  float inner_cone_cos = 0.9659258f;
  float outer_cone_cos = 0.8660254f;
  bool casts_shadows = false;
};

struct ForwardPlusStats {
  uint32_t tile_size = 16;
  uint32_t max_lights_per_tile = 128;
  uint32_t tiles_x = 0;
  uint32_t tiles_y = 0;
  uint32_t local_light_count = 0;
  bool active = false;
  bool overflow_risk = false;
};

struct DrawItem {
  InstanceId instance = kInvalidInstance;
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  MaterialSetId material_set = kInvalidMaterialSet;
  glm::mat4 transform{1.0f};
  LayerId layer = 0;
  bool visible = true;
  bool shadow_visible = true;
};

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
};

struct ParticleBatch {
  LayerId layer = 0;
  bool depth_test = true;
  TextureId texture = kInvalidTexture;
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
  std::vector<ParticleInstance> particles;
};

struct FrameInfo {
  int width = 0;
  int height = 0;
  float delta_time = 0.0f;
};

struct RenderTargetDesc {
  int width = 0;
  int height = 0;
  bool depth = true;
  bool stencil = false;
};

enum class TextureFormat {
  RGBA8,
  RGB8,
  R8
};

struct TextureDesc {
  int width = 0;
  int height = 0;
  TextureFormat format = TextureFormat::RGBA8;
  bool srgb = false;
  bool generate_mips = false;
};

}  // namespace karma::renderer
