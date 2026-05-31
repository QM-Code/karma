#include "karma/features/visual/particles/effect_library.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace karma::particles {

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

template <typename T>
bool parseNumber(std::string_view text, T& out_value) {
  const std::string value = trim(text);
  if (value.empty()) {
    return false;
  }
  try {
    if constexpr (std::is_same_v<T, float>) {
      out_value = std::stof(value);
    } else if constexpr (std::is_same_v<T, uint32_t>) {
      out_value = static_cast<uint32_t>(std::stoul(value));
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

bool parseBlendMode(std::string_view text, renderer::ParticleBlendMode& out_value) {
  const std::string value = lowercase(trim(text));
  if (value == "additive") {
    out_value = renderer::ParticleBlendMode::Additive;
    return true;
  }
  if (value == "alpha") {
    out_value = renderer::ParticleBlendMode::Alpha;
    return true;
  }
  if (value == "distortion" ||
      value == "heat_distortion" ||
      value == "heat-distortion") {
    out_value = renderer::ParticleBlendMode::Distortion;
    return true;
  }
  return false;
}

bool parseAlignment(std::string_view text, renderer::ParticleAlignment& out_value) {
  const std::string value = lowercase(trim(text));
  if (value == "billboard" || value == "camera" || value == "camera_facing") {
    out_value = renderer::ParticleAlignment::Billboard;
    return true;
  }
  if (value == "ground" || value == "decal" || value == "floor") {
    out_value = renderer::ParticleAlignment::Ground;
    return true;
  }
  return false;
}

bool parseShadingMode(std::string_view text, renderer::ParticleShadingMode& out_value) {
  const std::string value = lowercase(trim(text));
  if (value == "standard" || value == "default") {
    out_value = renderer::ParticleShadingMode::Standard;
    return true;
  }
  if (value == "shell" || value == "sphere_shell" || value == "orb_shell" ||
      value == "spherical_shell") {
    out_value = renderer::ParticleShadingMode::Shell;
    return true;
  }
  return false;
}

bool parseSpawnShape(std::string_view text, components::ParticleSpawnShape& out_value) {
  const std::string value = lowercase(trim(text));
  if (value == "box") {
    out_value = components::ParticleSpawnShape::Box;
    return true;
  }
  if (value == "sphere") {
    out_value = components::ParticleSpawnShape::Sphere;
    return true;
  }
  if (value == "sphere_surface" || value == "sphere-surface" || value == "shell") {
    out_value = components::ParticleSpawnShape::SphereSurface;
    return true;
  }
  return false;
}

bool applyEffectField(ParticleEffectDesc& desc,
                      const std::string& key,
                      const std::string& raw_value,
                      std::string& out_error) {
  auto& emitter = desc.emitter;
  if (key == "enabled") {
    return parseBool(raw_value, emitter.enabled);
  }
  if (key == "playing") {
    return parseBool(raw_value, emitter.playing);
  }
  if (key == "loop") {
    return parseBool(raw_value, emitter.loop);
  }
  if (key == "emit_burst_on_start") {
    return parseBool(raw_value, emitter.emit_burst_on_start);
  }
  if (key == "local_space") {
    return parseBool(raw_value, emitter.local_space);
  }
  if (key == "layer") {
    return parseNumber(raw_value, emitter.layer);
  }
  if (key == "depth_test") {
    return parseBool(raw_value, emitter.depth_test);
  }
  if (key == "blend_mode") {
    return parseBlendMode(raw_value, emitter.blend_mode);
  }
  if (key == "alignment") {
    return parseAlignment(raw_value, emitter.alignment);
  }
  if (key == "shading_mode") {
    return parseShadingMode(raw_value, emitter.shading_mode);
  }
  if (key == "use_soft_mask") {
    return parseBool(raw_value, emitter.use_soft_mask);
  }
  if (key == "soft_particle_distance") {
    return parseNumber(raw_value, emitter.soft_particle_distance);
  }
  if (key == "distortion_strength") {
    return parseNumber(raw_value, emitter.distortion_strength);
  }
  if (key == "fresnel_power") {
    return parseNumber(raw_value, emitter.fresnel_power);
  }
  if (key == "fresnel_strength") {
    return parseNumber(raw_value, emitter.fresnel_strength);
  }
  if (key == "refraction_strength") {
    return parseNumber(raw_value, emitter.refraction_strength);
  }
  if (key == "interior_glow") {
    return parseNumber(raw_value, emitter.interior_glow);
  }
  if (key == "texture") {
    desc.texture_key = stripQuotes(trim(raw_value));
    return true;
  }
  if (key == "atlas_columns") {
    return parseNumber(raw_value, emitter.atlas_columns);
  }
  if (key == "atlas_rows") {
    return parseNumber(raw_value, emitter.atlas_rows);
  }
  if (key == "atlas_frame_count") {
    return parseNumber(raw_value, emitter.atlas_frame_count);
  }
  if (key == "atlas_frame_width") {
    return parseNumber(raw_value, emitter.atlas_frame_width);
  }
  if (key == "atlas_frame_height") {
    return parseNumber(raw_value, emitter.atlas_frame_height);
  }
  if (key == "atlas_border") {
    uint32_t value = 0u;
    if (!parseNumber(raw_value, value)) {
      return false;
    }
    emitter.atlas_border_x = value;
    emitter.atlas_border_y = value;
    return true;
  }
  if (key == "atlas_border_x") {
    return parseNumber(raw_value, emitter.atlas_border_x);
  }
  if (key == "atlas_border_y") {
    return parseNumber(raw_value, emitter.atlas_border_y);
  }
  if (key == "atlas_spacing" || key == "atlas_gutter") {
    uint32_t value = 0u;
    if (!parseNumber(raw_value, value)) {
      return false;
    }
    emitter.atlas_spacing_x = value;
    emitter.atlas_spacing_y = value;
    return true;
  }
  if (key == "atlas_spacing_x" || key == "atlas_gutter_x") {
    return parseNumber(raw_value, emitter.atlas_spacing_x);
  }
  if (key == "atlas_spacing_y" || key == "atlas_gutter_y") {
    return parseNumber(raw_value, emitter.atlas_spacing_y);
  }
  if (key == "animation_fps") {
    return parseNumber(raw_value, emitter.animation_fps);
  }
  if (key == "animate_over_lifetime") {
    return parseBool(raw_value, emitter.animate_over_lifetime);
  }
  if (key == "random_start_frame") {
    return parseBool(raw_value, emitter.random_start_frame);
  }
  if (key == "max_particles") {
    return parseNumber(raw_value, emitter.max_particles);
  }
  if (key == "burst_count") {
    return parseNumber(raw_value, emitter.burst_count);
  }
  if (key == "seed") {
    return parseNumber(raw_value, emitter.seed);
  }
  if (key == "time_scale") {
    return parseNumber(raw_value, emitter.time_scale);
  }
  if (key == "duration") {
    return parseNumber(raw_value, emitter.duration);
  }
  if (key == "spawn_rate") {
    return parseNumber(raw_value, emitter.spawn_rate);
  }
  if (key == "particle_lifetime_min") {
    return parseNumber(raw_value, emitter.particle_lifetime_min);
  }
  if (key == "particle_lifetime_max") {
    return parseNumber(raw_value, emitter.particle_lifetime_max);
  }
  if (key == "start_size_min") {
    return parseNumber(raw_value, emitter.start_size_min);
  }
  if (key == "start_size_max") {
    return parseNumber(raw_value, emitter.start_size_max);
  }
  if (key == "end_size_min") {
    return parseNumber(raw_value, emitter.end_size_min);
  }
  if (key == "end_size_max") {
    return parseNumber(raw_value, emitter.end_size_max);
  }
  if (key == "size_curve_exponent") {
    return parseNumber(raw_value, emitter.size_curve_exponent);
  }
  if (key == "alpha_curve_exponent") {
    return parseNumber(raw_value, emitter.alpha_curve_exponent);
  }
  if (key == "initial_rotation_min") {
    return parseNumber(raw_value, emitter.initial_rotation_min);
  }
  if (key == "initial_rotation_max") {
    return parseNumber(raw_value, emitter.initial_rotation_max);
  }
  if (key == "angular_velocity_min") {
    return parseNumber(raw_value, emitter.angular_velocity_min);
  }
  if (key == "angular_velocity_max") {
    return parseNumber(raw_value, emitter.angular_velocity_max);
  }
  if (key == "spawn_shape") {
    return parseSpawnShape(raw_value, emitter.spawn_shape);
  }
  if (key == "spawn_box_extents") {
    return parseVec3(raw_value, emitter.spawn_box_extents);
  }
  if (key == "spawn_radius_min") {
    return parseNumber(raw_value, emitter.spawn_radius_min);
  }
  if (key == "spawn_radius_max") {
    return parseNumber(raw_value, emitter.spawn_radius_max);
  }
  if (key == "radial_speed_min") {
    return parseNumber(raw_value, emitter.radial_speed_min);
  }
  if (key == "radial_speed_max") {
    return parseNumber(raw_value, emitter.radial_speed_max);
  }
  if (key == "velocity_min") {
    return parseVec3(raw_value, emitter.velocity_min);
  }
  if (key == "velocity_max") {
    return parseVec3(raw_value, emitter.velocity_max);
  }
  if (key == "acceleration") {
    return parseVec3(raw_value, emitter.acceleration);
  }
  if (key == "drag") {
    return parseNumber(raw_value, emitter.drag);
  }
  if (key == "collide_with_ground" || key == "ground_collision") {
    return parseBool(raw_value, emitter.collide_with_ground);
  }
  if (key == "ground_height") {
    return parseNumber(raw_value, emitter.ground_height);
  }
  if (key == "bounce_damping") {
    return parseNumber(raw_value, emitter.bounce_damping);
  }
  if (key == "collision_friction") {
    return parseNumber(raw_value, emitter.collision_friction);
  }
  if (key == "rest_speed_threshold") {
    return parseNumber(raw_value, emitter.rest_speed_threshold);
  }
  if (key == "start_color") {
    return parseColor(raw_value, emitter.start_color);
  }
  if (key == "end_color") {
    return parseColor(raw_value, emitter.end_color);
  }

  out_error = "unknown field '" + key + "'";
  return false;
}

}  // namespace

void ParticleLibrary::registerEffect(const std::string& key, ParticleEffectDesc desc) {
  effects_[key] = std::move(desc);
  version_ += 1;
}

void ParticleLibrary::registerEmitterTemplate(const std::string& key,
                                              components::ParticleEmitterComponent emitter) {
  registerEffect(key, ParticleEffectDesc{.emitter = std::move(emitter)});
}

bool ParticleLibrary::registerEffectFile(const std::string& key,
                                         const std::filesystem::path& path) {
  EffectFileRecord& record = effect_files_[key];
  record.path = path;
  record.last_write_time = std::filesystem::file_time_type{};
  return reloadEffectFile(key, record);
}

void ParticleLibrary::unregisterEffect(const std::string& key) {
  const size_t erased_effects = effects_.erase(key);
  const size_t erased_files = effect_files_.erase(key);
  if (erased_effects > 0 || erased_files > 0) {
    version_ += 1;
  }
}

void ParticleLibrary::clear() {
  if (!effects_.empty() || !effect_files_.empty()) {
    effects_.clear();
    effect_files_.clear();
    version_ += 1;
  }
}

void ParticleLibrary::registerTextureAlias(const std::string& key,
                                           renderer::TextureId texture) {
  auto it = texture_aliases_.find(key);
  if (it != texture_aliases_.end() && it->second == texture) {
    return;
  }
  texture_aliases_[key] = texture;
  version_ += 1;
}

void ParticleLibrary::registerTextureAliases(
    std::initializer_list<ParticleTextureAliasRegistration> aliases) {
  for (const ParticleTextureAliasRegistration& alias : aliases) {
    registerTextureAlias(std::string(alias.key), alias.texture);
  }
}

void ParticleLibrary::unregisterTextureAlias(const std::string& key) {
  if (texture_aliases_.erase(key) > 0) {
    version_ += 1;
  }
}

void ParticleLibrary::clearTextureAliases() {
  if (!texture_aliases_.empty()) {
    texture_aliases_.clear();
    version_ += 1;
  }
}

renderer::TextureId ParticleLibrary::resolveTextureAlias(const std::string& key) const {
  auto it = texture_aliases_.find(key);
  if (it == texture_aliases_.end()) {
    return renderer::kInvalidTexture;
  }
  return it->second;
}

bool ParticleLibrary::registerEffectFiles(
    std::initializer_list<ParticleEffectFileRegistration> effects) {
  bool all_ok = true;
  for (const ParticleEffectFileRegistration& effect : effects) {
    all_ok = registerEffectFile(std::string(effect.key), effect.path) && all_ok;
  }
  return all_ok;
}

void ParticleLibrary::update() {
  if (effect_files_.empty()) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (next_poll_time_ != std::chrono::steady_clock::time_point{} &&
      now < next_poll_time_) {
    return;
  }
  next_poll_time_ = now + poll_interval_;

  for (auto& [key, record] : effect_files_) {
    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(record.path, ec);
    if (ec) {
      continue;
    }
    if (record.last_write_time != write_time) {
      reloadEffectFile(key, record);
    }
  }
}

const ParticleEffectDesc* ParticleLibrary::find(const std::string& key) const {
  auto it = effects_.find(key);
  if (it == effects_.end()) {
    return nullptr;
  }
  return &it->second;
}

const components::ParticleEmitterComponent* ParticleLibrary::findEmitterTemplate(
    const std::string& key) const {
  const ParticleEffectDesc* effect = find(key);
  return effect ? &effect->emitter : nullptr;
}

bool ParticleLibrary::instantiateEmitter(const std::string& key,
                                         components::ParticleEmitterComponent& out_emitter) const {
  const ParticleEffectDesc* effect = find(key);
  if (effect == nullptr) {
    return false;
  }
  out_emitter = effect->emitter;
  if (!effect->texture_key.empty()) {
    out_emitter.texture = resolveTextureAlias(effect->texture_key);
  }
  return true;
}

std::optional<components::ParticleEmitterComponent> ParticleLibrary::instantiateEmitter(
    const std::string& key) const {
  components::ParticleEmitterComponent emitter{};
  if (!instantiateEmitter(key, emitter)) {
    return std::nullopt;
  }
  return emitter;
}

bool ParticleLibrary::reloadEffectFile(const std::string& key, EffectFileRecord& record) {
  ParticleEffectDesc desc{};
  if (!parseEffectFile(record.path, desc)) {
    return false;
  }

  std::error_code ec;
  record.last_write_time = std::filesystem::last_write_time(record.path, ec);
  if (ec) {
    record.last_write_time = std::filesystem::file_time_type{};
  }

  effects_[key] = std::move(desc);
  version_ += 1;
  spdlog::info("Particle effect '{}' reloaded from {}", key, record.path.string());
  return true;
}

bool ParticleLibrary::parseEffectFile(const std::filesystem::path& path,
                                      ParticleEffectDesc& out_desc) const {
  std::ifstream file(path);
  if (!file.is_open()) {
    spdlog::error("Particle effect load failed: could not open {}", path.string());
    return false;
  }

  ParticleEffectDesc parsed{};
  std::string line;
  size_t line_number = 0;
  while (std::getline(file, line)) {
    line_number += 1;
    const size_t comment_pos = line.find('#');
    if (comment_pos != std::string::npos) {
      line.erase(comment_pos);
    }

    const std::string trimmed = trim(line);
    if (trimmed.empty()) {
      continue;
    }

    const size_t equals_pos = trimmed.find('=');
    if (equals_pos == std::string::npos) {
      spdlog::error("Particle effect parse failed: {}:{} missing '='",
                    path.string(),
                    line_number);
      return false;
    }

    const std::string key = lowercase(trim(std::string_view(trimmed).substr(0, equals_pos)));
    const std::string value = trim(std::string_view(trimmed).substr(equals_pos + 1));
    std::string parse_error;
    if (!applyEffectField(parsed, key, value, parse_error)) {
      spdlog::error("Particle effect parse failed: {}:{} {}",
                    path.string(),
                    line_number,
                    parse_error.empty() ? "invalid value" : parse_error);
      return false;
    }
  }

  out_desc = std::move(parsed);
  return true;
}

}  // namespace karma::particles
