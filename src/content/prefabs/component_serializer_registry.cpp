#include "karma/content/prefabs/component_serializer_registry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "karma/world/components/animator.h"
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
#include "karma/world/components/volumetric.h"

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

std::string rootMotionModeName(components::RootMotionMode mode) {
  switch (mode) {
    case components::RootMotionMode::Disabled:
      return "disabled";
    case components::RootMotionMode::ApplyToLocalTransform:
      return "apply_to_local_transform";
    case components::RootMotionMode::ExposeDelta:
      return "expose_delta";
  }
  return "disabled";
}

bool readRootMotionMode(const Json& object, components::RootMotionMode& out) {
  const auto it = object.find("root_motion_mode");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "disabled") {
    out = components::RootMotionMode::Disabled;
    return true;
  }
  if (value == "apply_to_local_transform") {
    out = components::RootMotionMode::ApplyToLocalTransform;
    return true;
  }
  if (value == "expose_delta") {
    out = components::RootMotionMode::ExposeDelta;
    return true;
  }
  return false;
}

std::string animatorParameterTypeName(components::AnimatorParameterType type) {
  switch (type) {
    case components::AnimatorParameterType::Bool:
      return "bool";
    case components::AnimatorParameterType::Int:
      return "int";
    case components::AnimatorParameterType::Float:
      return "float";
    case components::AnimatorParameterType::Trigger:
      return "trigger";
  }
  return "float";
}

std::optional<components::AnimatorParameterType> parseAnimatorParameterType(std::string_view value) {
  if (value == "bool") return components::AnimatorParameterType::Bool;
  if (value == "int") return components::AnimatorParameterType::Int;
  if (value == "float") return components::AnimatorParameterType::Float;
  if (value == "trigger") return components::AnimatorParameterType::Trigger;
  return std::nullopt;
}

std::string animatorConditionOpName(components::AnimatorConditionOp op) {
  switch (op) {
    case components::AnimatorConditionOp::If:
      return "if";
    case components::AnimatorConditionOp::IfNot:
      return "if_not";
    case components::AnimatorConditionOp::Equals:
      return "equals";
    case components::AnimatorConditionOp::NotEquals:
      return "not_equals";
    case components::AnimatorConditionOp::Greater:
      return "greater";
    case components::AnimatorConditionOp::GreaterOrEqual:
      return "greater_or_equal";
    case components::AnimatorConditionOp::Less:
      return "less";
    case components::AnimatorConditionOp::LessOrEqual:
      return "less_or_equal";
  }
  return "if";
}

std::optional<components::AnimatorConditionOp> parseAnimatorConditionOp(std::string_view value) {
  if (value == "if") return components::AnimatorConditionOp::If;
  if (value == "if_not") return components::AnimatorConditionOp::IfNot;
  if (value == "equals") return components::AnimatorConditionOp::Equals;
  if (value == "not_equals") return components::AnimatorConditionOp::NotEquals;
  if (value == "greater") return components::AnimatorConditionOp::Greater;
  if (value == "greater_or_equal") return components::AnimatorConditionOp::GreaterOrEqual;
  if (value == "less") return components::AnimatorConditionOp::Less;
  if (value == "less_or_equal") return components::AnimatorConditionOp::LessOrEqual;
  return std::nullopt;
}

std::string animatorMotionTypeName(components::AnimatorMotionType type) {
  return type == components::AnimatorMotionType::BlendTree1D ? "blend_tree_1d" : "clip";
}

std::optional<components::AnimatorMotionType> parseAnimatorMotionType(std::string_view value) {
  if (value == "clip") return components::AnimatorMotionType::Clip;
  if (value == "blend_tree_1d") return components::AnimatorMotionType::BlendTree1D;
  return std::nullopt;
}

std::string sourceShapeName(components::ParticleSourceShape shape) {
  switch (shape) {
    case components::ParticleSourceShape::Box:
      return "box";
    case components::ParticleSourceShape::Sphere:
      return "sphere";
    case components::ParticleSourceShape::SphereSurface:
      return "sphere_surface";
    case components::ParticleSourceShape::Disc:
      return "disc";
    case components::ParticleSourceShape::Ring:
      return "ring";
    case components::ParticleSourceShape::Cylinder:
      return "cylinder";
    case components::ParticleSourceShape::Capsule:
      return "capsule";
    case components::ParticleSourceShape::Cone:
      return "cone";
    case components::ParticleSourceShape::Line:
      return "line";
    case components::ParticleSourceShape::Path:
      return "path";
    case components::ParticleSourceShape::TrailPath:
      return "trail_path";
    case components::ParticleSourceShape::MeshSurface:
      return "mesh_surface";
  }
  return "box";
}

bool readSourceShapeValue(std::string_view value, components::ParticleSourceShape& out) {
  if (value == "box") {
    out = components::ParticleSourceShape::Box;
    return true;
  }
  if (value == "sphere") {
    out = components::ParticleSourceShape::Sphere;
    return true;
  }
  if (value == "sphere_surface") {
    out = components::ParticleSourceShape::SphereSurface;
    return true;
  }
  if (value == "disc") {
    out = components::ParticleSourceShape::Disc;
    return true;
  }
  if (value == "ring") {
    out = components::ParticleSourceShape::Ring;
    return true;
  }
  if (value == "cylinder") {
    out = components::ParticleSourceShape::Cylinder;
    return true;
  }
  if (value == "capsule") {
    out = components::ParticleSourceShape::Capsule;
    return true;
  }
  if (value == "cone") {
    out = components::ParticleSourceShape::Cone;
    return true;
  }
  if (value == "line") {
    out = components::ParticleSourceShape::Line;
    return true;
  }
  if (value == "path") {
    out = components::ParticleSourceShape::Path;
    return true;
  }
  if (value == "trail_path") {
    out = components::ParticleSourceShape::TrailPath;
    return true;
  }
  if (value == "mesh_surface") {
    out = components::ParticleSourceShape::MeshSurface;
    return true;
  }
  return false;
}

bool readSourceShape(const Json& object,
                     std::string_view key,
                     components::ParticleSourceShape& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  return readSourceShapeValue(it->get<std::string>(), out);
}

std::string sourceSamplingName(components::ParticleSourceSamplingMode sampling) {
  switch (sampling) {
    case components::ParticleSourceSamplingMode::Random:
      return "random";
    case components::ParticleSourceSamplingMode::Sequential:
      return "sequential";
    case components::ParticleSourceSamplingMode::Vertices:
      return "vertices";
  }
  return "random";
}

bool readSourceSamplingValue(std::string_view value,
                             components::ParticleSourceSamplingMode& out) {
  if (value == "random") {
    out = components::ParticleSourceSamplingMode::Random;
    return true;
  }
  if (value == "sequential") {
    out = components::ParticleSourceSamplingMode::Sequential;
    return true;
  }
  if (value == "vertices") {
    out = components::ParticleSourceSamplingMode::Vertices;
    return true;
  }
  return false;
}

std::string sourceDistributionName(components::ParticleSourceDistribution distribution) {
  switch (distribution) {
    case components::ParticleSourceDistribution::Uniform:
      return "uniform";
    case components::ParticleSourceDistribution::Surface:
      return "surface";
    case components::ParticleSourceDistribution::Edge:
      return "edge";
  }
  return "uniform";
}

bool readSourceDistributionValue(std::string_view value,
                                 components::ParticleSourceDistribution& out) {
  if (value == "uniform") {
    out = components::ParticleSourceDistribution::Uniform;
    return true;
  }
  if (value == "surface") {
    out = components::ParticleSourceDistribution::Surface;
    return true;
  }
  if (value == "edge") {
    out = components::ParticleSourceDistribution::Edge;
    return true;
  }
  return false;
}

std::string blendModeName(components::ParticleBlendMode mode) {
  switch (mode) {
    case components::ParticleBlendMode::Additive:
      return "additive";
    case components::ParticleBlendMode::Alpha:
      return "alpha";
    case components::ParticleBlendMode::Distortion:
      return "distortion";
  }
  return "additive";
}

bool readBlendMode(const Json& object, components::ParticleBlendMode& out) {
  const auto it = object.find("blend_mode");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "additive") {
    out = components::ParticleBlendMode::Additive;
    return true;
  }
  if (value == "alpha") {
    out = components::ParticleBlendMode::Alpha;
    return true;
  }
  if (value == "distortion") {
    out = components::ParticleBlendMode::Distortion;
    return true;
  }
  return false;
}

std::string alignmentName(components::ParticleAlignment alignment) {
  switch (alignment) {
    case components::ParticleAlignment::Billboard:
      return "billboard";
    case components::ParticleAlignment::Ground:
      return "ground";
  }
  return "billboard";
}

bool readAlignment(const Json& object, components::ParticleAlignment& out) {
  const auto it = object.find("alignment");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "billboard") {
    out = components::ParticleAlignment::Billboard;
    return true;
  }
  if (value == "ground") {
    out = components::ParticleAlignment::Ground;
    return true;
  }
  return false;
}

std::string shadingModeName(components::ParticleShadingMode mode) {
  switch (mode) {
    case components::ParticleShadingMode::Standard:
      return "standard";
    case components::ParticleShadingMode::Shell:
      return "shell";
  }
  return "standard";
}

bool readShadingMode(const Json& object, components::ParticleShadingMode& out) {
  const auto it = object.find("shading_mode");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "standard") {
    out = components::ParticleShadingMode::Standard;
    return true;
  }
  if (value == "shell") {
    out = components::ParticleShadingMode::Shell;
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

Json serializeAnimator(const components::AnimatorComponent& component) {
  Json clip_refs = Json::array();
  for (const auto& clip : component.clips) {
    clip_refs.push_back(Json{{"name", clip.name}});
  }

  Json parameters = Json::array();
  for (const auto& parameter : component.state_machine.parameters) {
    parameters.push_back(Json{
        {"name", parameter.name},
        {"type", animatorParameterTypeName(parameter.type)},
        {"bool_value", parameter.bool_value},
        {"int_value", parameter.int_value},
        {"float_value", parameter.float_value},
      });
  }

  Json states = Json::array();
  for (const auto& state : component.state_machine.states) {
    Json transitions = Json::array();
    for (const auto& transition : state.transitions) {
      Json conditions = Json::array();
      for (const auto& condition : transition.conditions) {
        conditions.push_back(Json{
            {"parameter", condition.parameter},
            {"op", animatorConditionOpName(condition.op)},
            {"bool_value", condition.bool_value},
            {"int_value", condition.int_value},
            {"float_value", condition.float_value},
        });
      }
      transitions.push_back(Json{
          {"to_state_index", transition.to_state_index},
          {"duration_seconds", transition.duration_seconds},
          {"has_exit_time", transition.has_exit_time},
          {"exit_time_normalized", transition.exit_time_normalized},
          {"conditions", std::move(conditions)},
      });
    }

    Json children = Json::array();
    for (const auto& child : state.blend_tree.children) {
      children.push_back(Json{
          {"clip_index", child.clip_index},
          {"threshold", child.threshold},
          {"speed", child.speed},
      });
    }

    states.push_back(Json{
        {"name", state.name},
        {"motion_type", animatorMotionTypeName(state.motion_type)},
        {"clip_index", state.clip_index},
        {"speed", state.speed},
        {"loop", state.loop},
        {"blend_tree", Json{
            {"parameter", state.blend_tree.parameter},
            {"children", std::move(children)},
        }},
        {"transitions", std::move(transitions)},
    });
  }

  return Json{
      {"clip_references", std::move(clip_refs)},
      {"current_clip_index", component.current_clip_index},
      {"time_seconds", component.time_seconds},
      {"speed", component.speed},
      {"loop", component.loop},
      {"playing", component.playing},
      {"root_motion_mode", rootMotionModeName(component.root_motion_mode)},
      {"root_motion_node_index", component.root_motion_node_index},
      {"state_machine", Json{
          {"entry_state_index", component.state_machine.entry_state_index},
          {"parameters", std::move(parameters)},
          {"states", std::move(states)},
      }},
  };
}

std::optional<components::AnimatorComponent> deserializeAnimator(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::AnimatorComponent component{};
  uint32_t current_clip_index = 0;
  if (!readUint32(json, "current_clip_index", current_clip_index) ||
      !readFloat(json, "time_seconds", component.time_seconds) ||
      !readFloat(json, "speed", component.speed) ||
      !readBool(json, "loop", component.loop) ||
      !readBool(json, "playing", component.playing) ||
      !readRootMotionMode(json, component.root_motion_mode) ||
      !readUint32(json, "root_motion_node_index", component.root_motion_node_index)) {
    return std::nullopt;
  }
  component.current_clip_index = current_clip_index;

  if (const auto clips_it = json.find("clip_references");
      clips_it != json.end() && clips_it->is_array()) {
    for (const Json& clip_json : *clips_it) {
      if (!clip_json.is_object()) {
        return std::nullopt;
      }
      animation::AnimationClip clip{};
      if (!readString(clip_json, "name", clip.name)) {
        return std::nullopt;
      }
      component.clips.push_back(std::move(clip));
    }
  }

  const auto sm_it = json.find("state_machine");
  if (sm_it == json.end()) {
    return component;
  }
  if (!sm_it->is_object()) {
    return std::nullopt;
  }
  uint32_t entry_state_index = 0;
  if (!readUint32(*sm_it, "entry_state_index", entry_state_index)) {
    return std::nullopt;
  }
  component.state_machine.entry_state_index = entry_state_index;

  if (const auto params_it = sm_it->find("parameters");
      params_it != sm_it->end() && params_it->is_array()) {
    for (const Json& param_json : *params_it) {
      if (!param_json.is_object()) {
        return std::nullopt;
      }
      std::string type_name;
      components::AnimatorParameter parameter{};
      if (!readString(param_json, "name", parameter.name) ||
          !readString(param_json, "type", type_name)) {
        return std::nullopt;
      }
      const auto type = parseAnimatorParameterType(type_name);
      if (!type) {
        return std::nullopt;
      }
      parameter.type = *type;
      if (!readBool(param_json, "bool_value", parameter.bool_value) ||
          !readFloat(param_json, "float_value", parameter.float_value)) {
        return std::nullopt;
      }
      if (const auto int_it = param_json.find("int_value"); int_it != param_json.end()) {
        if (!int_it->is_number_integer()) {
          return std::nullopt;
        }
        parameter.int_value = int_it->get<int>();
      }
      component.state_machine.parameters.push_back(std::move(parameter));
    }
  }

  if (const auto states_it = sm_it->find("states");
      states_it != sm_it->end() && states_it->is_array()) {
    for (const Json& state_json : *states_it) {
      if (!state_json.is_object()) {
        return std::nullopt;
      }
      components::AnimatorState state{};
      std::string motion_type;
      uint32_t clip_index = animation::kInvalidAnimationIndex;
      if (!readString(state_json, "name", state.name) ||
          !readString(state_json, "motion_type", motion_type) ||
          !readUint32(state_json, "clip_index", clip_index) ||
          !readFloat(state_json, "speed", state.speed) ||
          !readBool(state_json, "loop", state.loop)) {
        return std::nullopt;
      }
      const auto parsed_motion_type = parseAnimatorMotionType(motion_type);
      if (!parsed_motion_type) {
        return std::nullopt;
      }
      state.motion_type = *parsed_motion_type;
      state.clip_index = clip_index;

      if (const auto blend_it = state_json.find("blend_tree");
          blend_it != state_json.end() && blend_it->is_object()) {
        if (!readString(*blend_it, "parameter", state.blend_tree.parameter)) {
          return std::nullopt;
        }
        if (const auto children_it = blend_it->find("children");
            children_it != blend_it->end() && children_it->is_array()) {
          for (const Json& child_json : *children_it) {
            components::AnimatorBlendTree1DChild child{};
            uint32_t child_clip = animation::kInvalidAnimationIndex;
            if (!readUint32(child_json, "clip_index", child_clip) ||
                !readFloat(child_json, "threshold", child.threshold) ||
                !readFloat(child_json, "speed", child.speed)) {
              return std::nullopt;
            }
            child.clip_index = child_clip;
            state.blend_tree.children.push_back(child);
          }
        }
      }

      if (const auto transitions_it = state_json.find("transitions");
          transitions_it != state_json.end() && transitions_it->is_array()) {
        for (const Json& transition_json : *transitions_it) {
          components::AnimatorTransition transition{};
          if (!readUint32(transition_json, "to_state_index", transition.to_state_index) ||
              !readFloat(transition_json, "duration_seconds", transition.duration_seconds) ||
              !readBool(transition_json, "has_exit_time", transition.has_exit_time) ||
              !readFloat(transition_json, "exit_time_normalized",
                         transition.exit_time_normalized)) {
            return std::nullopt;
          }
          if (const auto conditions_it = transition_json.find("conditions");
              conditions_it != transition_json.end() && conditions_it->is_array()) {
            for (const Json& condition_json : *conditions_it) {
              components::AnimatorCondition condition{};
              std::string op_name;
              if (!readString(condition_json, "parameter", condition.parameter) ||
                  !readString(condition_json, "op", op_name) ||
                  !readBool(condition_json, "bool_value", condition.bool_value) ||
                  !readFloat(condition_json, "float_value", condition.float_value)) {
                return std::nullopt;
              }
              const auto op = parseAnimatorConditionOp(op_name);
              if (!op) {
                return std::nullopt;
              }
              condition.op = *op;
              if (const auto int_it = condition_json.find("int_value");
                  int_it != condition_json.end()) {
                if (!int_it->is_number_integer()) {
                  return std::nullopt;
                }
                condition.int_value = int_it->get<int>();
              }
              transition.conditions.push_back(std::move(condition));
            }
          }
          state.transitions.push_back(std::move(transition));
        }
      }
      component.state_machine.states.push_back(std::move(state));
    }
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
  if (component.texture_key.has_value()) {
    json["texture_key"] = *component.texture_key;
  }
  if (component.source_shape.has_value()) {
    json["source_shape"] = sourceShapeName(*component.source_shape);
  }
  if (component.source_box_extents.has_value()) {
    json["source_box_extents"] = toJson(*component.source_box_extents);
  }
  if (component.source_dimensions.has_value()) {
    json["source_dimensions"] = toJson(*component.source_dimensions);
  }
  if (component.source_radius_min.has_value()) {
    json["source_radius_min"] = *component.source_radius_min;
  }
  if (component.source_radius_max.has_value()) {
    json["source_radius_max"] = *component.source_radius_max;
  }
  if (component.source_inner_radius.has_value()) {
    json["source_inner_radius"] = *component.source_inner_radius;
  }
  if (component.source_outer_radius.has_value()) {
    json["source_outer_radius"] = *component.source_outer_radius;
  }
  if (component.source_height.has_value()) {
    json["source_height"] = *component.source_height;
  }
  if (component.source_angle.has_value()) {
    json["source_angle"] = *component.source_angle;
  }
  if (component.source_path_points.has_value()) {
    Json points = Json::array();
    for (const math::Vec3& point : *component.source_path_points) {
      points.push_back(toJson(point));
    }
    json["source_path_points"] = std::move(points);
  }
  if (component.source_closed_loop.has_value()) {
    json["source_closed_loop"] = *component.source_closed_loop;
  }
  if (component.source_sampling.has_value()) {
    json["source_sampling"] = sourceSamplingName(*component.source_sampling);
  }
  if (component.source_jitter_radius.has_value()) {
    json["source_jitter_radius"] = *component.source_jitter_radius;
  }
  if (component.source_mesh_key.has_value()) {
    json["source_mesh_key"] = *component.source_mesh_key;
  }
  if (component.source_mesh_path.has_value()) {
    json["source_mesh_path"] = *component.source_mesh_path;
  }
  if (component.source_distribution.has_value()) {
    json["source_distribution"] = sourceDistributionName(*component.source_distribution);
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
  if (const auto it = json.find("texture_key"); it != json.end()) {
    if (!it->is_string()) {
      return std::nullopt;
    }
    component.texture_key = it->get<std::string>();
  }
  if (const auto it = json.find("source_shape"); it != json.end()) {
    if (!it->is_string()) {
      return std::nullopt;
    }
    components::ParticleSourceShape shape{};
    if (!readSourceShapeValue(it->get<std::string>(), shape)) {
      return std::nullopt;
    }
    component.source_shape = shape;
  }
  auto readOptionalVec3Field = [&](std::string_view key, std::optional<math::Vec3>& out) {
    const auto it = json.find(key);
    if (it == json.end()) {
      return true;
    }
    math::Vec3 value{};
    if (!readVec3Value(*it, value)) {
      return false;
    }
    out = value;
    return true;
  };
  auto readOptionalFloatField = [&](std::string_view key, std::optional<float>& out) {
    const auto it = json.find(key);
    if (it == json.end()) {
      return true;
    }
    float value = 0.0f;
    if (!readFloatValue(*it, value)) {
      return false;
    }
    out = value;
    return true;
  };
  if (!readOptionalVec3Field("source_box_extents", component.source_box_extents) ||
      !readOptionalVec3Field("source_dimensions", component.source_dimensions) ||
      !readOptionalFloatField("source_radius_min", component.source_radius_min) ||
      !readOptionalFloatField("source_radius_max", component.source_radius_max) ||
      !readOptionalFloatField("source_inner_radius", component.source_inner_radius) ||
      !readOptionalFloatField("source_outer_radius", component.source_outer_radius) ||
      !readOptionalFloatField("source_height", component.source_height) ||
      !readOptionalFloatField("source_angle", component.source_angle) ||
      !readOptionalFloatField("source_jitter_radius", component.source_jitter_radius)) {
    return std::nullopt;
  }
  if (const auto it = json.find("source_path_points"); it != json.end()) {
    if (!it->is_array()) {
      return std::nullopt;
    }
    std::vector<math::Vec3> points;
    points.reserve(it->size());
    for (const Json& point_json : *it) {
      math::Vec3 point{};
      if (!readVec3Value(point_json, point)) {
        return std::nullopt;
      }
      points.push_back(point);
    }
    component.source_path_points = std::move(points);
  }
  if (const auto it = json.find("source_closed_loop"); it != json.end()) {
    if (!it->is_boolean()) {
      return std::nullopt;
    }
    component.source_closed_loop = it->get<bool>();
  }
  if (const auto it = json.find("source_sampling"); it != json.end()) {
    if (!it->is_string()) {
      return std::nullopt;
    }
    components::ParticleSourceSamplingMode sampling{};
    if (!readSourceSamplingValue(it->get<std::string>(), sampling)) {
      return std::nullopt;
    }
    component.source_sampling = sampling;
  }
  if (const auto it = json.find("source_mesh_key"); it != json.end()) {
    if (!it->is_string()) {
      return std::nullopt;
    }
    component.source_mesh_key = it->get<std::string>();
  }
  if (const auto it = json.find("source_mesh_path"); it != json.end()) {
    if (!it->is_string()) {
      return std::nullopt;
    }
    component.source_mesh_path = it->get<std::string>();
  }
  if (const auto it = json.find("source_distribution"); it != json.end()) {
    if (!it->is_string()) {
      return std::nullopt;
    }
    components::ParticleSourceDistribution distribution{};
    if (!readSourceDistributionValue(it->get<std::string>(), distribution)) {
      return std::nullopt;
    }
    component.source_distribution = distribution;
  }
  return component;
}

Json serializeParticleEmitter(const components::ParticleEmitterComponent& component) {
  return Json{
      {"enabled", component.enabled},
      {"playing", component.playing},
      {"start_delay", component.start_delay},
      {"texture_key", component.texture_key},
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
      !readFloat(json, "start_delay", component.start_delay) ||
      !readString(json, "texture_key", component.texture_key)) {
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

float computeVolumeDensity(float radius, float center_opacity) {
  if (center_opacity >= 0.9999f) {
    return 1000.0f / std::max(radius * 2.0f, 1.0e-4f);
  }
  const float clamped_opacity = std::clamp(center_opacity, 0.001f, 0.999f);
  const float center_transmittance = 1.0f - clamped_opacity;
  return -std::log(center_transmittance) / std::max(radius * 2.0f, 1.0e-4f);
}

std::string volumetricShapeName(components::VolumetricShape shape) {
  switch (shape) {
    case components::VolumetricShape::Sphere:
      return "sphere";
    case components::VolumetricShape::Capsule:
      return "capsule";
  }
  return "sphere";
}

bool readVolumetricShape(const Json& object, components::VolumetricShape& out) {
  const auto it = object.find("shape");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "sphere") {
    out = components::VolumetricShape::Sphere;
    return true;
  }
  if (value == "capsule") {
    out = components::VolumetricShape::Capsule;
    return true;
  }
  return false;
}

Json serializeVolumetric(const components::VolumetricComponent& component) {
  return Json{
      {"shape", volumetricShapeName(component.shape)},
      {"color", toJson(component.color)},
      {"emissive_color", toJson(component.emissive_color)},
      {"density", component.density},
      {"center_opacity", component.center_opacity},
      {"scattering", component.scattering},
      {"anisotropy", component.anisotropy},
      {"absorption", component.absorption},
      {"distortion_strength", component.distortion_strength},
      {"noise_strength", component.noise_strength},
      {"radius", component.radius},
      {"capsule_half_length", component.capsule_half_length},
      {"scale_with_transform", component.scale_with_transform},
      {"visible", component.visible},
      {"overlay_depth", component.overlay_depth},
  };
}

std::optional<components::VolumetricComponent> deserializeVolumetric(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::VolumetricComponent component{};
  const bool has_density = json.find("density") != json.end();
  if (!readVolumetricShape(json, component.shape) ||
      !readColor(json, "color", component.color) ||
      !readColor(json, "emissive_color", component.emissive_color) ||
      !readFloat(json, "density", component.density) ||
      !readFloat(json, "center_opacity", component.center_opacity) ||
      !readFloat(json, "scattering", component.scattering) ||
      !readFloat(json, "anisotropy", component.anisotropy) ||
      !readFloat(json, "absorption", component.absorption) ||
      !readFloat(json, "distortion_strength", component.distortion_strength) ||
      !readFloat(json, "noise_strength", component.noise_strength) ||
      !readFloat(json, "radius", component.radius) ||
      !readFloat(json, "capsule_half_length", component.capsule_half_length) ||
      !readBool(json, "scale_with_transform", component.scale_with_transform) ||
      !readBool(json, "visible", component.visible) ||
      !readFloat(json, "overlay_depth", component.overlay_depth)) {
    return std::nullopt;
  }
  if (component.radius <= 0.0f ||
      component.capsule_half_length < 0.0f ||
      component.density < 0.0f ||
      component.scattering < 0.0f ||
      component.absorption < 0.0f ||
      component.overlay_depth <= 0.0f) {
    return std::nullopt;
  }
  if (!has_density) {
    component.density = computeVolumeDensity(component.radius, component.center_opacity);
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
  registerComponent<components::AnimatorComponent>(
      registry, "AnimatorComponent", serializeAnimator, deserializeAnimator);
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
  registerComponent<components::VolumetricComponent>(
      registry, "VolumetricComponent", serializeVolumetric, deserializeVolumetric);
}

void ensureBuiltinComponentSerializers() {
  ComponentSerializerRegistry& registry = componentSerializerRegistry();
  if (registry.find("TransformComponent") != nullptr) {
    return;
  }
  registerBuiltinComponentSerializers(registry);
}

}  // namespace karma::prefabs
