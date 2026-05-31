#include "prefab_parse_support.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>

#include <glm/ext/quaternion_float.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/core/math/quat.h"

namespace karma::prefabs::detail {

namespace {

std::vector<std::string> splitSeparated(std::string_view text, char delimiter) {
  std::vector<std::string> values;
  std::stringstream stream(trim(text));
  std::string part;
  while (std::getline(stream, part, delimiter)) {
    values.push_back(trim(part));
  }
  return values;
}

std::vector<std::string> splitCommaSeparated(std::string_view text) {
  return splitSeparated(text, ',');
}

math::Quat eulerDegreesToQuat(const math::Vec3& euler_degrees) {
  const glm::vec3 euler_radians = glm::radians(
      glm::vec3(euler_degrees.x, euler_degrees.y, euler_degrees.z));
  const glm::quat q = glm::quat(euler_radians);
  return {q.x, q.y, q.z, q.w};
}

using EntryFieldApplyFn = bool (*)(
    Prefab& prefab,
    PrefabEntry& entry,
    const std::string& key,
    const std::string& raw_value,
    std::string& out_error);

constexpr std::array<EntryFieldApplyFn, 5u> kEntryFieldParsers = {
    &applyMeshField,
    &applyParticleField,
    &applyLightField,
    &applyBeamField,
    &applyVolumeSphereField,
};

}  // namespace

std::string trim(std::string_view text) {
  size_t start = 0;
  while (start < text.size() &&
         std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }

  size_t end = text.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }

  return std::string(text.substr(start, end - start));
}

std::string lowercase(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::string stripQuotes(std::string value) {
  if (value.size() >= 2u &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1u, value.size() - 2u);
  }
  return value;
}

bool parseBool(std::string_view text, bool& out_value) {
  const std::string value = lowercase(trim(text));
  if (value == "true" || value == "1" || value == "yes" || value == "on") {
    out_value = true;
    return true;
  }
  if (value == "false" || value == "0" || value == "no" || value == "off") {
    out_value = false;
    return true;
  }
  return false;
}

bool parseVec3(std::string_view text, math::Vec3& out_value) {
  const std::vector<std::string> parts = splitCommaSeparated(text);
  if (parts.size() != 3u) {
    return false;
  }
  return parseNumber(parts[0], out_value.x) &&
         parseNumber(parts[1], out_value.y) &&
         parseNumber(parts[2], out_value.z);
}

bool parseColor(std::string_view text, math::Color& out_value) {
  const std::vector<std::string> parts = splitCommaSeparated(text);
  if (parts.size() != 4u) {
    return false;
  }
  return parseNumber(parts[0], out_value.r) &&
         parseNumber(parts[1], out_value.g) &&
         parseNumber(parts[2], out_value.b) &&
         parseNumber(parts[3], out_value.a);
}

bool parseVec3List(std::string_view text, std::vector<math::Vec3>& out_points) {
  std::vector<math::Vec3> points;
  for (const std::string& raw_point : splitSeparated(text, ';')) {
    if (raw_point.empty()) {
      continue;
    }
    math::Vec3 point{};
    if (!parseVec3(raw_point, point)) {
      return false;
    }
    points.push_back(point);
  }
  out_points = std::move(points);
  return true;
}

bool parseShadingModel(std::string_view text,
                       renderer::MaterialDesc::ShadingModel& out_value) {
  const std::string value = lowercase(trim(text));
  if (value == "standard" || value == "default") {
    out_value = renderer::MaterialDesc::ShadingModel::Standard;
    return true;
  }
  if (value == "energy_shell" || value == "energyshell" || value == "shell") {
    out_value = renderer::MaterialDesc::ShadingModel::EnergyShell;
    return true;
  }
  if (value == "wave_volume" || value == "wavevolume" || value == "wave") {
    out_value = renderer::MaterialDesc::ShadingModel::WaveVolume;
    return true;
  }
  if (value == "sphere_halo" || value == "spherehalo" || value == "halo") {
    out_value = renderer::MaterialDesc::ShadingModel::SphereHalo;
    return true;
  }
  if (value == "screen_wave" || value == "screenwave") {
    out_value = renderer::MaterialDesc::ShadingModel::ScreenWave;
    return true;
  }
  if (value == "sphere_glow_volume" || value == "sphereglowvolume") {
    out_value = renderer::MaterialDesc::ShadingModel::SphereGlowVolume;
    return true;
  }
  if (value == "volumetric_sphere" || value == "volumetricsphere") {
    out_value = renderer::MaterialDesc::ShadingModel::VolumetricSphere;
    return true;
  }
  return false;
}

bool parseBlendMode(std::string_view text, renderer::MaterialDesc::BlendMode& out_value) {
  const std::string value = lowercase(trim(text));
  if (value == "alpha") {
    out_value = renderer::MaterialDesc::BlendMode::Alpha;
    return true;
  }
  if (value == "additive" || value == "add") {
    out_value = renderer::MaterialDesc::BlendMode::Additive;
    return true;
  }
  return false;
}

bool parseLightType(std::string_view text, components::LightComponent::Type& out_value) {
  const std::string value = lowercase(trim(text));
  if (value == "directional" || value == "sun") {
    out_value = components::LightComponent::Type::Directional;
    return true;
  }
  if (value == "point") {
    out_value = components::LightComponent::Type::Point;
    return true;
  }
  if (value == "spot" || value == "spotlight") {
    out_value = components::LightComponent::Type::Spot;
    return true;
  }
  return false;
}

bool parseParamType(std::string_view text, PrefabParameter::Type& out_value) {
  const std::string value = lowercase(trim(text));
  if (value == "bool" || value == "boolean") {
    out_value = PrefabParameter::Type::Bool;
    return true;
  }
  if (value == "float" || value == "number") {
    out_value = PrefabParameter::Type::Float;
    return true;
  }
  if (value == "vec3" || value == "float3") {
    out_value = PrefabParameter::Type::Vec3;
    return true;
  }
  if (value == "color" || value == "rgba") {
    out_value = PrefabParameter::Type::Color;
    return true;
  }
  if (value == "string" || value == "path") {
    out_value = PrefabParameter::Type::String;
    return true;
  }
  return false;
}

bool parseParamValue(PrefabParameter::Type type,
                     std::string_view text,
                     PrefabParamValue& out_value) {
  switch (type) {
    case PrefabParameter::Type::Bool: {
      bool value = false;
      if (!parseBool(text, value)) {
        return false;
      }
      out_value = value;
      return true;
    }
    case PrefabParameter::Type::Float: {
      float value = 0.0f;
      if (!parseNumber(text, value)) {
        return false;
      }
      out_value = value;
      return true;
    }
    case PrefabParameter::Type::Vec3: {
      math::Vec3 value{};
      if (!parseVec3(text, value)) {
        return false;
      }
      out_value = value;
      return true;
    }
    case PrefabParameter::Type::Color: {
      math::Color value{};
      if (!parseColor(text, value)) {
        return false;
      }
      out_value = value;
      return true;
    }
    case PrefabParameter::Type::String:
      out_value = stripQuotes(trim(text));
      return true;
  }
  return false;
}

void markBindingEnabled(PrefabColorBinding& binding) {
  binding.enabled = true;
}

void markBindingEnabled(PrefabFloatBinding& binding) {
  binding.enabled = true;
}

bool applyTransformField(components::TransformComponent& transform,
                         const std::string& key,
                         const std::string& raw_value) {
  if (key == "position") {
    math::Vec3 value{};
    if (!parseVec3(raw_value, value)) {
      return false;
    }
    transform.setPosition(value);
    return true;
  }
  if (key == "rotation_deg" || key == "rotation_euler_deg") {
    math::Vec3 value{};
    if (!parseVec3(raw_value, value)) {
      return false;
    }
    transform.setRotation(eulerDegreesToQuat(value));
    return true;
  }
  if (key == "scale") {
    math::Vec3 value{};
    if (!parseVec3(raw_value, value)) {
      return false;
    }
    transform.setScale(value);
    return true;
  }
  if (key == "uniform_scale") {
    float value = 1.0f;
    if (!parseNumber(raw_value, value)) {
      return false;
    }
    transform.setScale({value, value, value});
    return true;
  }
  return false;
}

bool applyColorBindingField(PrefabColorBinding& binding,
                            const std::string& key,
                            const std::string& raw_value,
                            const std::string& prefix) {
  if (key == prefix) {
    math::Color value{};
    if (!parseColor(raw_value, value)) {
      return false;
    }
    binding.value = value;
    markBindingEnabled(binding);
    return true;
  }
  if (key == prefix + "_param") {
    binding.param = stripQuotes(trim(raw_value));
    markBindingEnabled(binding);
    return !binding.param.empty();
  }
  if (key == prefix + "_scale") {
    math::Color value{};
    if (!parseColor(raw_value, value)) {
      return false;
    }
    binding.scale = value;
    markBindingEnabled(binding);
    return true;
  }
  if (key == prefix + "_mix") {
    math::Color value{};
    if (!parseColor(raw_value, value)) {
      return false;
    }
    binding.mix_color = value;
    markBindingEnabled(binding);
    return true;
  }
  if (key == prefix + "_mix_factor") {
    if (!parseNumber(raw_value, binding.mix_factor)) {
      return false;
    }
    markBindingEnabled(binding);
    return true;
  }
  return false;
}

bool applyFloatBindingField(PrefabFloatBinding& binding,
                            const std::string& key,
                            const std::string& raw_value,
                            const std::string& prefix) {
  if (key == prefix) {
    float value = 0.0f;
    if (!parseNumber(raw_value, value)) {
      return false;
    }
    binding.value = value;
    markBindingEnabled(binding);
    return true;
  }
  if (key == prefix + "_param") {
    binding.param = stripQuotes(trim(raw_value));
    markBindingEnabled(binding);
    return !binding.param.empty();
  }
  if (key == prefix + "_scale") {
    if (!parseNumber(raw_value, binding.scale)) {
      return false;
    }
    markBindingEnabled(binding);
    return true;
  }
  if (key == prefix + "_bias") {
    if (!parseNumber(raw_value, binding.bias)) {
      return false;
    }
    markBindingEnabled(binding);
    return true;
  }
  return false;
}

bool parseSectionHeader(std::string_view raw_header, SectionHeader& out_header) {
  const std::string header = trim(raw_header);
  if (header.empty()) {
    return false;
  }

  const size_t space_pos = header.find_first_of(" \t");
  const std::string raw_kind =
      space_pos == std::string::npos ? header : header.substr(0, space_pos);
  const std::string kind = lowercase(raw_kind);
  const std::string name = space_pos == std::string::npos
                               ? std::string{}
                               : trim(std::string_view(header).substr(space_pos + 1));

  SectionKind section_kind = SectionKind::Invalid;
  if (kind == "prefab") {
    section_kind = SectionKind::Prefab;
  } else if (kind == "param") {
    section_kind = SectionKind::Param;
  } else if (kind == "mesh") {
    section_kind = SectionKind::Mesh;
  } else if (kind == "particle") {
    section_kind = SectionKind::Particle;
  } else if (kind == "light") {
    section_kind = SectionKind::Light;
  } else if (kind == "beam") {
    section_kind = SectionKind::Beam;
  } else if (kind == "volume_sphere" || kind == "volumesphere") {
    section_kind = SectionKind::VolumeSphere;
  }

  if (section_kind == SectionKind::Invalid) {
    return false;
  }

  out_header.kind = section_kind;
  out_header.name = name;
  return true;
}

const char* sectionKindLabel(SectionKind kind) {
  switch (kind) {
    case SectionKind::Invalid:
      return "invalid";
    case SectionKind::Prefab:
      return "prefab";
    case SectionKind::Param:
      return "param";
    case SectionKind::Mesh:
      return "mesh";
    case SectionKind::Particle:
      return "particle";
    case SectionKind::Light:
      return "light";
    case SectionKind::Beam:
      return "beam";
    case SectionKind::VolumeSphere:
      return "volume_sphere";
  }
  return "invalid";
}

std::optional<PrefabEntry::Type> entryTypeForSection(SectionKind kind) {
  switch (kind) {
    case SectionKind::Mesh:
      return PrefabEntry::Type::Mesh;
    case SectionKind::Particle:
      return PrefabEntry::Type::Particle;
    case SectionKind::Light:
      return PrefabEntry::Type::Light;
    case SectionKind::Beam:
      return PrefabEntry::Type::Beam;
    case SectionKind::VolumeSphere:
      return PrefabEntry::Type::VolumeSphere;
    case SectionKind::Invalid:
    case SectionKind::Prefab:
    case SectionKind::Param:
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> resolvePrefabSourcePath(
    const std::filesystem::path& path) {
  std::filesystem::path resolved = path;
  if (std::filesystem::is_directory(resolved)) {
    resolved /= "prefab.kprefab";
  }
  resolved = std::filesystem::absolute(resolved).lexically_normal();
  if (!std::filesystem::exists(resolved)) {
    return std::nullopt;
  }
  return resolved;
}

bool applyPrefabField(Prefab& prefab,
                      const std::string& key,
                      const std::string& raw_value,
                      std::string& out_error) {
  if (key == "name") {
    prefab.name = stripQuotes(trim(raw_value));
    return true;
  }
  out_error = "unknown prefab field '" + key + "'";
  return false;
}

bool applyParameterField(PrefabParameter& param,
                         const std::string& key,
                         const std::string& raw_value,
                         std::string& out_error) {
  if (key == "type") {
    return parseParamType(raw_value, param.type);
  }
  if (key == "default") {
    return parseParamValue(param.type, raw_value, param.default_value);
  }
  out_error = "unknown param field '" + key + "'";
  return false;
}

bool applyEntryField(Prefab& prefab,
                     PrefabEntry& entry,
                     const std::string& key,
                     const std::string& raw_value,
                     std::string& out_error) {
  const size_t index = static_cast<size_t>(entry.type);
  if (index >= kEntryFieldParsers.size()) {
    out_error = "unknown prefab entry type";
    return false;
  }
  return kEntryFieldParsers[index](prefab, entry, key, raw_value, out_error);
}

}  // namespace karma::prefabs::detail
