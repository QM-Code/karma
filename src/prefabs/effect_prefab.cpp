#include "karma/prefabs/effect_prefab.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

#include <glm/ext/quaternion_float.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include "karma/math/quat.h"
#include "karma/particles/effect_api.h"

namespace karma::prefabs {

namespace {

std::string trim(std::string_view text) {
  size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }

  size_t end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
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

std::vector<std::string> splitCommaSeparated(std::string_view text) {
  std::vector<std::string> values;
  std::stringstream stream(trim(text));
  std::string part;
  while (std::getline(stream, part, ',')) {
    values.push_back(trim(part));
  }
  return values;
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

math::Quat eulerDegreesToQuat(const math::Vec3& euler_degrees) {
  const glm::vec3 euler_radians = glm::radians(glm::vec3(euler_degrees.x,
                                                         euler_degrees.y,
                                                         euler_degrees.z));
  const glm::quat q = glm::quat(euler_radians);
  return {q.x, q.y, q.z, q.w};
}

bool parseShadingModel(std::string_view text, renderer::MaterialDesc::ShadingModel& out_value) {
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

math::Vec3 multiplyVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x * b.x, a.y * b.y, a.z * b.z};
}

math::Vec3 addVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

components::TransformComponent composeTransform(const components::TransformComponent& root,
                                                const components::TransformComponent& local) {
  components::TransformComponent world_transform{};
  const math::Vec3 scaled_local = multiplyVec3(local.getPosition(), root.getScale());
  const math::Vec3 rotated_local = math::rotateVec(root.getRotation(), scaled_local);
  world_transform.setPosition(addVec3(root.getPosition(), rotated_local));
  world_transform.setRotation(math::mul(root.getRotation(), local.getRotation()));
  world_transform.setScale(multiplyVec3(root.getScale(), local.getScale()));
  return world_transform;
}

math::Color lerpColor(const math::Color& a, const math::Color& b, float t) {
  const float s = std::clamp(t, 0.0f, 1.0f);
  return {
      a.r + (b.r - a.r) * s,
      a.g + (b.g - a.g) * s,
      a.b + (b.b - a.b) * s,
      a.a + (b.a - a.a) * s,
  };
}

math::Color multiplyColor(const math::Color& a, const math::Color& b) {
  return {a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a};
}

math::Color resolveColorBinding(
    const EffectPrefabColorBinding& binding,
    const std::unordered_map<std::string, math::Color>& resolved_params,
    const math::Color& fallback) {
  math::Color color = binding.value.value_or(fallback);
  if (!binding.param.empty()) {
    const auto it = resolved_params.find(binding.param);
    if (it != resolved_params.end()) {
      color = it->second;
    }
  }
  color = multiplyColor(color, binding.scale);
  if (binding.mix_color.has_value() && binding.mix_factor > 0.0f) {
    color = lerpColor(color, *binding.mix_color, binding.mix_factor);
  }
  return color;
}

void markBindingEnabled(EffectPrefabColorBinding& binding) {
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

bool applyColorBindingField(EffectPrefabColorBinding& binding,
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

bool applyMeshField(EffectPrefab& prefab,
                    EffectPrefabEntry& entry,
                    const std::string& key,
                    const std::string& raw_value,
                    std::string& out_error) {
  if (applyTransformField(entry.local_transform, key, raw_value)) {
    return true;
  }
  if (key == "mesh") {
    std::filesystem::path mesh_path = stripQuotes(trim(raw_value));
    if (!mesh_path.is_absolute()) {
      mesh_path = prefab.source_path.parent_path() / mesh_path;
    }
    entry.mesh.mesh_path = mesh_path.lexically_normal();
    return true;
  }
  if (key == "visible") {
    return parseBool(raw_value, entry.mesh.visible);
  }
  if (key == "shadow_visible") {
    return parseBool(raw_value, entry.mesh.shadow_visible);
  }

  auto& material = entry.mesh.material.material;
  auto& base_binding = entry.mesh.material.base_color_binding;
  auto& emissive_binding = entry.mesh.material.emissive_color_binding;
  if (applyColorBindingField(base_binding, key, raw_value, "material.base_color")) {
    return true;
  }
  if (applyColorBindingField(emissive_binding, key, raw_value, "material.emissive_color")) {
    return true;
  }
  if (key == "material.metallic") {
    return parseNumber(raw_value, material.metallic);
  }
  if (key == "material.roughness") {
    return parseNumber(raw_value, material.roughness);
  }
  if (key == "material.normal_scale") {
    return parseNumber(raw_value, material.normal_scale);
  }
  if (key == "material.occlusion_strength") {
    return parseNumber(raw_value, material.occlusion_strength);
  }
  if (key == "material.shading_model") {
    return parseShadingModel(raw_value, material.shading_model);
  }
  if (key == "material.shell_fresnel_power") {
    return parseNumber(raw_value, material.shell_fresnel_power);
  }
  if (key == "material.shell_fresnel_strength") {
    return parseNumber(raw_value, material.shell_fresnel_strength);
  }
  if (key == "material.shell_refraction_strength") {
    return parseNumber(raw_value, material.shell_refraction_strength);
  }
  if (key == "material.shell_interior_strength") {
    return parseNumber(raw_value, material.shell_interior_strength);
  }
  if (key == "material.shell_highlight_strength") {
    return parseNumber(raw_value, material.shell_highlight_strength);
  }
  if (key == "material.shell_alpha_boost") {
    return parseNumber(raw_value, material.shell_alpha_boost);
  }
  if (key == "material.shell_swirl_strength") {
    return parseNumber(raw_value, material.shell_swirl_strength);
  }
  if (key == "material.wave_tint_strength") {
    return parseNumber(raw_value, material.wave_tint_strength);
  }
  if (key == "material.wave_distortion_strength") {
    return parseNumber(raw_value, material.wave_distortion_strength);
  }
  if (key == "material.wave_edge_strength") {
    return parseNumber(raw_value, material.wave_edge_strength);
  }
  if (key == "material.wave_noise_strength") {
    return parseNumber(raw_value, material.wave_noise_strength);
  }
  if (key == "material.unlit") {
    return parseBool(raw_value, material.unlit);
  }
  if (key == "material.transparent") {
    return parseBool(raw_value, material.transparent);
  }
  if (key == "material.depth_test") {
    return parseBool(raw_value, material.depth_test);
  }
  if (key == "material.depth_write") {
    return parseBool(raw_value, material.depth_write);
  }
  if (key == "material.wireframe") {
    return parseBool(raw_value, material.wireframe);
  }
  if (key == "material.double_sided") {
    return parseBool(raw_value, material.double_sided);
  }

  out_error = "unknown mesh field '" + key + "'";
  return false;
}

bool applyParticleField(EffectPrefabEntry& entry,
                        const std::string& key,
                        const std::string& raw_value,
                        std::string& out_error) {
  if (applyTransformField(entry.local_transform, key, raw_value)) {
    return true;
  }
  if (key == "effect") {
    entry.particle.effect_key = stripQuotes(trim(raw_value));
    return !entry.particle.effect_key.empty();
  }
  if (key == "enabled") {
    return parseBool(raw_value, entry.particle.enabled);
  }
  if (key == "playing") {
    return parseBool(raw_value, entry.particle.playing);
  }
  if (key == "auto_apply") {
    return parseBool(raw_value, entry.particle.auto_apply);
  }
  if (key == "preserve_enabled") {
    return parseBool(raw_value, entry.particle.preserve_enabled);
  }
  if (key == "preserve_playing") {
    return parseBool(raw_value, entry.particle.preserve_playing);
  }

  auto& override = entry.particle.effect_override;
  if (applyColorBindingField(entry.particle.start_color_binding,
                             key,
                             raw_value,
                             "override.start_color")) {
    return true;
  }
  if (applyColorBindingField(entry.particle.end_color_binding,
                             key,
                             raw_value,
                             "override.end_color")) {
    return true;
  }
  if (key == "override.active") {
    return parseBool(raw_value, override.active);
  }
  if (key == "override.time_scale") {
    return parseNumber(raw_value, override.time_scale);
  }
  if (key == "override.spawn_rate_scale") {
    return parseNumber(raw_value, override.spawn_rate_scale);
  }
  if (key == "override.lifetime_scale") {
    return parseNumber(raw_value, override.lifetime_scale);
  }
  if (key == "override.size_scale") {
    return parseNumber(raw_value, override.size_scale);
  }
  if (key == "override.radius_scale") {
    return parseNumber(raw_value, override.radius_scale);
  }
  if (key == "override.velocity_scale") {
    return parseNumber(raw_value, override.velocity_scale);
  }
  if (key == "override.angular_velocity_scale") {
    return parseNumber(raw_value, override.angular_velocity_scale);
  }
  if (key == "override.alpha_scale") {
    return parseNumber(raw_value, override.alpha_scale);
  }

  out_error = "unknown particle field '" + key + "'";
  return false;
}

bool applyLightField(EffectPrefabEntry& entry,
                     const std::string& key,
                     const std::string& raw_value,
                     std::string& out_error) {
  if (applyTransformField(entry.local_transform, key, raw_value)) {
    return true;
  }
  if (applyColorBindingField(entry.light.color_binding, key, raw_value, "color")) {
    return true;
  }
  if (key == "type") {
    return parseLightType(raw_value, entry.light.light.type);
  }
  if (key == "intensity") {
    return parseNumber(raw_value, entry.light.light.intensity);
  }
  if (key == "range") {
    return parseNumber(raw_value, entry.light.light.range);
  }
  if (key == "casts_shadows") {
    return parseBool(raw_value, entry.light.light.casts_shadows);
  }
  if (key == "inner_cone_degrees") {
    return parseNumber(raw_value, entry.light.light.inner_cone_degrees);
  }
  if (key == "outer_cone_degrees") {
    return parseNumber(raw_value, entry.light.light.outer_cone_degrees);
  }
  if (key == "shadow_extent") {
    return parseNumber(raw_value, entry.light.light.shadow_extent);
  }

  out_error = "unknown light field '" + key + "'";
  return false;
}

bool applyPrefabField(EffectPrefab& prefab,
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

bool applyColorParameterField(EffectPrefabColorParameter& param,
                              const std::string& key,
                              const std::string& raw_value,
                              std::string& out_error) {
  if (key == "default") {
    return parseColor(raw_value, param.default_value);
  }
  out_error = "unknown color parameter field '" + key + "'";
  return false;
}

bool parseSectionHeader(std::string_view raw_header,
                        std::string& out_kind,
                        std::string& out_name) {
  const std::string header = trim(raw_header);
  if (header.empty()) {
    return false;
  }
  const size_t space_pos = header.find_first_of(" \t");
  if (space_pos == std::string::npos) {
    out_kind = lowercase(header);
    out_name.clear();
    return true;
  }
  out_kind = lowercase(header.substr(0, space_pos));
  out_name = trim(std::string_view(header).substr(space_pos + 1));
  return true;
}

std::unordered_map<std::string, math::Color> resolveColorParameters(
    const EffectPrefab& prefab,
    const EffectPrefabInstantiateDesc& desc) {
  std::unordered_map<std::string, math::Color> resolved;
  for (const auto& param : prefab.color_parameters) {
    resolved[param.name] = param.default_value;
  }
  for (const auto& override : desc.color_overrides) {
    resolved[override.name] = override.value;
  }
  return resolved;
}

ecs::Entity createMeshEntity(ecs::World& world,
                             renderer::GraphicsDevice* graphics,
                             const EffectPrefabEntry& entry,
                             const std::string& entity_name,
                             const components::TransformComponent& world_transform,
                             const std::unordered_map<std::string, math::Color>& resolved_params) {
  ecs::Entity entity = world.createEntity();
  if (!entity_name.empty()) {
    world.setName(entity, entity_name);
  }
  world.add(entity, world_transform);

  renderer::MaterialId material_id = renderer::kInvalidMaterial;
  if (graphics != nullptr) {
    renderer::MaterialDesc material_desc = entry.mesh.material.material;
    if (entry.mesh.material.base_color_binding.enabled) {
      material_desc.base_color =
          resolveColorBinding(entry.mesh.material.base_color_binding,
                              resolved_params,
                              material_desc.base_color);
    }
    if (entry.mesh.material.emissive_color_binding.enabled) {
      material_desc.emissive_color =
          resolveColorBinding(entry.mesh.material.emissive_color_binding,
                              resolved_params,
                              material_desc.emissive_color);
    }
    material_id = graphics->createMaterial(material_desc);
  }

  world.add(entity,
            components::MeshComponent{
                .mesh_key = entry.mesh.mesh_path.string(),
                .material_id = material_id,
                .owns_material_id = material_id != renderer::kInvalidMaterial,
                .visible = entry.mesh.visible,
                .shadow_visible = entry.mesh.shadow_visible,
            });
  return entity;
}

ecs::Entity createParticleEntity(
    ecs::World& world,
    const EffectPrefabEntry& entry,
    const std::string& entity_name,
    const components::TransformComponent& world_transform,
    const std::unordered_map<std::string, math::Color>& resolved_params) {
  std::optional<components::ParticleEffectOverrideComponent> effect_override =
      entry.particle.effect_override;
  if (entry.particle.start_color_binding.enabled) {
    effect_override->start_color =
        resolveColorBinding(entry.particle.start_color_binding,
                            resolved_params,
                            effect_override->start_color.value_or(math::Color{}));
  }
  if (entry.particle.end_color_binding.enabled) {
    effect_override->end_color =
        resolveColorBinding(entry.particle.end_color_binding,
                            resolved_params,
                            effect_override->end_color.value_or(math::Color{}));
  }

  return particles::createEffectEntity(
      world,
      particles::ParticleEffectEntityDesc{
          .name = entity_name,
          .effect_key = entry.particle.effect_key,
          .transform = world_transform,
          .enabled = entry.particle.enabled,
          .playing = entry.particle.playing,
          .auto_apply = entry.particle.auto_apply,
          .preserve_enabled = entry.particle.preserve_enabled,
          .preserve_playing = entry.particle.preserve_playing,
          .effect_override = effect_override,
      });
}

ecs::Entity createLightEntity(ecs::World& world,
                              const EffectPrefabEntry& entry,
                              const std::string& entity_name,
                              const components::TransformComponent& world_transform,
                              const std::unordered_map<std::string, math::Color>& resolved_params) {
  ecs::Entity entity = world.createEntity();
  if (!entity_name.empty()) {
    world.setName(entity, entity_name);
  }
  world.add(entity, world_transform);
  components::LightComponent light = entry.light.light;
  if (entry.light.color_binding.enabled) {
    light.color = resolveColorBinding(entry.light.color_binding, resolved_params, light.color);
  }
  world.add(entity, light);
  return entity;
}

}  // namespace

bool loadEffectPrefab(const std::filesystem::path& path, EffectPrefab& out_prefab) {
  std::ifstream file(path);
  if (!file.is_open()) {
    spdlog::error("Effect prefab load failed: could not open {}", path.string());
    return false;
  }

  EffectPrefab prefab{};
  prefab.source_path = std::filesystem::absolute(path);

  enum class ActiveSection {
    None,
    Prefab,
    Color,
    Entry,
  };

  ActiveSection active_section = ActiveSection::None;
  size_t active_color_index = 0u;
  size_t active_entry_index = 0u;

  std::string line;
  size_t line_number = 0u;
  while (std::getline(file, line)) {
    line_number += 1u;
    const size_t comment_pos = line.find('#');
    if (comment_pos != std::string::npos) {
      line.erase(comment_pos);
    }

    const std::string trimmed = trim(line);
    if (trimmed.empty()) {
      continue;
    }

    if (trimmed.front() == '[' && trimmed.back() == ']') {
      std::string section_kind;
      std::string section_name;
      if (!parseSectionHeader(std::string_view(trimmed).substr(1u, trimmed.size() - 2u),
                              section_kind,
                              section_name)) {
        spdlog::error("Effect prefab parse failed: {}:{} invalid section header",
                      path.string(),
                      line_number);
        return false;
      }

      if (section_kind == "prefab") {
        active_section = ActiveSection::Prefab;
        continue;
      }
      if (section_kind == "color") {
        if (section_name.empty()) {
          spdlog::error("Effect prefab parse failed: {}:{} color section missing name",
                        path.string(),
                        line_number);
          return false;
        }
        active_color_index = prefab.color_parameters.size();
        prefab.color_parameters.push_back(EffectPrefabColorParameter{.name = section_name});
        active_section = ActiveSection::Color;
        continue;
      }

      EffectPrefabEntry entry{};
      if (section_kind == "mesh") {
        entry.type = EffectPrefabEntry::Type::Mesh;
      } else if (section_kind == "particle") {
        entry.type = EffectPrefabEntry::Type::Particle;
      } else if (section_kind == "light") {
        entry.type = EffectPrefabEntry::Type::Light;
      } else {
        spdlog::error("Effect prefab parse failed: {}:{} unknown section '{}'",
                      path.string(),
                      line_number,
                      section_kind);
        return false;
      }

      if (section_name.empty()) {
        spdlog::error("Effect prefab parse failed: {}:{} {} section missing name",
                      path.string(),
                      line_number,
                      section_kind);
        return false;
      }

      entry.name = section_name;
      active_entry_index = prefab.entries.size();
      prefab.entries.push_back(std::move(entry));
      active_section = ActiveSection::Entry;
      continue;
    }

    const size_t equals_pos = trimmed.find('=');
    if (equals_pos == std::string::npos) {
      spdlog::error("Effect prefab parse failed: {}:{} missing '='",
                    path.string(),
                    line_number);
      return false;
    }

    const std::string key = lowercase(trim(std::string_view(trimmed).substr(0u, equals_pos)));
    const std::string value = trim(std::string_view(trimmed).substr(equals_pos + 1u));
    std::string parse_error;
    bool ok = false;
    switch (active_section) {
      case ActiveSection::Prefab:
        ok = applyPrefabField(prefab, key, value, parse_error);
        break;
      case ActiveSection::Color:
        ok = applyColorParameterField(prefab.color_parameters[active_color_index],
                                      key,
                                      value,
                                      parse_error);
        break;
      case ActiveSection::Entry: {
        auto& entry = prefab.entries[active_entry_index];
        switch (entry.type) {
          case EffectPrefabEntry::Type::Mesh:
            ok = applyMeshField(prefab, entry, key, value, parse_error);
            break;
          case EffectPrefabEntry::Type::Particle:
            ok = applyParticleField(entry, key, value, parse_error);
            break;
          case EffectPrefabEntry::Type::Light:
            ok = applyLightField(entry, key, value, parse_error);
            break;
        }
        break;
      }
      case ActiveSection::None:
        parse_error = "field specified outside a section";
        break;
    }

    if (!ok) {
      spdlog::error("Effect prefab parse failed: {}:{} {}",
                    path.string(),
                    line_number,
                    parse_error.empty() ? "invalid value" : parse_error);
      return false;
    }
  }

  if (prefab.name.empty()) {
    prefab.name = path.stem().string();
  }

  out_prefab = std::move(prefab);
  return true;
}

std::optional<EffectPrefab> loadEffectPrefab(const std::filesystem::path& path) {
  EffectPrefab prefab{};
  if (!loadEffectPrefab(path, prefab)) {
    return std::nullopt;
  }
  return prefab;
}

std::optional<EffectPrefabInstance> instantiateEffectPrefab(
    ecs::World& world,
    renderer::GraphicsDevice* graphics,
    const EffectPrefab& prefab,
    const EffectPrefabInstantiateDesc& desc) {
  ecs::Entity root = world.createEntity();
  const std::string root_name = !desc.name.empty() ? desc.name : prefab.name;
  if (!root_name.empty()) {
    world.setName(root, root_name);
  }
  world.add(root, desc.transform);

  const std::unordered_map<std::string, math::Color> resolved_params =
      resolveColorParameters(prefab, desc);

  components::EffectPrefabInstanceComponent instance_component{};
  instance_component.prefab_name = prefab.name;
  instance_component.enabled = true;

  EffectPrefabInstance instance{};
  instance.root = root;

  for (const auto& entry : prefab.entries) {
    const std::string entity_name =
        !root_name.empty() ? root_name + "/" + entry.name : entry.name;
    const components::TransformComponent world_transform =
        composeTransform(desc.transform, entry.local_transform);

    ecs::Entity member{};
    switch (entry.type) {
      case EffectPrefabEntry::Type::Mesh:
        member = createMeshEntity(world, graphics, entry, entity_name, world_transform, resolved_params);
        break;
      case EffectPrefabEntry::Type::Particle:
        member = createParticleEntity(world, entry, entity_name, world_transform, resolved_params);
        break;
      case EffectPrefabEntry::Type::Light:
        member = createLightEntity(world, entry, entity_name, world_transform, resolved_params);
        break;
    }

    if (!member.isValid()) {
      continue;
    }

    components::EffectPrefabMemberComponent member_component{};
    member_component.root = root;
    member_component.name = entry.name;
    member_component.local_transform = entry.local_transform;
    switch (entry.type) {
      case EffectPrefabEntry::Type::Mesh:
        member_component.kind = components::EffectPrefabMemberKind::Mesh;
        member_component.mesh_visible = entry.mesh.visible;
        break;
      case EffectPrefabEntry::Type::Particle:
        member_component.kind = components::EffectPrefabMemberKind::Particle;
        break;
      case EffectPrefabEntry::Type::Light:
        member_component.kind = components::EffectPrefabMemberKind::Light;
        member_component.light_intensity = entry.light.light.intensity;
        member_component.light_range = entry.light.light.range;
        break;
    }
    world.add(member, std::move(member_component));

    instance_component.members.push_back(member);
    instance.members.push_back(member);
    instance.named_members[entry.name] = member;
  }

  world.add(root, std::move(instance_component));
  return instance;
}

std::optional<EffectPrefabInstance> instantiateEffectPrefab(
    ecs::World& world,
    renderer::GraphicsDevice* graphics,
    const std::filesystem::path& path,
    const EffectPrefabInstantiateDesc& desc) {
  std::optional<EffectPrefab> prefab = loadEffectPrefab(path);
  if (!prefab.has_value()) {
    return std::nullopt;
  }
  return instantiateEffectPrefab(world, graphics, *prefab, desc);
}

bool setPrefabPlayback(ecs::World& world, ecs::Entity root, bool enabled) {
  if (!world.isAlive(root) || !world.has<components::EffectPrefabInstanceComponent>(root)) {
    return false;
  }

  auto& instance = world.get<components::EffectPrefabInstanceComponent>(root);
  instance.enabled = enabled;

  for (const ecs::Entity member : instance.members) {
    if (!world.isAlive(member)) {
      continue;
    }

    if (world.has<components::EffectPrefabMemberComponent>(member)) {
      const auto& prefab_member = world.get<components::EffectPrefabMemberComponent>(member);
      if (prefab_member.kind == components::EffectPrefabMemberKind::Mesh &&
          world.has<components::MeshComponent>(member)) {
        world.get<components::MeshComponent>(member).visible =
            enabled && prefab_member.mesh_visible;
      } else if (prefab_member.kind == components::EffectPrefabMemberKind::Light &&
                 world.has<components::LightComponent>(member)) {
        auto& light = world.get<components::LightComponent>(member);
        light.intensity = enabled ? prefab_member.light_intensity : 0.0f;
        light.range = enabled ? prefab_member.light_range : 0.0f;
      }
    }

    if (world.has<components::ParticleEmitterComponent>(member)) {
      particles::setEffectPlayback(world, member, enabled, enabled);
    }
  }

  return true;
}

bool restartPrefab(ecs::World& world, ecs::Entity root) {
  if (!world.isAlive(root) || !world.has<components::EffectPrefabInstanceComponent>(root)) {
    return false;
  }

  auto& instance = world.get<components::EffectPrefabInstanceComponent>(root);
  bool restarted_any = false;
  for (const ecs::Entity member : instance.members) {
    restarted_any = particles::restartEffect(world, member) || restarted_any;
  }
  return restarted_any;
}

}  // namespace karma::prefabs
