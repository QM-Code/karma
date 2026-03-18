#pragma once

#include <string>

#include "karma/math/types.h"

namespace karma::renderer {

using Color = math::Color;

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
