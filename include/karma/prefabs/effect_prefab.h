#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "karma/components/effect_prefab.h"
#include "karma/components/beam_path.h"
#include "karma/components/light.h"
#include "karma/components/mesh.h"
#include "karma/components/particle_effect_override.h"
#include "karma/components/transform.h"
#include "karma/components/volume_sphere.h"
#include "karma/ecs/world.h"
#include "karma/renderer/device.h"
#include "karma/renderer/types.h"

namespace karma::prefabs {

using EffectPrefabParamValue =
    std::variant<bool, float, math::Vec3, math::Color, std::string>;

struct EffectPrefabParameter {
  enum class Type : uint8_t {
    Bool = 0,
    Float = 1,
    Vec3 = 2,
    Color = 3,
    String = 4,
  };

  std::string name;
  Type type = Type::Color;
  EffectPrefabParamValue default_value{math::Color{1.0f, 1.0f, 1.0f, 1.0f}};
};

struct EffectPrefabColorBinding {
  bool enabled = false;
  std::optional<math::Color> value;
  std::string param;
  math::Color scale{1.0f, 1.0f, 1.0f, 1.0f};
  std::optional<math::Color> mix_color;
  float mix_factor = 0.0f;
};

struct EffectPrefabFloatBinding {
  bool enabled = false;
  std::optional<float> value;
  std::string param;
  float scale = 1.0f;
  float bias = 0.0f;
};

struct EffectPrefabMaterialDesc {
  renderer::MaterialDesc material{};
  EffectPrefabColorBinding base_color_binding{};
  EffectPrefabColorBinding emissive_color_binding{};
};

struct EffectPrefabMeshDesc {
  std::filesystem::path mesh_path;
  bool visible = true;
  bool shadow_visible = true;
  EffectPrefabMaterialDesc material{};
};

struct EffectPrefabParticleDesc {
  std::string effect_key;
  bool enabled = true;
  bool playing = true;
  bool auto_apply = true;
  bool preserve_enabled = true;
  bool preserve_playing = true;
  components::ParticleEffectOverrideComponent effect_override{};
  EffectPrefabColorBinding start_color_binding{};
  EffectPrefabColorBinding end_color_binding{};
};

struct EffectPrefabLightDesc {
  components::LightComponent light{};
  EffectPrefabColorBinding color_binding{};
  EffectPrefabFloatBinding intensity_binding{};
  EffectPrefabFloatBinding range_binding{};
};

struct EffectPrefabBeamDesc {
  components::BeamPathComponent beam{};
  EffectPrefabColorBinding core_color_binding{};
  EffectPrefabColorBinding glow_color_binding{};
};

struct EffectPrefabVolumeSphereDesc {
  components::VolumeSphereComponent volume{};
  EffectPrefabColorBinding color_binding{};
  EffectPrefabColorBinding emissive_color_binding{};
  EffectPrefabFloatBinding radius_binding{};
  EffectPrefabFloatBinding center_opacity_binding{};
  EffectPrefabFloatBinding distortion_strength_binding{};
  EffectPrefabFloatBinding noise_strength_binding{};
  EffectPrefabFloatBinding overlay_depth_binding{};
};

struct EffectPrefabEntry {
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
  EffectPrefabMeshDesc mesh{};
  EffectPrefabParticleDesc particle{};
  EffectPrefabLightDesc light{};
  EffectPrefabBeamDesc beam{};
  EffectPrefabVolumeSphereDesc volume_sphere{};
};

struct EffectPrefab {
  std::string name;
  std::filesystem::path source_path;
  std::vector<EffectPrefabParameter> parameters;
  std::vector<EffectPrefabEntry> entries;
};

struct EffectPrefabColorOverride {
  std::string name;
  math::Color value{1.0f, 1.0f, 1.0f, 1.0f};
};

struct EffectPrefabParamOverride {
  std::string name;
  EffectPrefabParamValue value{math::Color{1.0f, 1.0f, 1.0f, 1.0f}};
};

struct EffectPrefabInstantiateDesc {
  std::string name;
  components::TransformComponent transform{};
  std::vector<EffectPrefabColorOverride> color_overrides;
  std::vector<EffectPrefabParamOverride> param_overrides;
};

struct EffectPrefabInstance {
  ecs::Entity root{};
  std::vector<ecs::Entity> members;
  std::unordered_map<std::string, ecs::Entity> named_members;

  bool valid() const { return root.isValid(); }

  ecs::Entity find(std::string_view name) const {
    const auto it = named_members.find(std::string(name));
    if (it == named_members.end()) {
      return {};
    }
    return it->second;
  }
};

bool loadEffectPrefab(const std::filesystem::path& path, EffectPrefab& out_prefab);
std::optional<EffectPrefab> loadEffectPrefab(const std::filesystem::path& path);

std::optional<EffectPrefabInstance> instantiateEffectPrefab(
    ecs::World& world,
    renderer::GraphicsDevice* graphics,
    const EffectPrefab& prefab,
    const EffectPrefabInstantiateDesc& desc = {});

std::optional<EffectPrefabInstance> instantiateEffectPrefab(
    ecs::World& world,
    renderer::GraphicsDevice* graphics,
    const std::filesystem::path& path,
    const EffectPrefabInstantiateDesc& desc = {});

bool setPrefabPlayback(ecs::World& world, ecs::Entity root, bool enabled);
bool restartPrefab(ecs::World& world, ecs::Entity root);

using Prefab = EffectPrefab;
using PrefabParameter = EffectPrefabParameter;
using PrefabParamValue = EffectPrefabParamValue;
using PrefabParamOverride = EffectPrefabParamOverride;
using PrefabInstantiateDesc = EffectPrefabInstantiateDesc;
using PrefabInstance = EffectPrefabInstance;
using PrefabColorBinding = EffectPrefabColorBinding;
using PrefabFloatBinding = EffectPrefabFloatBinding;

inline bool loadPrefab(const std::filesystem::path& path, Prefab& out_prefab) {
  return loadEffectPrefab(path, out_prefab);
}

inline std::optional<Prefab> loadPrefab(const std::filesystem::path& path) {
  return loadEffectPrefab(path);
}

inline std::optional<PrefabInstance> instantiatePrefab(
    ecs::World& world,
    renderer::GraphicsDevice* graphics,
    const Prefab& prefab,
    const PrefabInstantiateDesc& desc = {}) {
  return instantiateEffectPrefab(world, graphics, prefab, desc);
}

inline std::optional<PrefabInstance> instantiatePrefab(
    ecs::World& world,
    renderer::GraphicsDevice* graphics,
    const std::filesystem::path& path,
    const PrefabInstantiateDesc& desc = {}) {
  return instantiateEffectPrefab(world, graphics, path, desc);
}

}  // namespace karma::prefabs
