#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "karma/prefabs/prefab.h"

namespace karma::prefabs::detail {

enum class SectionKind : uint8_t {
  Invalid = 0,
  Prefab = 1,
  Param = 2,
  Mesh = 3,
  Particle = 4,
  Light = 5,
  Beam = 6,
  VolumeSphere = 7,
};

struct SectionHeader {
  SectionKind kind = SectionKind::Invalid;
  std::string name;
};

std::string trim(std::string_view text);
std::string lowercase(std::string_view text);
std::string stripQuotes(std::string value);

template <typename T>
bool parseNumber(std::string_view text, T& out_value) {
  const std::string value = trim(text);
  if (value.empty()) {
    return false;
  }

  try {
    if constexpr (std::is_same_v<T, float>) {
      out_value = std::stof(value);
    } else if constexpr (std::is_same_v<T, int>) {
      out_value = std::stoi(value);
    } else {
      static_assert(!sizeof(T*), "Unsupported numeric type");
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool parseBool(std::string_view text, bool& out_value);
bool parseVec3(std::string_view text, math::Vec3& out_value);
bool parseColor(std::string_view text, math::Color& out_value);
bool parseVec3List(std::string_view text, std::vector<math::Vec3>& out_points);
bool parseShadingModel(std::string_view text,
                       renderer::MaterialDesc::ShadingModel& out_value);
bool parseBlendMode(std::string_view text, renderer::MaterialDesc::BlendMode& out_value);
bool parseLightType(std::string_view text, components::LightComponent::Type& out_value);
bool parseParamType(std::string_view text, PrefabParameter::Type& out_value);
bool parseParamValue(PrefabParameter::Type type,
                     std::string_view text,
                     PrefabParamValue& out_value);

void markBindingEnabled(PrefabColorBinding& binding);
void markBindingEnabled(PrefabFloatBinding& binding);

bool applyTransformField(components::TransformComponent& transform,
                         const std::string& key,
                         const std::string& raw_value);
bool applyColorBindingField(PrefabColorBinding& binding,
                            const std::string& key,
                            const std::string& raw_value,
                            const std::string& prefix);
bool applyFloatBindingField(PrefabFloatBinding& binding,
                            const std::string& key,
                            const std::string& raw_value,
                            const std::string& prefix);

bool parseSectionHeader(std::string_view raw_header, SectionHeader& out_header);
const char* sectionKindLabel(SectionKind kind);
std::optional<PrefabEntry::Type> entryTypeForSection(SectionKind kind);

std::optional<std::filesystem::path> resolvePrefabSourcePath(
    const std::filesystem::path& path);

bool applyPrefabField(Prefab& prefab,
                      const std::string& key,
                      const std::string& raw_value,
                      std::string& out_error);

bool applyParameterField(PrefabParameter& param,
                         const std::string& key,
                         const std::string& raw_value,
                         std::string& out_error);

bool applyMeshField(Prefab& prefab,
                    PrefabEntry& entry,
                    const std::string& key,
                    const std::string& raw_value,
                    std::string& out_error);
bool applyParticleField(Prefab& prefab,
                        PrefabEntry& entry,
                        const std::string& key,
                        const std::string& raw_value,
                        std::string& out_error);
bool applyLightField(Prefab& prefab,
                     PrefabEntry& entry,
                     const std::string& key,
                     const std::string& raw_value,
                     std::string& out_error);
bool applyBeamField(Prefab& prefab,
                    PrefabEntry& entry,
                    const std::string& key,
                    const std::string& raw_value,
                    std::string& out_error);
bool applyVolumeSphereField(Prefab& prefab,
                            PrefabEntry& entry,
                            const std::string& key,
                            const std::string& raw_value,
                            std::string& out_error);

bool applyEntryField(Prefab& prefab,
                     PrefabEntry& entry,
                     const std::string& key,
                     const std::string& raw_value,
                     std::string& out_error);

}  // namespace karma::prefabs::detail
