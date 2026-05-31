#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "karma/core/math/types.h"
#include "karma/rendering/renderer/ids.h"

#include <glm/glm.hpp>

namespace karma::renderer {

using Color = math::Color;

struct MaterialDesc {
  enum class BlendMode : uint32_t {
    Alpha = 0,
    Additive = 1,
  };

  enum class ShadingModel : uint32_t {
    Standard = 0,
    EnergyShell = 1,
    WaveVolume = 2,
    SphereHalo = 3,
    ScreenWave = 4,
    SphereGlowVolume = 5,
    VolumetricSphere = 6,
  };

  std::filesystem::path vertex_shader_path;
  std::filesystem::path fragment_shader_path;
  math::Color base_color{1.0f, 1.0f, 1.0f, 1.0f};
  math::Color emissive_color{0.0f, 0.0f, 0.0f, 1.0f};
  float metallic = 1.0f;
  float roughness = 1.0f;
  float normal_scale = 1.0f;
  float occlusion_strength = 1.0f;
  ShadingModel shading_model = ShadingModel::Standard;
  float shell_fresnel_power = 5.0f;
  float shell_fresnel_strength = 1.0f;
  float shell_refraction_strength = 0.08f;
  float shell_interior_strength = 0.4f;
  float shell_highlight_strength = 1.0f;
  float shell_alpha_boost = 0.0f;
  float shell_swirl_strength = 0.0f;
  bool analytic_sphere_normals = false;
  float shell_body_strength = 1.0f;
  float screen_center_x = 0.5f;
  float screen_center_y = 0.5f;
  float screen_radius_x = 0.25f;
  float screen_radius_y = 0.25f;
  float wave_tint_strength = 0.75f;
  float wave_distortion_strength = 0.6f;
  float wave_edge_strength = 0.35f;
  float wave_noise_strength = 0.65f;
  glm::vec3 volume_center{0.0f, 0.0f, 0.0f};
  float volume_radius = 1.0f;
  float volume_density = 1.0f;
  TextureId base_color_texture = kInvalidTexture;
  bool unlit = false;
  bool transparent = false;
  BlendMode blend_mode = BlendMode::Alpha;
  bool depth_test = true;
  bool depth_write = true;
  bool wireframe = false;
  bool double_sided = false;
};

struct MaterialResourceDesc {
  enum class Kind {
    MeshTint,
  };

  Kind kind = Kind::MeshTint;
  std::string material_key;
  std::string source_mesh_key;
  std::string shader_key;
  std::string albedo_texture_key;
  std::string normal_texture_key;
  std::string metallic_roughness_texture_key;
  Color base_color_tint{1.0f, 1.0f, 1.0f, 1.0f};
  float metallic = 0.0f;
  float roughness = 0.5f;
  bool double_sided = false;

  static MaterialResourceDesc fromMeshTint(std::string source_mesh, Color tint) {
    MaterialResourceDesc desc{};
    desc.kind = Kind::MeshTint;
    desc.source_mesh_key = std::move(source_mesh);
    desc.base_color_tint = tint;
    return desc;
  }
};

}  // namespace karma::renderer
