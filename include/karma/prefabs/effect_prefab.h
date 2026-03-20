#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "karma/components/effect_prefab.h"
#include "karma/components/beam_path.h"
#include "karma/components/light.h"
#include "karma/components/mesh.h"
#include "karma/components/particle_effect_override.h"
#include "karma/components/transform.h"
#include "karma/ecs/world.h"
#include "karma/renderer/device.h"
#include "karma/renderer/types.h"

namespace karma::prefabs {

struct EffectPrefabColorParameter {
  std::string name;
  math::Color default_value{1.0f, 1.0f, 1.0f, 1.0f};
};

struct EffectPrefabColorBinding {
  bool enabled = false;
  std::optional<math::Color> value;
  std::string param;
  math::Color scale{1.0f, 1.0f, 1.0f, 1.0f};
  std::optional<math::Color> mix_color;
  float mix_factor = 0.0f;
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
};

struct EffectPrefabBeamDesc {
  components::BeamPathComponent beam{};
  EffectPrefabColorBinding core_color_binding{};
  EffectPrefabColorBinding glow_color_binding{};
};

struct EffectPrefabEntry {
  enum class Type : uint8_t {
    Mesh = 0,
    Particle = 1,
    Light = 2,
    Beam = 3,
  };

  Type type = Type::Mesh;
  std::string name;
  components::TransformComponent local_transform{};
  EffectPrefabMeshDesc mesh{};
  EffectPrefabParticleDesc particle{};
  EffectPrefabLightDesc light{};
  EffectPrefabBeamDesc beam{};
};

struct EffectPrefab {
  std::string name;
  std::filesystem::path source_path;
  std::vector<EffectPrefabColorParameter> color_parameters;
  std::vector<EffectPrefabEntry> entries;
};

struct EffectPrefabColorOverride {
  std::string name;
  math::Color value{1.0f, 1.0f, 1.0f, 1.0f};
};

struct EffectPrefabInstantiateDesc {
  std::string name;
  components::TransformComponent transform{};
  std::vector<EffectPrefabColorOverride> color_overrides;
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

}  // namespace karma::prefabs
