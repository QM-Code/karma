#pragma once

#include <string>

namespace karma::renderer {

struct Color {
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float a = 1.0f;
};

struct MaterialResourceDesc {
  std::string material_key;
  std::string shader_key;
  std::string albedo_texture_key;
  std::string normal_texture_key;
  std::string metallic_roughness_texture_key;
  Color base_color_tint{};
  float metallic = 0.0f;
  float roughness = 0.5f;
  bool double_sided = false;
};

}  // namespace karma::renderer
