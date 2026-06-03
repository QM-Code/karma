#include "karma/content/prefabs/component_serializer_registry.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "karma/world/components/beam_path.h"
#include "karma/world/components/collider.h"
#include "karma/world/components/light.h"
#include "karma/world/components/light_pulse.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/particle_effect.h"
#include "karma/world/components/particle_effect_override.h"
#include "karma/world/components/particle_emitter.h"
#include "karma/world/components/rigidbody.h"
#include "karma/world/components/tag.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/visibility.h"
#include "karma/world/components/volume_sphere.h"

namespace karma::prefabs {

namespace {

using Json = nlohmann::json;

Json toJson(const math::Vec3& value) {
  return Json::array({value.x, value.y, value.z});
}

Json toJson(const math::Quat& value) {
  return Json::array({value.x, value.y, value.z, value.w});
}

Json toJson(const math::Color& value) {
  return Json::array({value.r, value.g, value.b, value.a});
}

bool readFloatValue(const Json& value, float& out) {
  if (!value.is_number()) {
    return false;
  }
  out = value.get<float>();
  return true;
}

bool readBool(const Json& object, std::string_view key, bool& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_boolean()) {
    return false;
  }
  out = it->get<bool>();
  return true;
}

bool readString(const Json& object, std::string_view key, std::string& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  out = it->get<std::string>();
  return true;
}

bool readFloat(const Json& object, std::string_view key, float& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  return readFloatValue(*it, out);
}

bool readUint32(const Json& object, std::string_view key, uint32_t& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_number_unsigned() && !it->is_number_integer()) {
    return false;
  }
  const int64_t value = it->get<int64_t>();
  if (value < 0 || value > static_cast<int64_t>(UINT32_MAX)) {
    return false;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

bool readVec3Value(const Json& value, math::Vec3& out) {
  if (!value.is_array() || value.size() != 3u) {
    return false;
  }
  return readFloatValue(value[0], out.x) &&
         readFloatValue(value[1], out.y) &&
         readFloatValue(value[2], out.z);
}

bool readQuatValue(const Json& value, math::Quat& out) {
  if (!value.is_array() || value.size() != 4u) {
    return false;
  }
  return readFloatValue(value[0], out.x) &&
         readFloatValue(value[1], out.y) &&
         readFloatValue(value[2], out.z) &&
         readFloatValue(value[3], out.w);
}

bool readColorValue(const Json& value, math::Color& out) {
  if (!value.is_array() || value.size() != 4u) {
    return false;
  }
  return readFloatValue(value[0], out.r) &&
         readFloatValue(value[1], out.g) &&
         readFloatValue(value[2], out.b) &&
         readFloatValue(value[3], out.a);
}

bool readVec3(const Json& object, std::string_view key, math::Vec3& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  return readVec3Value(*it, out);
}

bool readQuat(const Json& object, std::string_view key, math::Quat& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  return readQuatValue(*it, out);
}

bool readColor(const Json& object, std::string_view key, math::Color& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  return readColorValue(*it, out);
}

std::string lightTypeName(components::LightComponent::Type type) {
  switch (type) {
    case components::LightComponent::Type::Directional:
      return "directional";
    case components::LightComponent::Type::Point:
      return "point";
    case components::LightComponent::Type::Spot:
      return "spot";
  }
  return "point";
}

bool readLightType(const Json& object, components::LightComponent::Type& out) {
  const auto it = object.find("type");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "directional") {
    out = components::LightComponent::Type::Directional;
    return true;
  }
  if (value == "point") {
    out = components::LightComponent::Type::Point;
    return true;
  }
  if (value == "spot") {
    out = components::LightComponent::Type::Spot;
    return true;
  }
  return false;
}

std::string spawnShapeName(components::ParticleSpawnShape shape) {
  switch (shape) {
    case components::ParticleSpawnShape::Box:
      return "box";
    case components::ParticleSpawnShape::Sphere:
      return "sphere";
    case components::ParticleSpawnShape::SphereSurface:
      return "sphere_surface";
  }
  return "box";
}

bool readSpawnShape(const Json& object, components::ParticleSpawnShape& out) {
  const auto it = object.find("spawn_shape");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "box") {
    out = components::ParticleSpawnShape::Box;
    return true;
  }
  if (value == "sphere") {
    out = components::ParticleSpawnShape::Sphere;
    return true;
  }
  if (value == "sphere_surface") {
    out = components::ParticleSpawnShape::SphereSurface;
    return true;
  }
  return false;
}

std::string blendModeName(renderer::ParticleBlendMode mode) {
  switch (mode) {
    case renderer::ParticleBlendMode::Additive:
      return "additive";
    case renderer::ParticleBlendMode::Alpha:
      return "alpha";
    case renderer::ParticleBlendMode::Distortion:
      return "distortion";
  }
  return "additive";
}

bool readBlendMode(const Json& object, renderer::ParticleBlendMode& out) {
  const auto it = object.find("blend_mode");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "additive") {
    out = renderer::ParticleBlendMode::Additive;
    return true;
  }
  if (value == "alpha") {
    out = renderer::ParticleBlendMode::Alpha;
    return true;
  }
  if (value == "distortion") {
    out = renderer::ParticleBlendMode::Distortion;
    return true;
  }
  return false;
}

std::string alignmentName(renderer::ParticleAlignment alignment) {
  switch (alignment) {
    case renderer::ParticleAlignment::Billboard:
      return "billboard";
    case renderer::ParticleAlignment::Ground:
      return "ground";
  }
  return "billboard";
}

bool readAlignment(const Json& object, renderer::ParticleAlignment& out) {
  const auto it = object.find("alignment");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "billboard") {
    out = renderer::ParticleAlignment::Billboard;
    return true;
  }
  if (value == "ground") {
    out = renderer::ParticleAlignment::Ground;
    return true;
  }
  return false;
}

std::string shadingModeName(renderer::ParticleShadingMode mode) {
  switch (mode) {
    case renderer::ParticleShadingMode::Standard:
      return "standard";
    case renderer::ParticleShadingMode::Shell:
      return "shell";
  }
  return "standard";
}

bool readShadingMode(const Json& object, renderer::ParticleShadingMode& out) {
  const auto it = object.find("shading_mode");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "standard") {
    out = renderer::ParticleShadingMode::Standard;
    return true;
  }
  if (value == "shell") {
    out = renderer::ParticleShadingMode::Shell;
    return true;
  }
  return false;
}

bool readOptionalColor(const Json& object,
                       std::string_view key,
                       std::optional<math::Color>& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (it->is_null()) {
    out.reset();
    return true;
  }
  math::Color color{};
  if (!readColorValue(*it, color)) {
    return false;
  }
  out = color;
  return true;
}

Json serializeTransform(const components::TransformComponent& component) {
  return Json{
      {"position", toJson(component.getPosition())},
      {"rotation", toJson(component.getRotation())},
      {"scale", toJson(component.getScale())},
  };
}

std::optional<components::TransformComponent> deserializeTransform(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  if (!readVec3(json, "position", position) ||
      !readQuat(json, "rotation", rotation) ||
      !readVec3(json, "scale", scale)) {
    return std::nullopt;
  }
  return components::TransformComponent{position, rotation, scale};
}

Json serializeLocalTransform(const components::LocalTransformComponent& component) {
  return Json{
      {"position", toJson(component.position)},
      {"rotation", toJson(component.rotation)},
      {"scale", toJson(component.scale)},
  };
}

std::optional<components::LocalTransformComponent> deserializeLocalTransform(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::LocalTransformComponent component{};
  if (!readVec3(json, "position", component.position) ||
      !readQuat(json, "rotation", component.rotation) ||
      !readVec3(json, "scale", component.scale)) {
    return std::nullopt;
  }
  return component;
}

Json serializeTag(const components::TagComponent& component) {
  return Json{{"name", component.name}};
}

std::optional<components::TagComponent> deserializeTag(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::TagComponent component{};
  if (!readString(json, "name", component.name)) {
    return std::nullopt;
  }
  return component;
}

Json serializeMesh(const components::MeshComponent& component) {
  return Json{
      {"mesh_key", component.mesh_key},
      {"material_key", component.material_key},
      {"texture_key", component.texture_key},
      {"visible", component.visible},
      {"shadow_visible", component.shadow_visible},
  };
}

std::optional<components::MeshComponent> deserializeMesh(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::MeshComponent component{};
  if (!readString(json, "mesh_key", component.mesh_key) ||
      !readString(json, "material_key", component.material_key) ||
      !readString(json, "texture_key", component.texture_key) ||
      !readBool(json, "visible", component.visible) ||
      !readBool(json, "shadow_visible", component.shadow_visible)) {
    return std::nullopt;
  }
  return component;
}

Json serializeLight(const components::LightComponent& component) {
  return Json{
      {"type", lightTypeName(component.type)},
      {"color", toJson(component.color)},
      {"intensity", component.intensity},
      {"range", component.range},
      {"inner_cone_degrees", component.inner_cone_degrees},
      {"outer_cone_degrees", component.outer_cone_degrees},
      {"casts_shadows", component.casts_shadows},
      {"shadow_extent", component.shadow_extent},
  };
}

std::optional<components::LightComponent> deserializeLight(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::LightComponent component{};
  if (!readLightType(json, component.type) ||
      !readColor(json, "color", component.color) ||
      !readFloat(json, "intensity", component.intensity) ||
      !readFloat(json, "range", component.range) ||
      !readFloat(json, "inner_cone_degrees", component.inner_cone_degrees) ||
      !readFloat(json, "outer_cone_degrees", component.outer_cone_degrees) ||
      !readBool(json, "casts_shadows", component.casts_shadows) ||
      !readFloat(json, "shadow_extent", component.shadow_extent)) {
    return std::nullopt;
  }
  return component;
}

Json serializeVisibility(const components::VisibilityComponent& component) {
  return Json{
      {"visible", component.visible},
      {"render_layer_mask", component.render_layer_mask},
      {"collision_layer_mask", component.collision_layer_mask},
  };
}

std::optional<components::VisibilityComponent> deserializeVisibility(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::VisibilityComponent component{};
  if (!readBool(json, "visible", component.visible) ||
      !readUint32(json, "render_layer_mask", component.render_layer_mask) ||
      !readUint32(json, "collision_layer_mask", component.collision_layer_mask)) {
    return std::nullopt;
  }
  return component;
}

Json serializeRigidbody(const components::RigidbodyComponent& component) {
  return Json{
      {"mass", component.mass},
      {"velocity", toJson(component.velocity)},
      {"angular_velocity", toJson(component.angular_velocity)},
      {"is_kinematic", component.is_kinematic},
      {"use_gravity", component.use_gravity},
      {"is_trigger", component.is_trigger},
  };
}

std::optional<components::RigidbodyComponent> deserializeRigidbody(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::RigidbodyComponent component{};
  if (!readFloat(json, "mass", component.mass) ||
      !readVec3(json, "velocity", component.velocity) ||
      !readVec3(json, "angular_velocity", component.angular_velocity) ||
      !readBool(json, "is_kinematic", component.is_kinematic) ||
      !readBool(json, "use_gravity", component.use_gravity) ||
      !readBool(json, "is_trigger", component.is_trigger)) {
    return std::nullopt;
  }
  return component;
}

Json serializeColliderBase(const components::ColliderComponent& component) {
  return Json{
      {"is_trigger", component.is_trigger},
      {"debug_draw", component.debug_draw},
  };
}

bool readColliderBase(const Json& json, components::ColliderComponent& component) {
  return readBool(json, "is_trigger", component.is_trigger) &&
         readBool(json, "debug_draw", component.debug_draw);
}

Json serializeBoxCollider(const components::BoxColliderComponent& component) {
  Json json = serializeColliderBase(component);
  json["center"] = toJson(component.center);
  json["half_extents"] = toJson(component.half_extents);
  return json;
}

std::optional<components::BoxColliderComponent> deserializeBoxCollider(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::BoxColliderComponent component{};
  if (!readColliderBase(json, component) ||
      !readVec3(json, "center", component.center) ||
      !readVec3(json, "half_extents", component.half_extents)) {
    return std::nullopt;
  }
  return component;
}

Json serializeSphereCollider(const components::SphereColliderComponent& component) {
  Json json = serializeColliderBase(component);
  json["center"] = toJson(component.center);
  json["radius"] = component.radius;
  return json;
}

std::optional<components::SphereColliderComponent> deserializeSphereCollider(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::SphereColliderComponent component{};
  if (!readColliderBase(json, component) ||
      !readVec3(json, "center", component.center) ||
      !readFloat(json, "radius", component.radius)) {
    return std::nullopt;
  }
  return component;
}

Json serializeCapsuleCollider(const components::CapsuleColliderComponent& component) {
  Json json = serializeColliderBase(component);
  json["center"] = toJson(component.center);
  json["radius"] = component.radius;
  json["height"] = component.height;
  return json;
}

std::optional<components::CapsuleColliderComponent> deserializeCapsuleCollider(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::CapsuleColliderComponent component{};
  if (!readColliderBase(json, component) ||
      !readVec3(json, "center", component.center) ||
      !readFloat(json, "radius", component.radius) ||
      !readFloat(json, "height", component.height)) {
    return std::nullopt;
  }
  return component;
}

Json serializeParticleEffect(const components::ParticleEffectComponent& component) {
  return Json{
      {"effect_key", component.effect_key},
      {"auto_apply", component.auto_apply},
      {"preserve_enabled", component.preserve_enabled},
      {"preserve_playing", component.preserve_playing},
      {"preserve_start_delay", component.preserve_start_delay},
  };
}

std::optional<components::ParticleEffectComponent> deserializeParticleEffect(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::ParticleEffectComponent component{};
  if (!readString(json, "effect_key", component.effect_key) ||
      !readBool(json, "auto_apply", component.auto_apply) ||
      !readBool(json, "preserve_enabled", component.preserve_enabled) ||
      !readBool(json, "preserve_playing", component.preserve_playing) ||
      !readBool(json, "preserve_start_delay", component.preserve_start_delay)) {
    return std::nullopt;
  }
  return component;
}

Json serializeParticleEffectOverride(
    const components::ParticleEffectOverrideComponent& component) {
  Json json{
      {"active", component.active},
      {"time_scale", component.time_scale},
      {"spawn_rate_scale", component.spawn_rate_scale},
      {"lifetime_scale", component.lifetime_scale},
      {"size_scale", component.size_scale},
      {"radius_scale", component.radius_scale},
      {"velocity_scale", component.velocity_scale},
      {"angular_velocity_scale", component.angular_velocity_scale},
      {"alpha_scale", component.alpha_scale},
  };
  if (component.start_color.has_value()) {
    json["start_color"] = toJson(*component.start_color);
  }
  if (component.end_color.has_value()) {
    json["end_color"] = toJson(*component.end_color);
  }
  return json;
}

std::optional<components::ParticleEffectOverrideComponent>
deserializeParticleEffectOverride(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::ParticleEffectOverrideComponent component{};
  if (!readBool(json, "active", component.active) ||
      !readFloat(json, "time_scale", component.time_scale) ||
      !readFloat(json, "spawn_rate_scale", component.spawn_rate_scale) ||
      !readFloat(json, "lifetime_scale", component.lifetime_scale) ||
      !readFloat(json, "size_scale", component.size_scale) ||
      !readFloat(json, "radius_scale", component.radius_scale) ||
      !readFloat(json, "velocity_scale", component.velocity_scale) ||
      !readFloat(json, "angular_velocity_scale", component.angular_velocity_scale) ||
      !readFloat(json, "alpha_scale", component.alpha_scale) ||
      !readOptionalColor(json, "start_color", component.start_color) ||
      !readOptionalColor(json, "end_color", component.end_color)) {
    return std::nullopt;
  }
  return component;
}

Json serializeParticleEmitter(const components::ParticleEmitterComponent& component) {
  return Json{
      {"enabled", component.enabled},
      {"playing", component.playing},
      {"loop", component.loop},
      {"emit_burst_on_start", component.emit_burst_on_start},
      {"local_space", component.local_space},
      {"layer", component.layer},
      {"depth_test", component.depth_test},
      {"blend_mode", blendModeName(component.blend_mode)},
      {"alignment", alignmentName(component.alignment)},
      {"shading_mode", shadingModeName(component.shading_mode)},
      {"use_soft_mask", component.use_soft_mask},
      {"soft_particle_distance", component.soft_particle_distance},
      {"distortion_strength", component.distortion_strength},
      {"fresnel_power", component.fresnel_power},
      {"fresnel_strength", component.fresnel_strength},
      {"refraction_strength", component.refraction_strength},
      {"interior_glow", component.interior_glow},
      {"atlas_columns", component.atlas_columns},
      {"atlas_rows", component.atlas_rows},
      {"atlas_frame_count", component.atlas_frame_count},
      {"atlas_frame_width", component.atlas_frame_width},
      {"atlas_frame_height", component.atlas_frame_height},
      {"atlas_border_x", component.atlas_border_x},
      {"atlas_border_y", component.atlas_border_y},
      {"atlas_spacing_x", component.atlas_spacing_x},
      {"atlas_spacing_y", component.atlas_spacing_y},
      {"animation_fps", component.animation_fps},
      {"animate_over_lifetime", component.animate_over_lifetime},
      {"random_start_frame", component.random_start_frame},
      {"max_particles", component.max_particles},
      {"burst_count", component.burst_count},
      {"seed", component.seed},
      {"time_scale", component.time_scale},
      {"start_delay", component.start_delay},
      {"duration", component.duration},
      {"spawn_rate", component.spawn_rate},
      {"particle_lifetime_min", component.particle_lifetime_min},
      {"particle_lifetime_max", component.particle_lifetime_max},
      {"start_size_min", component.start_size_min},
      {"start_size_max", component.start_size_max},
      {"end_size_min", component.end_size_min},
      {"end_size_max", component.end_size_max},
      {"size_curve_exponent", component.size_curve_exponent},
      {"alpha_curve_exponent", component.alpha_curve_exponent},
      {"initial_rotation_min", component.initial_rotation_min},
      {"initial_rotation_max", component.initial_rotation_max},
      {"angular_velocity_min", component.angular_velocity_min},
      {"angular_velocity_max", component.angular_velocity_max},
      {"spawn_shape", spawnShapeName(component.spawn_shape)},
      {"spawn_box_extents", toJson(component.spawn_box_extents)},
      {"spawn_radius_min", component.spawn_radius_min},
      {"spawn_radius_max", component.spawn_radius_max},
      {"radial_speed_min", component.radial_speed_min},
      {"radial_speed_max", component.radial_speed_max},
      {"velocity_min", toJson(component.velocity_min)},
      {"velocity_max", toJson(component.velocity_max)},
      {"acceleration", toJson(component.acceleration)},
      {"drag", component.drag},
      {"collide_with_ground", component.collide_with_ground},
      {"ground_height", component.ground_height},
      {"bounce_damping", component.bounce_damping},
      {"collision_friction", component.collision_friction},
      {"rest_speed_threshold", component.rest_speed_threshold},
      {"start_color", toJson(component.start_color)},
      {"end_color", toJson(component.end_color)},
  };
}

std::optional<components::ParticleEmitterComponent> deserializeParticleEmitter(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::ParticleEmitterComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readBool(json, "playing", component.playing) ||
      !readBool(json, "loop", component.loop) ||
      !readBool(json, "emit_burst_on_start", component.emit_burst_on_start) ||
      !readBool(json, "local_space", component.local_space) ||
      !readUint32(json, "layer", component.layer) ||
      !readBool(json, "depth_test", component.depth_test) ||
      !readBlendMode(json, component.blend_mode) ||
      !readAlignment(json, component.alignment) ||
      !readShadingMode(json, component.shading_mode) ||
      !readBool(json, "use_soft_mask", component.use_soft_mask) ||
      !readFloat(json, "soft_particle_distance", component.soft_particle_distance) ||
      !readFloat(json, "distortion_strength", component.distortion_strength) ||
      !readFloat(json, "fresnel_power", component.fresnel_power) ||
      !readFloat(json, "fresnel_strength", component.fresnel_strength) ||
      !readFloat(json, "refraction_strength", component.refraction_strength) ||
      !readFloat(json, "interior_glow", component.interior_glow) ||
      !readUint32(json, "atlas_columns", component.atlas_columns) ||
      !readUint32(json, "atlas_rows", component.atlas_rows) ||
      !readUint32(json, "atlas_frame_count", component.atlas_frame_count) ||
      !readUint32(json, "atlas_frame_width", component.atlas_frame_width) ||
      !readUint32(json, "atlas_frame_height", component.atlas_frame_height) ||
      !readUint32(json, "atlas_border_x", component.atlas_border_x) ||
      !readUint32(json, "atlas_border_y", component.atlas_border_y) ||
      !readUint32(json, "atlas_spacing_x", component.atlas_spacing_x) ||
      !readUint32(json, "atlas_spacing_y", component.atlas_spacing_y) ||
      !readFloat(json, "animation_fps", component.animation_fps) ||
      !readBool(json, "animate_over_lifetime", component.animate_over_lifetime) ||
      !readBool(json, "random_start_frame", component.random_start_frame) ||
      !readUint32(json, "max_particles", component.max_particles) ||
      !readUint32(json, "burst_count", component.burst_count) ||
      !readUint32(json, "seed", component.seed) ||
      !readFloat(json, "time_scale", component.time_scale) ||
      !readFloat(json, "start_delay", component.start_delay) ||
      !readFloat(json, "duration", component.duration) ||
      !readFloat(json, "spawn_rate", component.spawn_rate) ||
      !readFloat(json, "particle_lifetime_min", component.particle_lifetime_min) ||
      !readFloat(json, "particle_lifetime_max", component.particle_lifetime_max) ||
      !readFloat(json, "start_size_min", component.start_size_min) ||
      !readFloat(json, "start_size_max", component.start_size_max) ||
      !readFloat(json, "end_size_min", component.end_size_min) ||
      !readFloat(json, "end_size_max", component.end_size_max) ||
      !readFloat(json, "size_curve_exponent", component.size_curve_exponent) ||
      !readFloat(json, "alpha_curve_exponent", component.alpha_curve_exponent) ||
      !readFloat(json, "initial_rotation_min", component.initial_rotation_min) ||
      !readFloat(json, "initial_rotation_max", component.initial_rotation_max) ||
      !readFloat(json, "angular_velocity_min", component.angular_velocity_min) ||
      !readFloat(json, "angular_velocity_max", component.angular_velocity_max) ||
      !readSpawnShape(json, component.spawn_shape) ||
      !readVec3(json, "spawn_box_extents", component.spawn_box_extents) ||
      !readFloat(json, "spawn_radius_min", component.spawn_radius_min) ||
      !readFloat(json, "spawn_radius_max", component.spawn_radius_max) ||
      !readFloat(json, "radial_speed_min", component.radial_speed_min) ||
      !readFloat(json, "radial_speed_max", component.radial_speed_max) ||
      !readVec3(json, "velocity_min", component.velocity_min) ||
      !readVec3(json, "velocity_max", component.velocity_max) ||
      !readVec3(json, "acceleration", component.acceleration) ||
      !readFloat(json, "drag", component.drag) ||
      !readBool(json, "collide_with_ground", component.collide_with_ground) ||
      !readFloat(json, "ground_height", component.ground_height) ||
      !readFloat(json, "bounce_damping", component.bounce_damping) ||
      !readFloat(json, "collision_friction", component.collision_friction) ||
      !readFloat(json, "rest_speed_threshold", component.rest_speed_threshold) ||
      !readColor(json, "start_color", component.start_color) ||
      !readColor(json, "end_color", component.end_color)) {
    return std::nullopt;
  }
  return component;
}

Json serializeLightPulse(const components::LightPulseComponent& component) {
  return Json{
      {"enabled", component.enabled},
      {"active", component.active},
      {"start_delay", component.start_delay},
      {"duration", component.duration},
      {"peak_intensity", component.peak_intensity},
      {"peak_range", component.peak_range},
      {"off_intensity", component.off_intensity},
      {"off_range", component.off_range},
      {"intensity_power", component.intensity_power},
      {"range_power", component.range_power},
      {"range_floor_factor", component.range_floor_factor},
      {"hide_after_completion", component.hide_after_completion},
  };
}

std::optional<components::LightPulseComponent> deserializeLightPulse(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::LightPulseComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readBool(json, "active", component.active) ||
      !readFloat(json, "start_delay", component.start_delay) ||
      !readFloat(json, "duration", component.duration) ||
      !readFloat(json, "peak_intensity", component.peak_intensity) ||
      !readFloat(json, "peak_range", component.peak_range) ||
      !readFloat(json, "off_intensity", component.off_intensity) ||
      !readFloat(json, "off_range", component.off_range) ||
      !readFloat(json, "intensity_power", component.intensity_power) ||
      !readFloat(json, "range_power", component.range_power) ||
      !readFloat(json, "range_floor_factor", component.range_floor_factor) ||
      !readBool(json, "hide_after_completion", component.hide_after_completion)) {
    return std::nullopt;
  }
  return component;
}

Json serializeBeamPath(const components::BeamPathComponent& component) {
  Json points = Json::array();
  for (const math::Vec3& point : component.points) {
    points.push_back(toJson(point));
  }
  return Json{
      {"points", std::move(points)},
      {"core_color", toJson(component.core_color)},
      {"glow_color", toJson(component.glow_color)},
      {"core_radius", component.core_radius},
      {"glow_radius", component.glow_radius},
      {"core_intensity", component.core_intensity},
      {"glow_intensity", component.glow_intensity},
      {"endpoint_core_size", component.endpoint_core_size},
      {"endpoint_glow_size", component.endpoint_glow_size},
      {"light_count", component.light_count},
      {"light_intensity", component.light_intensity},
      {"light_range", component.light_range},
      {"light_spacing", component.light_spacing},
      {"electric_intensity", component.electric_intensity},
      {"electric_size", component.electric_size},
      {"electric_spacing", component.electric_spacing},
      {"electric_jitter_radius", component.electric_jitter_radius},
      {"electric_speed", component.electric_speed},
      {"distortion_intensity", component.distortion_intensity},
      {"distortion_size", component.distortion_size},
      {"distortion_spacing", component.distortion_spacing},
      {"distortion_jitter_radius", component.distortion_jitter_radius},
      {"distortion_strength", component.distortion_strength},
      {"distortion_soft_particle_distance", component.distortion_soft_particle_distance},
      {"distortion_speed", component.distortion_speed},
      {"layer", component.layer},
      {"visible", component.visible},
      {"depth_test", component.depth_test},
      {"closed_loop", component.closed_loop},
      {"world_space", component.world_space},
      {"endpoint_flares", component.endpoint_flares},
  };
}

std::optional<components::BeamPathComponent> deserializeBeamPath(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::BeamPathComponent component{};
  const auto points_it = json.find("points");
  if (points_it != json.end()) {
    if (!points_it->is_array()) {
      return std::nullopt;
    }
    component.points.clear();
    for (const Json& point_json : *points_it) {
      math::Vec3 point{};
      if (!readVec3Value(point_json, point)) {
        return std::nullopt;
      }
      component.points.push_back(point);
    }
  }
  if (!readColor(json, "core_color", component.core_color) ||
      !readColor(json, "glow_color", component.glow_color) ||
      !readFloat(json, "core_radius", component.core_radius) ||
      !readFloat(json, "glow_radius", component.glow_radius) ||
      !readFloat(json, "core_intensity", component.core_intensity) ||
      !readFloat(json, "glow_intensity", component.glow_intensity) ||
      !readFloat(json, "endpoint_core_size", component.endpoint_core_size) ||
      !readFloat(json, "endpoint_glow_size", component.endpoint_glow_size) ||
      !readUint32(json, "light_count", component.light_count) ||
      !readFloat(json, "light_intensity", component.light_intensity) ||
      !readFloat(json, "light_range", component.light_range) ||
      !readFloat(json, "light_spacing", component.light_spacing) ||
      !readFloat(json, "electric_intensity", component.electric_intensity) ||
      !readFloat(json, "electric_size", component.electric_size) ||
      !readFloat(json, "electric_spacing", component.electric_spacing) ||
      !readFloat(json, "electric_jitter_radius", component.electric_jitter_radius) ||
      !readFloat(json, "electric_speed", component.electric_speed) ||
      !readFloat(json, "distortion_intensity", component.distortion_intensity) ||
      !readFloat(json, "distortion_size", component.distortion_size) ||
      !readFloat(json, "distortion_spacing", component.distortion_spacing) ||
      !readFloat(json, "distortion_jitter_radius", component.distortion_jitter_radius) ||
      !readFloat(json, "distortion_strength", component.distortion_strength) ||
      !readFloat(json,
                 "distortion_soft_particle_distance",
                 component.distortion_soft_particle_distance) ||
      !readFloat(json, "distortion_speed", component.distortion_speed) ||
      !readUint32(json, "layer", component.layer) ||
      !readBool(json, "visible", component.visible) ||
      !readBool(json, "depth_test", component.depth_test) ||
      !readBool(json, "closed_loop", component.closed_loop) ||
      !readBool(json, "world_space", component.world_space) ||
      !readBool(json, "endpoint_flares", component.endpoint_flares)) {
    return std::nullopt;
  }
  return component;
}

Json serializeVolumeSphere(const components::VolumeSphereComponent& component) {
  return Json{
      {"color", toJson(component.color)},
      {"emissive_color", toJson(component.emissive_color)},
      {"radius", component.radius},
      {"center_opacity", component.center_opacity},
      {"distortion_strength", component.distortion_strength},
      {"noise_strength", component.noise_strength},
      {"overlay_depth", component.overlay_depth},
      {"visible", component.visible},
      {"scale_with_transform", component.scale_with_transform},
  };
}

std::optional<components::VolumeSphereComponent> deserializeVolumeSphere(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::VolumeSphereComponent component{};
  if (!readColor(json, "color", component.color) ||
      !readColor(json, "emissive_color", component.emissive_color) ||
      !readFloat(json, "radius", component.radius) ||
      !readFloat(json, "center_opacity", component.center_opacity) ||
      !readFloat(json, "distortion_strength", component.distortion_strength) ||
      !readFloat(json, "noise_strength", component.noise_strength) ||
      !readFloat(json, "overlay_depth", component.overlay_depth) ||
      !readBool(json, "visible", component.visible) ||
      !readBool(json, "scale_with_transform", component.scale_with_transform)) {
    return std::nullopt;
  }
  return component;
}

template <typename Component, typename SerializeFn, typename DeserializeFn>
void registerComponent(ComponentSerializerRegistry& registry,
                       std::string type_name,
                       SerializeFn serialize,
                       DeserializeFn deserialize) {
  registry.registerSerializer(ComponentSerializer{
      .type_name = std::move(type_name),
      .has =
          [](const ecs::World& world, ecs::Entity entity) {
            return world.has<Component>(entity);
          },
      .serialize =
          [serialize = std::move(serialize)](const ecs::World& world, ecs::Entity entity) {
            return serialize(world.get<Component>(entity));
          },
      .deserialize =
          [deserialize = std::move(deserialize)](
              ecs::World& world, ecs::Entity entity, const Json& json) {
            std::optional<Component> component = deserialize(json);
            if (!component.has_value()) {
              return false;
            }
            world.add(entity, std::move(*component));
            return true;
          },
  });
}

}  // namespace

bool ComponentSerializerRegistry::registerSerializer(ComponentSerializer serializer) {
  if (serializer.type_name.empty() || !serializer.has || !serializer.serialize ||
      !serializer.deserialize) {
    return false;
  }

  const auto it = indices_.find(serializer.type_name);
  if (it != indices_.end()) {
    serializers_[it->second] = std::move(serializer);
    return true;
  }

  indices_[serializer.type_name] = serializers_.size();
  serializers_.push_back(std::move(serializer));
  return true;
}

void ComponentSerializerRegistry::clear() {
  serializers_.clear();
  indices_.clear();
}

const ComponentSerializer* ComponentSerializerRegistry::find(std::string_view type_name) const {
  const auto it = indices_.find(std::string(type_name));
  if (it == indices_.end()) {
    return nullptr;
  }
  return &serializers_[it->second];
}

ComponentSerializerRegistry& componentSerializerRegistry() {
  static ComponentSerializerRegistry registry;
  return registry;
}

void registerBuiltinComponentSerializers(ComponentSerializerRegistry& registry) {
  registerComponent<components::TagComponent>(
      registry, "TagComponent", serializeTag, deserializeTag);
  registerComponent<components::TransformComponent>(
      registry, "TransformComponent", serializeTransform, deserializeTransform);
  registerComponent<components::LocalTransformComponent>(
      registry, "LocalTransformComponent", serializeLocalTransform, deserializeLocalTransform);
  registerComponent<components::MeshComponent>(
      registry, "MeshComponent", serializeMesh, deserializeMesh);
  registerComponent<components::LightComponent>(
      registry, "LightComponent", serializeLight, deserializeLight);
  registerComponent<components::LightPulseComponent>(
      registry, "LightPulseComponent", serializeLightPulse, deserializeLightPulse);
  registerComponent<components::VisibilityComponent>(
      registry, "VisibilityComponent", serializeVisibility, deserializeVisibility);
  registerComponent<components::RigidbodyComponent>(
      registry, "RigidbodyComponent", serializeRigidbody, deserializeRigidbody);
  registerComponent<components::BoxColliderComponent>(
      registry, "BoxColliderComponent", serializeBoxCollider, deserializeBoxCollider);
  registerComponent<components::SphereColliderComponent>(
      registry, "SphereColliderComponent", serializeSphereCollider, deserializeSphereCollider);
  registerComponent<components::CapsuleColliderComponent>(
      registry, "CapsuleColliderComponent", serializeCapsuleCollider, deserializeCapsuleCollider);
  registerComponent<components::ParticleEffectComponent>(
      registry, "ParticleEffectComponent", serializeParticleEffect, deserializeParticleEffect);
  registerComponent<components::ParticleEffectOverrideComponent>(
      registry,
      "ParticleEffectOverrideComponent",
      serializeParticleEffectOverride,
      deserializeParticleEffectOverride);
  registerComponent<components::ParticleEmitterComponent>(
      registry, "ParticleEmitterComponent", serializeParticleEmitter, deserializeParticleEmitter);
  registerComponent<components::BeamPathComponent>(
      registry, "BeamPathComponent", serializeBeamPath, deserializeBeamPath);
  registerComponent<components::VolumeSphereComponent>(
      registry, "VolumeSphereComponent", serializeVolumeSphere, deserializeVolumeSphere);
}

void ensureBuiltinComponentSerializers() {
  ComponentSerializerRegistry& registry = componentSerializerRegistry();
  if (registry.find("TransformComponent") != nullptr) {
    return;
  }
  registerBuiltinComponentSerializers(registry);
}

}  // namespace karma::prefabs
