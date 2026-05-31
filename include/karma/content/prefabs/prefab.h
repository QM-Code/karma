#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "karma/world/components/beam_path.h"
#include "karma/world/components/light.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/particle_effect_override.h"
#include "karma/world/components/prefab_instance.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/volume_sphere.h"
#include "karma/rendering/renderer/material.h"

namespace karma::prefabs {

using PrefabParamValue = std::variant<bool, float, math::Vec3, math::Color, std::string>;

struct PrefabParameter {
  enum class Type : uint8_t {
    Bool = 0,
    Float = 1,
    Vec3 = 2,
    Color = 3,
    String = 4,
  };

  std::string name;
  Type type = Type::Color;
  PrefabParamValue default_value{math::Color{1.0f, 1.0f, 1.0f, 1.0f}};
};

struct PrefabColorBinding {
  bool enabled = false;
  std::optional<math::Color> value;
  std::string param;
  math::Color scale{1.0f, 1.0f, 1.0f, 1.0f};
  std::optional<math::Color> mix_color;
  float mix_factor = 0.0f;
};

struct PrefabFloatBinding {
  bool enabled = false;
  std::optional<float> value;
  std::string param;
  float scale = 1.0f;
  float bias = 0.0f;
};

struct PrefabMaterialDesc {
  renderer::MaterialDesc material{};
  PrefabColorBinding base_color_binding{};
  PrefabColorBinding emissive_color_binding{};
};

struct PrefabMeshDesc {
  std::filesystem::path mesh_path;
  bool visible = true;
  bool shadow_visible = true;
  PrefabMaterialDesc material{};
};

struct PrefabParticleDesc {
  std::string effect_key;
  bool enabled = true;
  bool playing = true;
  bool auto_apply = true;
  bool preserve_enabled = true;
  bool preserve_playing = true;
  components::ParticleEffectOverrideComponent effect_override{};
  PrefabColorBinding start_color_binding{};
  PrefabColorBinding end_color_binding{};
};

struct PrefabLightDesc {
  components::LightComponent light{};
  PrefabColorBinding color_binding{};
  PrefabFloatBinding intensity_binding{};
  PrefabFloatBinding range_binding{};
};

struct PrefabBeamDesc {
  components::BeamPathComponent beam{};
  PrefabColorBinding core_color_binding{};
  PrefabColorBinding glow_color_binding{};
};

struct PrefabVolumeSphereDesc {
  components::VolumeSphereComponent volume{};
  PrefabColorBinding color_binding{};
  PrefabColorBinding emissive_color_binding{};
  PrefabFloatBinding radius_binding{};
  PrefabFloatBinding center_opacity_binding{};
  PrefabFloatBinding distortion_strength_binding{};
  PrefabFloatBinding noise_strength_binding{};
  PrefabFloatBinding overlay_depth_binding{};
};

struct PrefabEntry {
  enum class Type : uint8_t {
    Mesh = 0,
    Particle = 1,
    Light = 2,
    Beam = 3,
    VolumeSphere = 4,
  };

  Type type = Type::Mesh;
  std::string name;
  components::TransformComponent local_transform{};
  PrefabMeshDesc mesh{};
  PrefabParticleDesc particle{};
  PrefabLightDesc light{};
  PrefabBeamDesc beam{};
  PrefabVolumeSphereDesc volume_sphere{};
};

struct Prefab {
  std::string name;
  std::filesystem::path source_path;
  std::vector<PrefabParameter> parameters;
  std::vector<PrefabEntry> entries;
};

bool loadPrefab(const std::filesystem::path& path, Prefab& out_prefab);
std::optional<Prefab> loadPrefab(const std::filesystem::path& path);

}  // namespace karma::prefabs
