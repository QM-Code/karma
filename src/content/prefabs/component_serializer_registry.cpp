#include "karma/prefabs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "component_serializer_rendering.h"
#include "component_serializer_utilities.h"
#include "karma/components.h"

namespace karma::prefabs {

namespace {

using Json = nlohmann::json;
using component_serializer_detail::isPortableRelativePath;
using component_serializer_detail::readBool;
using component_serializer_detail::readEntityReference;
using component_serializer_detail::readFloat;
using component_serializer_detail::readFloatValue;
using component_serializer_detail::readQuat;
using component_serializer_detail::readQuatValue;
using component_serializer_detail::resolveEntityReferenceValue;
using component_serializer_detail::readString;
using component_serializer_detail::readUint32;
using component_serializer_detail::readUint64;
using component_serializer_detail::readVec3;
using component_serializer_detail::readVec3Value;
using component_serializer_detail::registerComponent;
using component_serializer_detail::registerContextualComponent;
using component_serializer_detail::serializeEntityReference;
using component_serializer_detail::toJson;

Json toJson(const math::Color& value) {
  return Json::array({value.r, value.g, value.b, value.a});
}

bool readStringVector(const Json& object, std::string_view key, std::vector<std::string>& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_array()) {
    return false;
  }
  out.clear();
  out.reserve(it->size());
  for (const Json& entry : *it) {
    if (!entry.is_string()) {
      return false;
    }
    std::string value = entry.get<std::string>();
    if (value.empty()) {
      return false;
    }
    out.push_back(std::move(value));
  }
  return true;
}

bool readInt32(const Json& object, std::string_view key, int32_t& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_number_integer()) {
    return false;
  }
  if (it->is_number_unsigned()) {
    const uint64_t value = it->get<uint64_t>();
    if (value > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
      return false;
    }
    out = static_cast<int32_t>(value);
    return true;
  }
  const int64_t value = it->get<int64_t>();
  if (value < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
      value > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
    return false;
  }
  out = static_cast<int32_t>(value);
  return true;
}

bool readUint16(const Json& object, std::string_view key, uint16_t& out) {
  uint32_t value = out;
  if (!readUint32(object, key, value) || value > UINT16_MAX) {
    return false;
  }
  out = static_cast<uint16_t>(value);
  return true;
}

bool readUint8(const Json& object, std::string_view key, uint8_t& out) {
  uint32_t value = out;
  if (!readUint32(object, key, value) || value > UINT8_MAX) {
    return false;
  }
  out = static_cast<uint8_t>(value);
  return true;
}

bool readSize(const Json& object, std::string_view key, size_t& out) {
  uint64_t value = static_cast<uint64_t>(out);
  if (!readUint64(object, key, value) ||
      value > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    return false;
  }
  out = static_cast<size_t>(value);
  return true;
}

bool readFloatVector(const Json& object,
                     std::string_view key,
                     std::vector<float>& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_array()) {
    return false;
  }
  std::vector<float> values;
  values.reserve(it->size());
  for (const Json& entry : *it) {
    float value = 0.0f;
    if (!readFloatValue(entry, value)) {
      return false;
    }
    values.push_back(value);
  }
  out = std::move(values);
  return true;
}

Json floatVectorJson(const std::vector<float>& values) {
  Json out = Json::array();
  for (float value : values) {
    out.push_back(value);
  }
  return out;
}

Json vec3VectorJson(const std::vector<math::Vec3>& values) {
  Json out = Json::array();
  for (const math::Vec3& value : values) {
    out.push_back(toJson(value));
  }
  return out;
}

bool readVec3Vector(const Json& object,
                    std::string_view key,
                    std::vector<math::Vec3>& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_array()) {
    return false;
  }
  std::vector<math::Vec3> values;
  values.reserve(it->size());
  for (const Json& entry : *it) {
    math::Vec3 value{};
    if (!readVec3Value(entry, value)) {
      return false;
    }
    values.push_back(value);
  }
  out = std::move(values);
  return true;
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

std::string lightBakeModeName(components::LightComponent::BakeMode mode) {
  switch (mode) {
    case components::LightComponent::BakeMode::Realtime:
      return "realtime";
    case components::LightComponent::BakeMode::Mixed:
      return "mixed";
    case components::LightComponent::BakeMode::Baked:
      return "baked";
  }
  return "realtime";
}

bool readLightBakeMode(const Json& object,
                       components::LightComponent::BakeMode& out) {
  const auto it = object.find("bake_mode");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "realtime") {
    out = components::LightComponent::BakeMode::Realtime;
    return true;
  }
  if (value == "mixed") {
    out = components::LightComponent::BakeMode::Mixed;
    return true;
  }
  if (value == "baked") {
    out = components::LightComponent::BakeMode::Baked;
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
      {"position", toJson(component.localPosition())},
      {"rotation", toJson(component.localRotation())},
      {"scale", toJson(component.localScale())},
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
      world::AnimationClip clip{};
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
      uint32_t clip_index = world::kInvalidAnimationIndex;
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
            uint32_t child_clip = world::kInvalidAnimationIndex;
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
      {"bake_mode", lightBakeModeName(component.bake_mode)},
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
      !readLightBakeMode(json, component.bake_mode) ||
      !readColor(json, "color", component.color) ||
      !readFloat(json, "intensity", component.intensity) ||
      !readFloat(json, "range", component.range) ||
      !readFloat(json, "inner_cone_degrees", component.inner_cone_degrees) ||
      !readFloat(json, "outer_cone_degrees", component.outer_cone_degrees) ||
      !readBool(json, "casts_shadows", component.casts_shadows) ||
      !readFloat(json, "shadow_extent", component.shadow_extent)) {
    return std::nullopt;
  }
  if (component.intensity < 0.0f || component.range < 0.0f ||
      component.shadow_extent < 0.0f) {
    return std::nullopt;
  }
  if (component.type == components::LightComponent::Type::Spot &&
      (component.inner_cone_degrees < 0.0f ||
       component.outer_cone_degrees > 179.0f ||
       component.inner_cone_degrees > component.outer_cone_degrees)) {
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

Json serializeRenderTags(const components::RenderTagsComponent& component) {
  return Json{
      {"tags", component.tags},
  };
}

std::optional<components::RenderTagsComponent> deserializeRenderTags(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::RenderTagsComponent component{};
  if (!readStringVector(json, "tags", component.tags)) {
    return std::nullopt;
  }
  return component;
}

std::string terrainSourceName(components::TerrainSourceType source) {
  switch (source) {
    case components::TerrainSourceType::Procedural:
      return "procedural";
    case components::TerrainSourceType::ImageTileDirectory:
      return "image_tile_directory";
    case components::TerrainSourceType::SingleImage:
      return "single_image";
  }
  return "procedural";
}

bool readTerrainSource(const Json& object, components::TerrainSourceType& out) {
  const auto it = object.find("source");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "procedural") {
    out = components::TerrainSourceType::Procedural;
    return true;
  }
  if (value == "image_tile_directory") {
    out = components::TerrainSourceType::ImageTileDirectory;
    return true;
  }
  if (value == "single_image" || value == "fixed_image") {
    out = components::TerrainSourceType::SingleImage;
    return true;
  }
  return false;
}

std::string terrainHeightFormatName(components::TerrainHeightFormat format) {
  switch (format) {
    case components::TerrainHeightFormat::Auto:
      return "auto";
    case components::TerrainHeightFormat::ImageFile:
      return "image_file";
    case components::TerrainHeightFormat::Raw16Unsigned:
      return "raw16_unsigned";
    case components::TerrainHeightFormat::R32Float:
      return "r32_float";
  }
  return "auto";
}

bool readTerrainHeightFormat(const Json& object,
                             std::string_view key,
                             components::TerrainHeightFormat& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "auto") {
    out = components::TerrainHeightFormat::Auto;
    return true;
  }
  if (value == "image_file" || value == "image") {
    out = components::TerrainHeightFormat::ImageFile;
    return true;
  }
  if (value == "raw16_unsigned" || value == "raw16" || value == "raw") {
    out = components::TerrainHeightFormat::Raw16Unsigned;
    return true;
  }
  if (value == "r32_float" || value == "r32") {
    out = components::TerrainHeightFormat::R32Float;
    return true;
  }
  return false;
}

std::string terrainDataMapKindName(components::TerrainDataMapKind kind) {
  switch (kind) {
    case components::TerrainDataMapKind::Custom:
      return "custom";
    case components::TerrainDataMapKind::Flow:
      return "flow";
    case components::TerrainDataMapKind::Wear:
      return "wear";
    case components::TerrainDataMapKind::Deposit:
      return "deposit";
    case components::TerrainDataMapKind::Slope:
      return "slope";
    case components::TerrainDataMapKind::Curvature:
      return "curvature";
  }
  return "custom";
}

bool readTerrainDataMapKind(const Json& object, components::TerrainDataMapKind& out) {
  const auto it = object.find("kind");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "custom") {
    out = components::TerrainDataMapKind::Custom;
    return true;
  }
  if (value == "flow") {
    out = components::TerrainDataMapKind::Flow;
    return true;
  }
  if (value == "wear") {
    out = components::TerrainDataMapKind::Wear;
    return true;
  }
  if (value == "deposit") {
    out = components::TerrainDataMapKind::Deposit;
    return true;
  }
  if (value == "slope") {
    out = components::TerrainDataMapKind::Slope;
    return true;
  }
  if (value == "curvature") {
    out = components::TerrainDataMapKind::Curvature;
    return true;
  }
  return false;
}

Json serializeTerrainMaterialLayers(
    const std::vector<components::TerrainMaterialLayer>& layers) {
  Json array = Json::array();
  for (const auto& layer : layers) {
    array.push_back(Json{
        {"name", layer.name},
        {"material_key", layer.material_key},
        {"albedo_image", layer.albedo_image.generic_string()},
        {"normal_image", layer.normal_image.generic_string()},
        {"roughness_image", layer.roughness_image.generic_string()},
        {"uv_scale", layer.uv_scale},
        {"enabled", layer.enabled},
    });
  }
  return array;
}

bool readTerrainMaterialLayers(const Json& object,
                               std::vector<components::TerrainMaterialLayer>& out) {
  const auto it = object.find("material_layers");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_array()) {
    return false;
  }
  std::vector<components::TerrainMaterialLayer> layers;
  layers.reserve(it->size());
  for (const Json& layer_json : *it) {
    if (!layer_json.is_object()) {
      return false;
    }
    components::TerrainMaterialLayer layer{};
    std::string material_key;
    std::string albedo_image;
    std::string normal_image;
    std::string roughness_image;
    if (!readString(layer_json, "name", layer.name) ||
        !readString(layer_json, "material_key", material_key) ||
        !readString(layer_json, "albedo_image", albedo_image) ||
        !readString(layer_json, "normal_image", normal_image) ||
        !readString(layer_json, "roughness_image", roughness_image) ||
        !readFloat(layer_json, "uv_scale", layer.uv_scale) ||
        !readBool(layer_json, "enabled", layer.enabled)) {
      return false;
    }
    layer.material_key = std::move(material_key);
    layer.albedo_image = std::move(albedo_image);
    layer.normal_image = std::move(normal_image);
    layer.roughness_image = std::move(roughness_image);
    layers.push_back(std::move(layer));
  }
  out = std::move(layers);
  return true;
}

Json serializeTerrainDataMaps(
    const std::vector<components::TerrainDataMapBinding>& data_maps) {
  Json array = Json::array();
  for (const auto& map : data_maps) {
    array.push_back(Json{
        {"name", map.name},
        {"kind", terrainDataMapKindName(map.kind)},
        {"image", map.image.generic_string()},
        {"pattern", map.pattern},
        {"format", terrainHeightFormatName(map.format)},
        {"raw_width", map.raw_width},
        {"raw_height", map.raw_height},
        {"channel", map.channel},
        {"enabled", map.enabled},
    });
  }
  return array;
}

bool readTerrainDataMaps(const Json& object,
                         std::vector<components::TerrainDataMapBinding>& out) {
  const auto it = object.find("data_maps");
  if (it == object.end()) {
    return true;
  }
  if (!it->is_array()) {
    return false;
  }
  std::vector<components::TerrainDataMapBinding> maps;
  maps.reserve(it->size());
  for (const Json& map_json : *it) {
    if (!map_json.is_object()) {
      return false;
    }
    components::TerrainDataMapBinding map{};
    std::string image;
    if (!readString(map_json, "name", map.name) ||
        !readTerrainDataMapKind(map_json, map.kind) ||
        !readString(map_json, "image", image) ||
        !readString(map_json, "pattern", map.pattern) ||
        !readTerrainHeightFormat(map_json, "format", map.format) ||
        !readUint32(map_json, "raw_width", map.raw_width) ||
        !readUint32(map_json, "raw_height", map.raw_height) ||
        !readUint32(map_json, "channel", map.channel) ||
        !readBool(map_json, "enabled", map.enabled)) {
      return false;
    }
    map.image = std::move(image);
    maps.push_back(std::move(map));
  }
  out = std::move(maps);
  return true;
}

Json serializeTerrain(const components::TerrainComponent& component) {
  return Json{
      {"source", terrainSourceName(component.source)},
      {"tile_directory", component.tile_directory.generic_string()},
      {"height_pattern", component.height_pattern},
      {"color_pattern", component.color_pattern},
      {"control_pattern", component.control_pattern},
      {"height_image", component.height_image.generic_string()},
      {"heatmap_image", component.heatmap_image.generic_string()},
      {"color_image", component.color_image.generic_string()},
      {"control_image", component.control_image.generic_string()},
      {"height_format", terrainHeightFormatName(component.height_format)},
      {"raw_width", component.raw_width},
      {"raw_height", component.raw_height},
      {"raw_little_endian", component.raw_little_endian},
      {"flip_y", component.flip_y},
      {"height_value_min", component.height_value_min},
      {"height_value_max", component.height_value_max},
      {"tile_index_base", component.tile_index_base},
      {"source_revision", component.source_revision},
      {"material_layers", serializeTerrainMaterialLayers(component.material_layers)},
      {"data_maps", serializeTerrainDataMaps(component.data_maps)},
      {"terrain_size", component.terrain_size},
      {"tile_size", component.tile_size},
      {"tile_resolution", component.tile_resolution},
      {"origin_tile_x", component.origin_tile_x},
      {"origin_tile_z", component.origin_tile_z},
      {"height_scale", component.height_scale},
      {"height_offset", component.height_offset},
      {"view_distance", component.view_distance},
      {"base_patch_size", component.base_patch_size},
      {"tessellation_factor", component.tessellation_factor},
      {"target_tessellated_edge_size", component.target_tessellated_edge_size},
      {"layer", component.layer},
      {"visible", component.visible},
      {"cpu_fallback_enabled", component.cpu_fallback_enabled},
  };
}

std::optional<components::TerrainComponent> deserializeTerrain(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::TerrainComponent component{};
  std::string tile_directory;
  std::string height_image;
  std::string heatmap_image;
  std::string color_image;
  std::string control_image;
  if (!readTerrainSource(json, component.source) ||
      !readString(json, "tile_directory", tile_directory) ||
      !readString(json, "height_pattern", component.height_pattern) ||
      !readString(json, "color_pattern", component.color_pattern) ||
      !readString(json, "control_pattern", component.control_pattern) ||
      !readString(json, "height_image", height_image) ||
      !readString(json, "heatmap_image", heatmap_image) ||
      !readString(json, "color_image", color_image) ||
      !readString(json, "control_image", control_image) ||
      !readTerrainHeightFormat(json, "height_format", component.height_format) ||
      !readUint32(json, "raw_width", component.raw_width) ||
      !readUint32(json, "raw_height", component.raw_height) ||
      !readBool(json, "raw_little_endian", component.raw_little_endian) ||
      !readBool(json, "flip_y", component.flip_y) ||
      !readFloat(json, "height_value_min", component.height_value_min) ||
      !readFloat(json, "height_value_max", component.height_value_max) ||
      !readInt32(json, "tile_index_base", component.tile_index_base) ||
      !readUint64(json, "source_revision", component.source_revision) ||
      !readTerrainMaterialLayers(json, component.material_layers) ||
      !readTerrainDataMaps(json, component.data_maps) ||
      !readFloat(json, "terrain_size", component.terrain_size) ||
      !readFloat(json, "tile_size", component.tile_size) ||
      !readUint32(json, "tile_resolution", component.tile_resolution) ||
      !readInt32(json, "origin_tile_x", component.origin_tile_x) ||
      !readInt32(json, "origin_tile_z", component.origin_tile_z) ||
      !readFloat(json, "height_scale", component.height_scale) ||
      !readFloat(json, "height_offset", component.height_offset) ||
      !readFloat(json, "view_distance", component.view_distance) ||
      !readUint32(json, "base_patch_size", component.base_patch_size) ||
      !readFloat(json, "tessellation_factor", component.tessellation_factor) ||
      !readFloat(json, "target_tessellated_edge_size",
                 component.target_tessellated_edge_size) ||
      !readUint32(json, "layer", component.layer) ||
      !readBool(json, "visible", component.visible) ||
      !readBool(json, "cpu_fallback_enabled", component.cpu_fallback_enabled)) {
    return std::nullopt;
  }
  const auto portable_or_empty = [](std::string_view value) {
    return value.empty() || isPortableRelativePath(value);
  };
  if (!portable_or_empty(tile_directory) ||
      !portable_or_empty(component.height_pattern) ||
      !portable_or_empty(component.color_pattern) ||
      !portable_or_empty(component.control_pattern) ||
      !portable_or_empty(height_image) ||
      !portable_or_empty(heatmap_image) ||
      !portable_or_empty(color_image) ||
      !portable_or_empty(control_image)) {
    return std::nullopt;
  }
  component.tile_directory = std::move(tile_directory);
  component.height_image = std::move(height_image);
  component.heatmap_image = std::move(heatmap_image);
  component.color_image = std::move(color_image);
  component.control_image = std::move(control_image);
  const bool is_single_image =
      component.source == components::TerrainSourceType::SingleImage;
  const bool is_tile_directory =
      component.source == components::TerrainSourceType::ImageTileDirectory;
  if (component.tile_resolution < 2u ||
      component.tile_resolution > rendering::kMaxTerrainTileResolution ||
      component.view_distance < 0.0f ||
      component.base_patch_size == 0u ||
      component.tessellation_factor < 1.0f ||
      component.target_tessellated_edge_size <= 0.0f ||
      component.height_value_max <= component.height_value_min) {
    return std::nullopt;
  }
  if (component.height_format == components::TerrainHeightFormat::Raw16Unsigned ||
      component.height_format == components::TerrainHeightFormat::R32Float) {
    if (component.raw_width == 0u || component.raw_height == 0u ||
        component.raw_width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        component.raw_height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
      return std::nullopt;
    }
  }
  for (const auto& layer : component.material_layers) {
    if (!portable_or_empty(layer.albedo_image.generic_string()) ||
        !portable_or_empty(layer.normal_image.generic_string()) ||
        !portable_or_empty(layer.roughness_image.generic_string()) ||
        layer.uv_scale <= 0.0f ||
        (layer.enabled &&
         layer.material_key.empty() &&
         layer.albedo_image.empty())) {
      return std::nullopt;
    }
  }
  for (const auto& map : component.data_maps) {
    if (!portable_or_empty(map.image.generic_string()) ||
        !portable_or_empty(map.pattern) || map.channel > 3u ||
        (map.enabled && map.image.empty() && map.pattern.empty())) {
      return std::nullopt;
    }
    if (map.format == components::TerrainHeightFormat::Raw16Unsigned ||
        map.format == components::TerrainHeightFormat::R32Float) {
      const uint32_t width = map.raw_width != 0u ? map.raw_width : component.raw_width;
      const uint32_t height = map.raw_height != 0u ? map.raw_height : component.raw_height;
      if (width == 0u || height == 0u ||
          width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
          height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
      }
    }
  }
  if (component.material_layers.size() > 4u) {
    return std::nullopt;
  }
  if (is_single_image) {
    if (component.terrain_size <= 0.0f ||
        (component.height_image.empty() && component.heatmap_image.empty())) {
      return std::nullopt;
    }
  } else if (component.tile_size <= 0.0f) {
    return std::nullopt;
  }
  if (is_tile_directory && component.height_pattern.empty()) {
    return std::nullopt;
  }
  return component;
}

const char* rigidbodyMotionTypeName(components::RigidbodyMotionType type) {
  switch (type) {
    case components::RigidbodyMotionType::Dynamic:
      return "dynamic";
    case components::RigidbodyMotionType::Kinematic:
      return "kinematic";
    case components::RigidbodyMotionType::Static:
      return "static";
  }
  return "dynamic";
}

bool readRigidbodyMotionType(const Json& object,
                             std::string_view key,
                             components::RigidbodyMotionType& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "dynamic") {
    out = components::RigidbodyMotionType::Dynamic;
    return true;
  }
  if (value == "kinematic") {
    out = components::RigidbodyMotionType::Kinematic;
    return true;
  }
  if (value == "static") {
    out = components::RigidbodyMotionType::Static;
    return true;
  }
  return false;
}

const char* rigidbodyMotionQualityName(components::RigidbodyMotionQuality quality) {
  switch (quality) {
    case components::RigidbodyMotionQuality::Discrete:
      return "discrete";
    case components::RigidbodyMotionQuality::LinearCast:
      return "linear_cast";
  }
  return "discrete";
}

bool readRigidbodyMotionQuality(const Json& object,
                                std::string_view key,
                                components::RigidbodyMotionQuality& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "discrete") {
    out = components::RigidbodyMotionQuality::Discrete;
    return true;
  }
  if (value == "linear_cast") {
    out = components::RigidbodyMotionQuality::LinearCast;
    return true;
  }
  return false;
}

Json serializeRigidbody(const components::RigidbodyComponent& component) {
  return Json{
      {"motion_type", rigidbodyMotionTypeName(component.motion_type)},
      {"motion_quality", rigidbodyMotionQualityName(component.motion_quality)},
      {"allowed_dofs", component.allowed_dofs},
      {"mass", component.mass},
      {"velocity", toJson(component.velocity)},
      {"angular_velocity", toJson(component.angular_velocity)},
      {"is_kinematic", component.is_kinematic},
      {"use_gravity", component.use_gravity},
      {"is_trigger", component.is_trigger},
      {"gravity_factor", component.gravity_factor},
      {"linear_damping", component.linear_damping},
      {"angular_damping", component.angular_damping},
      {"max_linear_velocity", component.max_linear_velocity},
      {"max_angular_velocity", component.max_angular_velocity},
      {"inertia_multiplier", component.inertia_multiplier},
      {"velocity_solver_steps", component.velocity_solver_steps},
      {"position_solver_steps", component.position_solver_steps},
      {"allow_sleeping", component.allow_sleeping},
      {"allow_dynamic_or_kinematic", component.allow_dynamic_or_kinematic},
      {"collide_kinematic_vs_non_dynamic",
       component.collide_kinematic_vs_non_dynamic},
      {"use_manifold_reduction", component.use_manifold_reduction},
      {"apply_gyroscopic_force", component.apply_gyroscopic_force},
      {"enhanced_internal_edge_removal",
       component.enhanced_internal_edge_removal},
  };
}

std::optional<components::RigidbodyComponent> deserializeRigidbody(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::RigidbodyComponent component{};
  uint32_t allowed_dofs = component.allowed_dofs;
  if (!readRigidbodyMotionType(json, "motion_type", component.motion_type) ||
      !readRigidbodyMotionQuality(json, "motion_quality", component.motion_quality) ||
      !readUint32(json, "allowed_dofs", allowed_dofs) ||
      !readFloat(json, "mass", component.mass) ||
      !readVec3(json, "velocity", component.velocity) ||
      !readVec3(json, "angular_velocity", component.angular_velocity) ||
      !readBool(json, "is_kinematic", component.is_kinematic) ||
      !readBool(json, "use_gravity", component.use_gravity) ||
      !readBool(json, "is_trigger", component.is_trigger) ||
      !readFloat(json, "gravity_factor", component.gravity_factor) ||
      !readFloat(json, "linear_damping", component.linear_damping) ||
      !readFloat(json, "angular_damping", component.angular_damping) ||
      !readFloat(json, "max_linear_velocity", component.max_linear_velocity) ||
      !readFloat(json, "max_angular_velocity", component.max_angular_velocity) ||
      !readFloat(json, "inertia_multiplier", component.inertia_multiplier) ||
      !readUint32(json, "velocity_solver_steps", component.velocity_solver_steps) ||
      !readUint32(json, "position_solver_steps", component.position_solver_steps) ||
      !readBool(json, "allow_sleeping", component.allow_sleeping) ||
      !readBool(json,
                "allow_dynamic_or_kinematic",
                component.allow_dynamic_or_kinematic) ||
      !readBool(json,
                "collide_kinematic_vs_non_dynamic",
                component.collide_kinematic_vs_non_dynamic) ||
      !readBool(json, "use_manifold_reduction", component.use_manifold_reduction) ||
      !readBool(json, "apply_gyroscopic_force", component.apply_gyroscopic_force) ||
      !readBool(json,
                "enhanced_internal_edge_removal",
                component.enhanced_internal_edge_removal)) {
    return std::nullopt;
  }
  if (allowed_dofs > components::RigidbodyDofAll || component.mass < 0.0f ||
      component.linear_damping < 0.0f || component.angular_damping < 0.0f ||
      component.max_linear_velocity <= 0.0f ||
      component.max_angular_velocity <= 0.0f ||
      component.inertia_multiplier <= 0.0f) {
    return std::nullopt;
  }
  component.allowed_dofs = static_cast<uint8_t>(allowed_dofs);
  return component;
}

Json serializePhysicsMaterial(const components::PhysicsMaterialComponent& component) {
  return Json{
      {"friction", component.friction},
      {"restitution", component.restitution},
  };
}

std::optional<components::PhysicsMaterialComponent> deserializePhysicsMaterial(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::PhysicsMaterialComponent component{};
  if (!readFloat(json, "friction", component.friction) ||
      !readFloat(json, "restitution", component.restitution) ||
      component.friction < 0.0f || component.restitution < 0.0f ||
      component.restitution > 1.0f) {
    return std::nullopt;
  }
  return component;
}

Json serializePhysicsCollisionFilter(
    const components::PhysicsCollisionFilterComponent& component) {
  return Json{
      {"layers", component.layers},
      {"collides_with", component.collides_with},
  };
}

std::optional<components::PhysicsCollisionFilterComponent>
deserializePhysicsCollisionFilter(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::PhysicsCollisionFilterComponent component{};
  if (!readUint32(json, "layers", component.layers) ||
      !readUint32(json, "collides_with", component.collides_with)) {
    return std::nullopt;
  }
  return component;
}

std::string colliderShapeName(components::ColliderShapeType type) {
  switch (type) {
    case components::ColliderShapeType::Box:
      return "box";
    case components::ColliderShapeType::Sphere:
      return "sphere";
    case components::ColliderShapeType::Capsule:
      return "capsule";
    case components::ColliderShapeType::Cylinder:
      return "cylinder";
    case components::ColliderShapeType::TaperedCapsule:
      return "tapered_capsule";
    case components::ColliderShapeType::ConvexHull:
      return "convex_hull";
    case components::ColliderShapeType::Triangle:
      return "triangle";
    case components::ColliderShapeType::HeightField:
      return "height_field";
    case components::ColliderShapeType::Mesh:
      return "mesh";
  }
  return "box";
}

std::optional<components::ColliderShapeType> parseColliderShapeType(std::string_view value) {
  if (value == "box") return components::ColliderShapeType::Box;
  if (value == "sphere") return components::ColliderShapeType::Sphere;
  if (value == "capsule") return components::ColliderShapeType::Capsule;
  if (value == "cylinder") return components::ColliderShapeType::Cylinder;
  if (value == "tapered_capsule") return components::ColliderShapeType::TaperedCapsule;
  if (value == "convex_hull") return components::ColliderShapeType::ConvexHull;
  if (value == "triangle") return components::ColliderShapeType::Triangle;
  if (value == "height_field") return components::ColliderShapeType::HeightField;
  if (value == "mesh") return components::ColliderShapeType::Mesh;
  return std::nullopt;
}

Json serializeColliderShape(const components::BoxColliderShape& shape) {
  return Json{{"center", toJson(shape.center)}, {"half_extents", toJson(shape.half_extents)}};
}

Json serializeColliderShape(const components::SphereColliderShape& shape) {
  return Json{{"center", toJson(shape.center)}, {"radius", shape.radius}};
}

Json serializeColliderShape(const components::CapsuleColliderShape& shape) {
  return Json{{"center", toJson(shape.center)}, {"radius", shape.radius}, {"height", shape.height}};
}

Json serializeColliderShape(const components::CylinderColliderShape& shape) {
  return Json{{"center", toJson(shape.center)},
              {"radius", shape.radius},
              {"height", shape.height},
              {"convex_radius", shape.convex_radius}};
}

Json serializeColliderShape(const components::TaperedCapsuleColliderShape& shape) {
  return Json{{"center", toJson(shape.center)},
              {"top_radius", shape.top_radius},
              {"bottom_radius", shape.bottom_radius},
              {"height", shape.height}};
}

Json serializeColliderShape(const components::ConvexHullColliderShape& shape) {
  Json points = Json::array();
  for (const math::Vec3& point : shape.points) {
    points.push_back(toJson(point));
  }
  return Json{{"center", toJson(shape.center)},
              {"points", std::move(points)},
              {"convex_radius", shape.convex_radius}};
}

Json serializeColliderShape(const components::TriangleColliderShape& shape) {
  Json points = Json::array();
  for (const math::Vec3& point : shape.points) {
    points.push_back(toJson(point));
  }
  return Json{{"points", std::move(points)}, {"convex_radius", shape.convex_radius}};
}

Json serializeColliderShape(const components::HeightFieldColliderShape& shape) {
  return Json{{"samples", shape.samples},
              {"sample_count", shape.sample_count},
              {"offset", toJson(shape.offset)},
              {"scale", toJson(shape.scale)},
              {"block_size", shape.block_size},
              {"bits_per_sample", shape.bits_per_sample}};
}

Json serializeColliderShape(const components::MeshColliderShape& shape) {
  Json vertices = Json::array();
  for (const math::Vec3& vertex : shape.vertices) {
    vertices.push_back(toJson(vertex));
  }
  return Json{{"mesh_asset_key", shape.mesh_asset_key},
              {"vertices", std::move(vertices)},
              {"indices", shape.indices}};
}

Json serializeCollider(const components::ColliderComponent& component) {
  return Json{
      {"type", colliderShapeName(component.type)},
      {"is_trigger", component.is_trigger},
      {"debug_draw", component.debug_draw},
      {"shape", std::visit([](const auto& shape) { return serializeColliderShape(shape); },
                           component.shape)},
  };
}

bool readVec3Array(const Json& json, std::vector<math::Vec3>& out) {
  if (!json.is_array()) {
    return false;
  }
  out.clear();
  out.reserve(json.size());
  for (const Json& item : json) {
    math::Vec3 value{};
    if (!readVec3Value(item, value)) {
      return false;
    }
    out.push_back(value);
  }
  return true;
}

bool readUint32Array(const Json& json, std::vector<uint32_t>& out) {
  if (!json.is_array()) {
    return false;
  }
  out.clear();
  out.reserve(json.size());
  for (const Json& item : json) {
    if (!item.is_number_unsigned() && !item.is_number_integer()) {
      return false;
    }
    const int64_t value = item.get<int64_t>();
    if (value < 0 || value > static_cast<int64_t>(UINT32_MAX)) {
      return false;
    }
    out.push_back(static_cast<uint32_t>(value));
  }
  return true;
}

bool readFloatArray(const Json& json, std::vector<float>& out) {
  if (!json.is_array()) {
    return false;
  }
  out.clear();
  out.reserve(json.size());
  for (const Json& item : json) {
    float value = 0.0f;
    if (!readFloatValue(item, value)) {
      return false;
    }
    out.push_back(value);
  }
  return true;
}

std::optional<components::ColliderComponent> deserializeCollider(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }

  std::string type_name;
  if (!readString(json, "type", type_name)) {
    return std::nullopt;
  }
  const auto type = parseColliderShapeType(type_name);
  if (!type) {
    return std::nullopt;
  }

  components::ColliderComponent component{};
  component.type = *type;
  if (!readBool(json, "is_trigger", component.is_trigger) ||
      !readBool(json, "debug_draw", component.debug_draw)) {
    return std::nullopt;
  }

  const auto shape_it = json.find("shape");
  if (shape_it == json.end() || !shape_it->is_object()) {
    return std::nullopt;
  }
  const Json& shape_json = *shape_it;

  switch (*type) {
    case components::ColliderShapeType::Box: {
      components::BoxColliderShape shape{};
      if (!readVec3(shape_json, "center", shape.center) ||
          !readVec3(shape_json, "half_extents", shape.half_extents)) {
        return std::nullopt;
      }
      component.shape = std::move(shape);
      break;
    }
    case components::ColliderShapeType::Sphere: {
      components::SphereColliderShape shape{};
      if (!readVec3(shape_json, "center", shape.center) ||
          !readFloat(shape_json, "radius", shape.radius)) {
        return std::nullopt;
      }
      component.shape = shape;
      break;
    }
    case components::ColliderShapeType::Capsule: {
      components::CapsuleColliderShape shape{};
      if (!readVec3(shape_json, "center", shape.center) ||
          !readFloat(shape_json, "radius", shape.radius) ||
          !readFloat(shape_json, "height", shape.height)) {
        return std::nullopt;
      }
      component.shape = shape;
      break;
    }
    case components::ColliderShapeType::Cylinder: {
      components::CylinderColliderShape shape{};
      if (!readVec3(shape_json, "center", shape.center) ||
          !readFloat(shape_json, "radius", shape.radius) ||
          !readFloat(shape_json, "height", shape.height) ||
          !readFloat(shape_json, "convex_radius", shape.convex_radius)) {
        return std::nullopt;
      }
      component.shape = shape;
      break;
    }
    case components::ColliderShapeType::TaperedCapsule: {
      components::TaperedCapsuleColliderShape shape{};
      if (!readVec3(shape_json, "center", shape.center) ||
          !readFloat(shape_json, "top_radius", shape.top_radius) ||
          !readFloat(shape_json, "bottom_radius", shape.bottom_radius) ||
          !readFloat(shape_json, "height", shape.height)) {
        return std::nullopt;
      }
      component.shape = shape;
      break;
    }
    case components::ColliderShapeType::ConvexHull: {
      components::ConvexHullColliderShape shape{};
      const auto points_it = shape_json.find("points");
      if (!readVec3(shape_json, "center", shape.center) ||
          points_it == shape_json.end() ||
          !readVec3Array(*points_it, shape.points) ||
          !readFloat(shape_json, "convex_radius", shape.convex_radius)) {
        return std::nullopt;
      }
      component.shape = std::move(shape);
      break;
    }
    case components::ColliderShapeType::Triangle: {
      components::TriangleColliderShape shape{};
      const auto points_it = shape_json.find("points");
      if (points_it == shape_json.end() || !points_it->is_array() ||
          points_it->size() != shape.points.size() ||
          !readFloat(shape_json, "convex_radius", shape.convex_radius)) {
        return std::nullopt;
      }
      for (size_t i = 0; i < shape.points.size(); ++i) {
        if (!readVec3Value((*points_it)[i], shape.points[i])) {
          return std::nullopt;
        }
      }
      component.shape = shape;
      break;
    }
    case components::ColliderShapeType::HeightField: {
      components::HeightFieldColliderShape shape{};
      const auto samples_it = shape_json.find("samples");
      if (samples_it == shape_json.end() ||
          !readFloatArray(*samples_it, shape.samples) ||
          !readUint32(shape_json, "sample_count", shape.sample_count) ||
          !readVec3(shape_json, "offset", shape.offset) ||
          !readVec3(shape_json, "scale", shape.scale) ||
          !readUint32(shape_json, "block_size", shape.block_size) ||
          !readUint32(shape_json, "bits_per_sample", shape.bits_per_sample)) {
        return std::nullopt;
      }
      component.shape = std::move(shape);
      break;
    }
    case components::ColliderShapeType::Mesh: {
      components::MeshColliderShape shape{};
      const auto vertices_it = shape_json.find("vertices");
      const auto indices_it = shape_json.find("indices");
      if (shape_json.contains("mesh_path") ||
          !readString(shape_json, "mesh_asset_key", shape.mesh_asset_key) ||
          (vertices_it != shape_json.end() && !readVec3Array(*vertices_it, shape.vertices)) ||
          (indices_it != shape_json.end() && !readUint32Array(*indices_it, shape.indices))) {
        return std::nullopt;
      }
      component.shape = std::move(shape);
      break;
    }
  }

  return component;
}

Json serializeCharacterController(const components::CharacterControllerComponent& component) {
  return Json{{"enabled", component.enabled},
              {"desired_velocity", toJson(component.desiredVelocity())},
              {"desired_angular_velocity", toJson(component.desiredAngularVelocity())},
              {"add_velocity", toJson(component.addVelocity())}};
}

std::optional<components::CharacterControllerComponent> deserializeCharacterController(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::CharacterControllerComponent component{};
  math::Vec3 desired{};
  math::Vec3 desired_angular{};
  math::Vec3 add{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readVec3(json, "desired_velocity", desired) ||
      !readVec3(json, "desired_angular_velocity", desired_angular) ||
      !readVec3(json, "add_velocity", add)) {
    return std::nullopt;
  }
  component.setDesiredVelocity(desired);
  component.setDesiredAngularVelocity(desired_angular);
  component.setAddVelocity(add);
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
      {"emission_scale", component.emission_scale},
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
  if (component.source_mesh_asset_key.has_value()) {
    json["source_mesh_asset_key"] = *component.source_mesh_asset_key;
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
  if (json.contains("source_mesh_key") || json.contains("source_mesh_path")) {
    return std::nullopt;
  }
  components::ParticleEffectOverrideComponent component{};
  if (!readBool(json, "active", component.active) ||
      !readFloat(json, "time_scale", component.time_scale) ||
      !readFloat(json, "emission_scale", component.emission_scale) ||
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
  if (const auto it = json.find("source_mesh_asset_key"); it != json.end()) {
    if (!it->is_string()) {
      return std::nullopt;
    }
    component.source_mesh_asset_key = it->get<std::string>();
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

Json serializeParticleBeam(const components::ParticleBeamComponent& component) {
  Json points = Json::array();
  for (const math::Vec3& point : component.local_path_points) {
    points.push_back(toJson(point));
  }
  return Json{
      {"enabled", component.enabled},
      {"visible", component.visible},
      {"layer", component.layer},
      {"depth_test", component.depth_test},
      {"blend_mode", blendModeName(component.blend_mode)},
      {"texture_key", component.texture_key},
      {"local_path_points", std::move(points)},
      {"start_width", component.start_width},
      {"end_width", component.end_width},
      {"start_color", toJson(component.start_color)},
      {"end_color", toJson(component.end_color)},
      {"edge_softness", component.edge_softness},
      {"uv_repeat", component.uv_repeat},
      {"uv_scroll_speed", component.uv_scroll_speed},
      {"time_scale", component.time_scale},
      {"restart_count", component.restart_count},
  };
}

std::optional<components::ParticleBeamComponent> deserializeParticleBeam(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::ParticleBeamComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readBool(json, "visible", component.visible) ||
      !readUint32(json, "layer", component.layer) ||
      !readBool(json, "depth_test", component.depth_test) ||
      !readBlendMode(json, component.blend_mode) ||
      !readString(json, "texture_key", component.texture_key) ||
      !readFloat(json, "start_width", component.start_width) ||
      !readFloat(json, "end_width", component.end_width) ||
      !readColor(json, "start_color", component.start_color) ||
      !readColor(json, "end_color", component.end_color) ||
      !readFloat(json, "edge_softness", component.edge_softness) ||
      !readFloat(json, "uv_repeat", component.uv_repeat) ||
      !readFloat(json, "uv_scroll_speed", component.uv_scroll_speed) ||
      !readFloat(json, "time_scale", component.time_scale) ||
      !readUint32(json, "restart_count", component.restart_count)) {
    return std::nullopt;
  }
  if (component.blend_mode == components::ParticleBlendMode::Distortion ||
      component.start_width <= 0.0f ||
      component.end_width <= 0.0f ||
      component.edge_softness < 0.0f ||
      component.uv_repeat < 0.0f ||
      component.time_scale < 0.0f ||
      !std::isfinite(component.start_width) ||
      !std::isfinite(component.end_width) ||
      !std::isfinite(component.edge_softness) ||
      !std::isfinite(component.uv_repeat) ||
      !std::isfinite(component.uv_scroll_speed) ||
      !std::isfinite(component.time_scale)) {
    return std::nullopt;
  }
  const auto points_it = json.find("local_path_points");
  if (points_it == json.end() || !points_it->is_array()) {
    return std::nullopt;
  }
  component.local_path_points.clear();
  component.local_path_points.reserve(points_it->size());
  for (const Json& point_json : *points_it) {
    math::Vec3 point{};
    if (!readVec3Value(point_json, point) ||
        !std::isfinite(point.x) ||
        !std::isfinite(point.y) ||
        !std::isfinite(point.z)) {
      return std::nullopt;
    }
    component.local_path_points.push_back(point);
  }
  if (component.local_path_points.size() < 2u) {
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
      {"radius", component.radius},
      {"capsule_half_length", component.capsule_half_length},
      {"scale_with_transform", component.scale_with_transform},
      {"visible", component.visible},
      {"overlay_depth", component.overlay_depth},
      {"surface_double_sided", component.surface_double_sided},
      {"interior_material_key", component.interior_material_key},
      {"surface_material_key", component.surface_material_key},
  };
}

std::optional<components::VolumetricComponent> deserializeVolumetric(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  static constexpr std::string_view kLegacyFields[] = {
      "color",
      "emissive_color",
      "density",
      "center_opacity",
      "scattering",
      "anisotropy",
      "absorption",
      "distortion_strength",
      "noise_strength",
  };
  for (std::string_view field : kLegacyFields) {
    if (json.find(std::string(field)) != json.end()) {
      return std::nullopt;
    }
  }
  components::VolumetricComponent component{};
  if (!readVolumetricShape(json, component.shape) ||
      !readFloat(json, "radius", component.radius) ||
      !readFloat(json, "capsule_half_length", component.capsule_half_length) ||
      !readBool(json, "scale_with_transform", component.scale_with_transform) ||
      !readBool(json, "visible", component.visible) ||
      !readFloat(json, "overlay_depth", component.overlay_depth) ||
      !readBool(json, "surface_double_sided", component.surface_double_sided) ||
      !readString(json, "interior_material_key", component.interior_material_key) ||
      !readString(json, "surface_material_key", component.surface_material_key)) {
    return std::nullopt;
  }
  if (component.radius <= 0.0f ||
      component.capsule_half_length < 0.0f ||
      component.overlay_depth <= 0.0f) {
    return std::nullopt;
  }
  return component;
}

Json serializeAudioListener(const components::AudioListenerComponent&) {
  return Json::object();
}

std::optional<components::AudioListenerComponent> deserializeAudioListener(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  return components::AudioListenerComponent{};
}

Json serializeAudioSource(const components::AudioSourceComponent& component) {
  return Json{
      {"clip_key", component.clip_key},
      {"gain", component.gain},
      {"pitch", component.pitch},
      {"min_distance", component.min_distance},
      {"max_distance", component.max_distance},
      {"looping", component.looping},
      {"play_on_start", component.play_on_start},
      {"spatialized", component.spatialized},
      {"max_instances", component.max_instances},
  };
}

std::optional<components::AudioSourceComponent> deserializeAudioSource(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::AudioSourceComponent component{};
  int32_t max_instances = component.max_instances;
  if (!readString(json, "clip_key", component.clip_key) ||
      !readFloat(json, "gain", component.gain) ||
      !readFloat(json, "pitch", component.pitch) ||
      !readFloat(json, "min_distance", component.min_distance) ||
      !readFloat(json, "max_distance", component.max_distance) ||
      !readBool(json, "looping", component.looping) ||
      !readBool(json, "play_on_start", component.play_on_start) ||
      !readBool(json, "spatialized", component.spatialized) ||
      !readInt32(json, "max_instances", max_instances) ||
      component.gain < 0.0f || component.pitch <= 0.0f ||
      component.min_distance < 0.0f ||
      component.max_distance < component.min_distance || max_instances <= 0) {
    return std::nullopt;
  }
  component.max_instances = max_instances;
  return component;
}

const char* antiAliasingModeName(rendering::AntiAliasingMode mode) {
  switch (mode) {
    case rendering::AntiAliasingMode::None:
      return "none";
    case rendering::AntiAliasingMode::MSAA:
      return "msaa";
    case rendering::AntiAliasingMode::SSAA:
      return "ssaa";
  }
  return "none";
}

bool readAntiAliasing(const Json& json,
                      rendering::AntiAliasingSettings& out) {
  const auto it = json.find("anti_aliasing");
  if (it == json.end()) {
    return true;
  }
  if (!it->is_object()) {
    return false;
  }
  std::string mode = antiAliasingModeName(out.mode);
  if (!readString(*it, "mode", mode) ||
      !readUint32(*it, "msaa_samples", out.msaa_samples) ||
      !readFloat(*it, "ssaa_scale", out.ssaa_scale)) {
    return false;
  }
  if (mode == "none") {
    out.mode = rendering::AntiAliasingMode::None;
  } else if (mode == "msaa") {
    out.mode = rendering::AntiAliasingMode::MSAA;
  } else if (mode == "ssaa") {
    out.mode = rendering::AntiAliasingMode::SSAA;
  } else {
    return false;
  }
  return out.msaa_samples > 0u && out.ssaa_scale >= 1.0f &&
         out.ssaa_scale <= 4.0f;
}

Json serializeCamera(const components::CameraComponent& component) {
  Json params = Json::object();
  for (const auto& [name, value] : component.shader_user_params) {
    params[name] = toJson(value);
  }
  return Json{
      {"perspective", component.perspective},
      {"render_shadows", component.render_shadows},
      {"fov_y_degrees", component.fov_y_degrees},
      {"near_clip", component.near_clip},
      {"far_clip", component.far_clip},
      {"ortho_left", component.ortho_left},
      {"ortho_right", component.ortho_right},
      {"ortho_top", component.ortho_top},
      {"ortho_bottom", component.ortho_bottom},
      {"is_primary", component.is_primary},
      {"render_to_texture", component.render_to_texture},
      {"render_target_key", component.render_target_key},
      {"frame_graph_key", component.frame_graph_key},
      {"shader_override_vertex_path",
       component.shader_override_vertex_path.generic_string()},
      {"shader_override_fragment_path",
       component.shader_override_fragment_path.generic_string()},
      {"anti_aliasing",
       Json{{"mode", antiAliasingModeName(component.anti_aliasing.mode)},
            {"msaa_samples", component.anti_aliasing.msaa_samples},
            {"ssaa_scale", component.anti_aliasing.ssaa_scale}}},
      {"shader_user_params", std::move(params)},
  };
}

std::optional<components::CameraComponent> deserializeCamera(const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::CameraComponent component{};
  std::string vertex_path;
  std::string fragment_path;
  if (!readBool(json, "perspective", component.perspective) ||
      !readBool(json, "render_shadows", component.render_shadows) ||
      !readFloat(json, "fov_y_degrees", component.fov_y_degrees) ||
      !readFloat(json, "near_clip", component.near_clip) ||
      !readFloat(json, "far_clip", component.far_clip) ||
      !readFloat(json, "ortho_left", component.ortho_left) ||
      !readFloat(json, "ortho_right", component.ortho_right) ||
      !readFloat(json, "ortho_top", component.ortho_top) ||
      !readFloat(json, "ortho_bottom", component.ortho_bottom) ||
      !readBool(json, "is_primary", component.is_primary) ||
      !readBool(json, "primary", component.is_primary) ||
      !readBool(json, "render_to_texture", component.render_to_texture) ||
      !readString(json, "render_target_key", component.render_target_key) ||
      !readString(json, "frame_graph_key", component.frame_graph_key) ||
      !readString(json, "shader_override_vertex_path", vertex_path) ||
      !readString(json, "shader_override_fragment_path", fragment_path) ||
      !readAntiAliasing(json, component.anti_aliasing) ||
      component.near_clip <= 0.0f || component.far_clip <= component.near_clip ||
      (component.perspective &&
       (component.fov_y_degrees < 1.0f || component.fov_y_degrees > 179.0f)) ||
      (!component.perspective &&
       (std::abs(component.ortho_right - component.ortho_left) <= 1.0e-5f ||
        std::abs(component.ortho_top - component.ortho_bottom) <= 1.0e-5f)) ||
      (!vertex_path.empty() && !isPortableRelativePath(vertex_path)) ||
      (!fragment_path.empty() && !isPortableRelativePath(fragment_path))) {
    return std::nullopt;
  }
  component.shader_override_vertex_path = vertex_path;
  component.shader_override_fragment_path = fragment_path;
  if (const auto params_it = json.find("shader_user_params");
      params_it != json.end()) {
    if (!params_it->is_object()) {
      return std::nullopt;
    }
    for (auto it = params_it->begin(); it != params_it->end(); ++it) {
      math::Color value{};
      if (it.key().empty() || !readColorValue(it.value(), value)) {
        return std::nullopt;
      }
      component.shader_user_params[it.key()] = value;
    }
  }
  return component;
}

Json serializeEnvironment(const components::EnvironmentComponent& component) {
  return Json{
      {"environment_map_asset_key", component.environment_map_asset_key},
      {"intensity", component.intensity},
      {"draw_skybox", component.draw_skybox},
      {"enabled", component.enabled},
  };
}

std::optional<components::EnvironmentComponent> deserializeEnvironment(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::EnvironmentComponent component{};
  if (!readString(json,
                  "environment_map_asset_key",
                  component.environment_map_asset_key) ||
      !readFloat(json, "intensity", component.intensity) ||
      !readBool(json, "draw_skybox", component.draw_skybox) ||
      !readBool(json, "enabled", component.enabled) ||
      component.intensity < 0.0f) {
    return std::nullopt;
  }
  return component;
}

Json serializeRootMotion(const components::RootMotionComponent& component) {
  return Json{
      {"root_motion_mode", rootMotionModeName(component.mode)},
      {"root_motion_node_index", component.root_motion_node_index},
  };
}

std::optional<components::RootMotionComponent> deserializeRootMotion(
    const Json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }
  components::RootMotionComponent component{};
  if (!readRootMotionMode(json, component.mode) ||
      !readUint32(json,
                  "root_motion_node_index",
                  component.root_motion_node_index)) {
    return std::nullopt;
  }
  return component;
}

const char* collisionListenModeName(components::CollisionListenMode mode) {
  switch (mode) {
    case components::CollisionListenMode::All:
      return "all";
    case components::CollisionListenMode::TriggersOnly:
      return "triggers_only";
    case components::CollisionListenMode::SolidsOnly:
      return "solids_only";
  }
  return "all";
}

bool readCollisionListenMode(const Json& json,
                             components::CollisionListenMode& out) {
  const auto it = json.find("mode");
  if (it == json.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  const std::string value = it->get<std::string>();
  if (value == "all") out = components::CollisionListenMode::All;
  else if (value == "triggers_only") out = components::CollisionListenMode::TriggersOnly;
  else if (value == "solids_only") out = components::CollisionListenMode::SolidsOnly;
  else return false;
  return true;
}

Json serializeCollisionListener(
    const components::CollisionListenerComponent& component) {
  return Json{{"enabled", component.enabled},
              {"mode", collisionListenModeName(component.mode)},
              {"emit_stay", component.emit_stay},
              {"collision_layer_mask", component.collision_layer_mask}};
}

std::optional<components::CollisionListenerComponent>
deserializeCollisionListener(const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::CollisionListenerComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readCollisionListenMode(json, component.mode) ||
      !readBool(json, "emit_stay", component.emit_stay) ||
      !readUint32(json,
                  "collision_layer_mask",
                  component.collision_layer_mask)) {
    return std::nullopt;
  }
  return component;
}

Json serializeContactListener(
    const components::ContactListenerComponent& component) {
  return Json{{"enabled", component.enabled},
              {"emit_stay", component.emit_stay},
              {"collision_layer_mask", component.collision_layer_mask}};
}

std::optional<components::ContactListenerComponent> deserializeContactListener(
    const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::ContactListenerComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readBool(json, "emit_stay", component.emit_stay) ||
      !readUint32(json,
                  "collision_layer_mask",
                  component.collision_layer_mask)) {
    return std::nullopt;
  }
  return component;
}

Json serializeGroundContact(const components::GroundContactComponent&) {
  // All contact fields are runtime outputs; component presence is authored.
  return Json::object();
}

std::optional<components::GroundContactComponent> deserializeGroundContact(
    const Json& json) {
  if (!json.is_object()) return std::nullopt;
  return components::GroundContactComponent{};
}

const char* deformationPathName(components::DeformationPath path) {
  return path == components::DeformationPath::CpuReference ? "cpu_reference"
                                                           : "gpu";
}

bool readDeformationPath(const Json& json, components::DeformationPath& out) {
  const auto it = json.find("path");
  if (it == json.end()) return true;
  if (!it->is_string()) return false;
  const std::string value = it->get<std::string>();
  if (value == "gpu") out = components::DeformationPath::Gpu;
  else if (value == "cpu_reference") out = components::DeformationPath::CpuReference;
  else return false;
  return true;
}

Json serializeDeformableMesh(
    const components::DeformableMeshComponent& component,
    const ComponentSerializationContext& context) {
  Json joints = Json::array();
  for (world::Entity joint : component.joint_entities) {
    joints.push_back(serializeEntityReference(joint, context));
  }
  return Json{
      {"joint_entities", std::move(joints)},
      {"base_morph_weights", floatVectorJson(component.base_morph_weights)},
      {"morph_weights", floatVectorJson(component.morph_weights)},
      {"render_transform_entity",
       serializeEntityReference(component.render_transform_entity, context)},
      {"skin_index", component.skin_index},
      {"path", deformationPathName(component.path)},
      {"override_render_transform", component.override_render_transform},
      {"enabled", component.enabled},
  };
}

std::optional<components::DeformableMeshComponent> deserializeDeformableMesh(
    const Json& json,
    const ComponentSerializationContext& context) {
  if (!json.is_object()) return std::nullopt;
  components::DeformableMeshComponent component{};
  if (!readFloatVector(json,
                       "base_morph_weights",
                       component.base_morph_weights) ||
      !readFloatVector(json, "morph_weights", component.morph_weights) ||
      !readEntityReference(json,
                           "render_transform_entity",
                           component.render_transform_entity,
                           context) ||
      !readUint32(json, "skin_index", component.skin_index) ||
      !readDeformationPath(json, component.path) ||
      !readBool(json,
                "override_render_transform",
                component.override_render_transform) ||
      !readBool(json, "enabled", component.enabled)) {
    return std::nullopt;
  }
  if (const auto joints_it = json.find("joint_entities");
      joints_it != json.end()) {
    if (!joints_it->is_array()) return std::nullopt;
    component.joint_entities.reserve(joints_it->size());
    for (const Json& entry : *joints_it) {
      world::Entity joint{};
      if (!resolveEntityReferenceValue(entry, joint, context) ||
          !joint.isValid()) {
        return std::nullopt;
      }
      component.joint_entities.push_back(joint);
    }
  }
  if (!component.base_morph_weights.empty() &&
      !component.morph_weights.empty() &&
      component.base_morph_weights.size() != component.morph_weights.size()) {
    return std::nullopt;
  }
  return component;
}

const char* authorityModeName(components::AuthorityMode mode) {
  switch (mode) {
    case components::AuthorityMode::Server: return "server";
    case components::AuthorityMode::Owner: return "owner";
    case components::AuthorityMode::Client: return "client";
    case components::AuthorityMode::Custom: return "custom";
  }
  return "server";
}

bool readAuthorityMode(const Json& json, components::AuthorityMode& out) {
  const auto it = json.find("mode");
  if (it == json.end()) return true;
  if (!it->is_string()) return false;
  const std::string value = it->get<std::string>();
  if (value == "server") out = components::AuthorityMode::Server;
  else if (value == "owner") out = components::AuthorityMode::Owner;
  else if (value == "client") out = components::AuthorityMode::Client;
  else if (value == "custom") out = components::AuthorityMode::Custom;
  else return false;
  return true;
}

const char* replicationPolicyName(components::ReplicationPolicy policy) {
  switch (policy) {
    case components::ReplicationPolicy::Snapshot: return "snapshot";
    case components::ReplicationPolicy::Delta: return "delta";
    case components::ReplicationPolicy::OwnerInput: return "owner_input";
  }
  return "snapshot";
}

bool parseReplicationPolicy(std::string_view value,
                            components::ReplicationPolicy& out) {
  if (value == "snapshot") out = components::ReplicationPolicy::Snapshot;
  else if (value == "delta") out = components::ReplicationPolicy::Delta;
  else if (value == "owner_input") out = components::ReplicationPolicy::OwnerInput;
  else return false;
  return true;
}

Json serializeNetworkIdentity(
    const components::NetworkIdentityComponent& component) {
  return Json{{"id", component.id}};
}

std::optional<components::NetworkIdentityComponent> deserializeNetworkIdentity(
    const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::NetworkIdentityComponent component{};
  if (!readUint64(json, "id", component.id)) return std::nullopt;
  return component;
}

Json serializeNetworkAuthority(
    const components::NetworkAuthorityComponent& component) {
  return Json{{"mode", authorityModeName(component.mode)},
              {"owner_peer", component.owner_peer},
              {"server_can_override", component.server_can_override}};
}

std::optional<components::NetworkAuthorityComponent>
deserializeNetworkAuthority(const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::NetworkAuthorityComponent component{};
  if (!readAuthorityMode(json, component.mode) ||
      !readUint32(json, "owner_peer", component.owner_peer) ||
      !readBool(json,
                "server_can_override",
                component.server_can_override)) {
    return std::nullopt;
  }
  return component;
}

Json serializeNetworkReplicated(
    const components::NetworkReplicatedComponent& component) {
  Json entries = Json::array();
  for (const auto& entry : component.components) {
    entries.push_back(Json{{"component_type", entry.component_type},
                           {"policy", replicationPolicyName(entry.policy)}});
  }
  return Json{{"components", std::move(entries)},
              {"visible_by_default", component.visible_by_default}};
}

std::optional<components::NetworkReplicatedComponent>
deserializeNetworkReplicated(const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::NetworkReplicatedComponent component{};
  if (!readBool(json,
                "visible_by_default",
                component.visible_by_default)) {
    return std::nullopt;
  }
  if (const auto entries_it = json.find("components");
      entries_it != json.end()) {
    if (!entries_it->is_array()) return std::nullopt;
    for (const Json& entry_json : *entries_it) {
      if (!entry_json.is_object()) return std::nullopt;
      components::ReplicatedComponentEntry entry{};
      std::string policy = replicationPolicyName(entry.policy);
      if (!readUint32(entry_json, "component_type", entry.component_type) ||
          !readString(entry_json, "policy", policy) ||
          !parseReplicationPolicy(policy, entry.policy)) {
        return std::nullopt;
      }
      component.components.push_back(entry);
    }
  }
  return component;
}

Json serializeScript(const components::ScriptComponent& component) {
  return Json{{"script_key", component.script_key},
              {"enabled", component.enabled}};
}

std::optional<components::ScriptComponent> deserializeScript(const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::ScriptComponent component{};
  if (!readString(json, "script_key", component.script_key) ||
      !readBool(json, "enabled", component.enabled)) {
    return std::nullopt;
  }
  return component;
}

Json serializeStatic(const components::StaticComponent& component) {
  return Json{{"enabled", component.enabled},
              {"include_descendants", component.include_descendants},
              {"flags", component.flags}};
}

std::optional<components::StaticComponent> deserializeStatic(const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::StaticComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readBool(json,
                "include_descendants",
                component.include_descendants) ||
      !readUint32(json, "flags", component.flags) ||
      !components::validStaticComponentFlags(component.flags)) {
    return std::nullopt;
  }
  return component;
}

#if defined(KARMA_ENABLE_NAVIGATION)

Json serializeNavQueryFilter(const navigation::NavQueryFilter& filter) {
  Json costs = Json::array();
  for (float cost : filter.area_costs) costs.push_back(cost);
  return Json{{"include_flags", filter.include_flags},
              {"exclude_flags", filter.exclude_flags},
              {"area_costs", std::move(costs)}};
}

bool deserializeNavQueryFilter(const Json& json,
                               navigation::NavQueryFilter& out) {
  if (!json.is_object() ||
      !readUint16(json, "include_flags", out.include_flags) ||
      !readUint16(json, "exclude_flags", out.exclude_flags)) {
    return false;
  }
  if (const auto costs_it = json.find("area_costs"); costs_it != json.end()) {
    if (!costs_it->is_array() ||
        costs_it->size() != out.area_costs.size()) {
      return false;
    }
    for (size_t index = 0; index < out.area_costs.size(); ++index) {
      if (!readFloatValue((*costs_it)[index], out.area_costs[index]) ||
          out.area_costs[index] <= 0.0f) {
        return false;
      }
    }
  }
  return true;
}

const char* navBuildModeName(navigation::NavMeshBuildMode mode) {
  return mode == navigation::NavMeshBuildMode::Tiled ? "tiled" : "solo";
}

const char* navPartitionName(navigation::NavMeshPartitionType partition) {
  switch (partition) {
    case navigation::NavMeshPartitionType::Watershed: return "watershed";
    case navigation::NavMeshPartitionType::Monotone: return "monotone";
    case navigation::NavMeshPartitionType::Layers: return "layers";
  }
  return "watershed";
}

Json serializeNavBuildConfig(const navigation::NavMeshBuildConfig& config) {
  Json areas = Json::array();
  for (const auto& area : config.area_configs) {
    areas.push_back(Json{{"area", area.area},
                         {"flags", area.flags},
                         {"cost", area.cost}});
  }
  return Json{
      {"build_mode", navBuildModeName(config.build_mode)},
      {"partition_type", navPartitionName(config.partition_type)},
      {"cell_size", config.cell_size},
      {"cell_height", config.cell_height},
      {"agent_height", config.agent_height},
      {"agent_radius", config.agent_radius},
      {"agent_max_climb", config.agent_max_climb},
      {"agent_max_slope_degrees", config.agent_max_slope_degrees},
      {"edge_max_len", config.edge_max_len},
      {"edge_max_error", config.edge_max_error},
      {"region_min_size", config.region_min_size},
      {"region_merge_size", config.region_merge_size},
      {"verts_per_poly", config.verts_per_poly},
      {"detail_sample_dist", config.detail_sample_dist},
      {"detail_sample_max_error", config.detail_sample_max_error},
      {"default_poly_flags", config.default_poly_flags},
      {"off_mesh_poly_flags", config.off_mesh_poly_flags},
      {"tile_size", config.tile_size},
      {"max_tiles", config.max_tiles},
      {"max_polys_per_tile", config.max_polys_per_tile},
      {"collect_build_debug_draw", config.collect_build_debug_draw},
      {"area_configs", std::move(areas)},
  };
}

bool deserializeNavBuildConfig(const Json& json,
                               navigation::NavMeshBuildConfig& out) {
  if (!json.is_object()) return false;
  std::string build_mode = navBuildModeName(out.build_mode);
  std::string partition = navPartitionName(out.partition_type);
  int32_t verts_per_poly = out.verts_per_poly;
  int32_t tile_size = out.tile_size;
  int32_t max_tiles = out.max_tiles;
  int32_t max_polys = out.max_polys_per_tile;
  if (!readString(json, "build_mode", build_mode) ||
      !readString(json, "partition_type", partition) ||
      !readFloat(json, "cell_size", out.cell_size) ||
      !readFloat(json, "cell_height", out.cell_height) ||
      !readFloat(json, "agent_height", out.agent_height) ||
      !readFloat(json, "agent_radius", out.agent_radius) ||
      !readFloat(json, "agent_max_climb", out.agent_max_climb) ||
      !readFloat(json,
                 "agent_max_slope_degrees",
                 out.agent_max_slope_degrees) ||
      !readFloat(json, "edge_max_len", out.edge_max_len) ||
      !readFloat(json, "edge_max_error", out.edge_max_error) ||
      !readFloat(json, "region_min_size", out.region_min_size) ||
      !readFloat(json, "region_merge_size", out.region_merge_size) ||
      !readInt32(json, "verts_per_poly", verts_per_poly) ||
      !readFloat(json, "detail_sample_dist", out.detail_sample_dist) ||
      !readFloat(json,
                 "detail_sample_max_error",
                 out.detail_sample_max_error) ||
      !readUint16(json, "default_poly_flags", out.default_poly_flags) ||
      !readUint16(json, "off_mesh_poly_flags", out.off_mesh_poly_flags) ||
      !readInt32(json, "tile_size", tile_size) ||
      !readInt32(json, "max_tiles", max_tiles) ||
      !readInt32(json, "max_polys_per_tile", max_polys) ||
      !readBool(json,
                "collect_build_debug_draw",
                out.collect_build_debug_draw)) {
    return false;
  }
  if (build_mode == "solo") out.build_mode = navigation::NavMeshBuildMode::Solo;
  else if (build_mode == "tiled") out.build_mode = navigation::NavMeshBuildMode::Tiled;
  else return false;
  if (partition == "watershed") out.partition_type = navigation::NavMeshPartitionType::Watershed;
  else if (partition == "monotone") out.partition_type = navigation::NavMeshPartitionType::Monotone;
  else if (partition == "layers") out.partition_type = navigation::NavMeshPartitionType::Layers;
  else return false;
  out.verts_per_poly = verts_per_poly;
  out.tile_size = tile_size;
  out.max_tiles = max_tiles;
  out.max_polys_per_tile = max_polys;
  if (out.cell_size <= 0.0f || out.cell_height <= 0.0f ||
      out.agent_height <= 0.0f || out.agent_radius < 0.0f ||
      out.verts_per_poly < 3 || out.tile_size <= 0 || out.max_tiles < 0 ||
      out.max_polys_per_tile < 0) {
    return false;
  }
  if (const auto areas_it = json.find("area_configs");
      areas_it != json.end()) {
    if (!areas_it->is_array()) return false;
    out.area_configs.clear();
    for (const Json& area_json : *areas_it) {
      if (!area_json.is_object()) return false;
      navigation::NavAreaConfig area{};
      if (!readUint8(area_json, "area", area.area) ||
          !readUint16(area_json, "flags", area.flags) ||
          !readFloat(area_json, "cost", area.cost) || area.cost <= 0.0f) {
        return false;
      }
      out.area_configs.push_back(area);
    }
  }
  return true;
}

Json serializeNavCrowdConfig(const navigation::NavCrowdConfig& config) {
  Json filters = Json::array();
  for (const auto& filter : config.query_filters) {
    filters.push_back(serializeNavQueryFilter(filter));
  }
  Json avoidance = Json::array();
  for (const auto& value : config.avoidance_params) {
    avoidance.push_back(Json{
        {"velocity_bias", value.velocity_bias},
        {"weight_desired_velocity", value.weight_desired_velocity},
        {"weight_current_velocity", value.weight_current_velocity},
        {"weight_side", value.weight_side},
        {"weight_time_of_impact", value.weight_time_of_impact},
        {"horizontal_time", value.horizontal_time},
        {"grid_size", value.grid_size},
        {"adaptive_divisions", value.adaptive_divisions},
        {"adaptive_rings", value.adaptive_rings},
        {"adaptive_depth", value.adaptive_depth},
    });
  }
  return Json{{"max_agents", config.max_agents},
              {"max_agent_radius", config.max_agent_radius},
              {"query_extents", toJson(config.query_extents)},
              {"query_filters", std::move(filters)},
              {"avoidance_params", std::move(avoidance)}};
}

bool deserializeNavCrowdConfig(const Json& json,
                               navigation::NavCrowdConfig& out) {
  if (!json.is_object()) return false;
  int32_t max_agents = out.max_agents;
  if (!readInt32(json, "max_agents", max_agents) ||
      !readFloat(json, "max_agent_radius", out.max_agent_radius) ||
      !readVec3(json, "query_extents", out.query_extents) ||
      max_agents <= 0 || out.max_agent_radius <= 0.0f) {
    return false;
  }
  out.max_agents = max_agents;
  if (const auto filters_it = json.find("query_filters");
      filters_it != json.end()) {
    if (!filters_it->is_array()) return false;
    out.query_filters.clear();
    for (const Json& entry : *filters_it) {
      navigation::NavQueryFilter filter{};
      if (!deserializeNavQueryFilter(entry, filter)) return false;
      out.query_filters.push_back(filter);
    }
  }
  if (const auto values_it = json.find("avoidance_params");
      values_it != json.end()) {
    if (!values_it->is_array()) return false;
    out.avoidance_params.clear();
    for (const Json& entry : *values_it) {
      if (!entry.is_object()) return false;
      navigation::NavCrowdObstacleAvoidanceParams value{};
      if (!readFloat(entry, "velocity_bias", value.velocity_bias) ||
          !readFloat(entry,
                     "weight_desired_velocity",
                     value.weight_desired_velocity) ||
          !readFloat(entry,
                     "weight_current_velocity",
                     value.weight_current_velocity) ||
          !readFloat(entry, "weight_side", value.weight_side) ||
          !readFloat(entry,
                     "weight_time_of_impact",
                     value.weight_time_of_impact) ||
          !readFloat(entry, "horizontal_time", value.horizontal_time) ||
          !readUint8(entry, "grid_size", value.grid_size) ||
          !readUint8(entry,
                     "adaptive_divisions",
                     value.adaptive_divisions) ||
          !readUint8(entry, "adaptive_rings", value.adaptive_rings) ||
          !readUint8(entry, "adaptive_depth", value.adaptive_depth)) {
        return false;
      }
      out.avoidance_params.push_back(value);
    }
  }
  return true;
}

Json serializeNavCrowdAgentParams(
    const navigation::NavCrowdAgentParams& params) {
  return Json{
      {"radius", params.radius},
      {"height", params.height},
      {"max_acceleration", params.max_acceleration},
      {"max_speed", params.max_speed},
      {"collision_query_range", params.collision_query_range},
      {"path_optimization_range", params.path_optimization_range},
      {"separation_weight", params.separation_weight},
      {"update_flags", params.update_flags},
      {"obstacle_avoidance_type", params.obstacle_avoidance_type},
      {"query_filter_type", params.query_filter_type},
  };
}

bool deserializeNavCrowdAgentParams(
    const Json& json,
    navigation::NavCrowdAgentParams& out) {
  return json.is_object() &&
         readFloat(json, "radius", out.radius) &&
         readFloat(json, "height", out.height) &&
         readFloat(json, "max_acceleration", out.max_acceleration) &&
         readFloat(json, "max_speed", out.max_speed) &&
         readFloat(json,
                   "collision_query_range",
                   out.collision_query_range) &&
         readFloat(json,
                   "path_optimization_range",
                   out.path_optimization_range) &&
         readFloat(json, "separation_weight", out.separation_weight) &&
         readUint8(json, "update_flags", out.update_flags) &&
         readUint8(json,
                   "obstacle_avoidance_type",
                   out.obstacle_avoidance_type) &&
         readUint8(json, "query_filter_type", out.query_filter_type) &&
         out.radius > 0.0f && out.height > 0.0f &&
         out.max_acceleration >= 0.0f && out.max_speed >= 0.0f;
}

Json serializeNavMeshSurface(
    const components::NavMeshSurfaceComponent& component) {
  return Json{{"enabled", component.enabled},
              {"layer_mask", component.layer_mask},
              {"area", component.area},
              {"walkable", component.walkable},
              {"mesh_asset_key", component.mesh_asset_key}};
}

std::optional<components::NavMeshSurfaceComponent> deserializeNavMeshSurface(
    const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::NavMeshSurfaceComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readUint32(json, "layer_mask", component.layer_mask) ||
      !readUint8(json, "area", component.area) ||
      !readBool(json, "walkable", component.walkable) ||
      !readString(json, "mesh_asset_key", component.mesh_asset_key)) {
    return std::nullopt;
  }
  return component;
}

Json serializeNavOffMeshLink(
    const components::NavOffMeshLinkComponent& component,
    const ComponentSerializationContext& context) {
  return Json{{"enabled", component.enabled},
              {"layer_mask", component.layer_mask},
              {"end_entity",
               serializeEntityReference(component.end_entity, context)},
              {"start_offset", toJson(component.start_offset)},
              {"end_offset", toJson(component.end_offset)},
              {"radius", component.radius},
              {"area", component.area},
              {"flags", component.flags},
              {"bidirectional", component.bidirectional},
              {"user_id", component.user_id}};
}

std::optional<components::NavOffMeshLinkComponent> deserializeNavOffMeshLink(
    const Json& json,
    const ComponentSerializationContext& context) {
  if (!json.is_object()) return std::nullopt;
  components::NavOffMeshLinkComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readUint32(json, "layer_mask", component.layer_mask) ||
      !readEntityReference(json, "end_entity", component.end_entity, context) ||
      !readVec3(json, "start_offset", component.start_offset) ||
      !readVec3(json, "end_offset", component.end_offset) ||
      !readFloat(json, "radius", component.radius) ||
      !readUint8(json, "area", component.area) ||
      !readUint16(json, "flags", component.flags) ||
      !readBool(json, "bidirectional", component.bidirectional) ||
      !readUint32(json, "user_id", component.user_id) ||
      component.radius <= 0.0f) {
    return std::nullopt;
  }
  return component;
}

Json serializeNavConvexVolume(
    const components::NavConvexVolumeComponent& component) {
  return Json{{"enabled", component.enabled},
              {"layer_mask", component.layer_mask},
              {"vertices", vec3VectorJson(component.vertices)},
              {"min_y", component.min_y},
              {"max_y", component.max_y},
              {"area", component.area}};
}

std::optional<components::NavConvexVolumeComponent>
deserializeNavConvexVolume(const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::NavConvexVolumeComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readUint32(json, "layer_mask", component.layer_mask) ||
      !readVec3Vector(json, "vertices", component.vertices) ||
      !readFloat(json, "min_y", component.min_y) ||
      !readFloat(json, "max_y", component.max_y) ||
      !readUint8(json, "area", component.area) ||
      component.min_y > component.max_y) {
    return std::nullopt;
  }
  return component;
}

bool readNavDebugMode(const Json& json,
                      navigation::NavMeshDebugDrawMode& out) {
  const auto it = json.find("debug_draw_mode");
  if (it == json.end()) return true;
  if (!it->is_string()) return false;
  const std::string value = it->get<std::string>();
  for (size_t index = 0; index < navigation::kNavMeshDebugDrawModeCount;
       ++index) {
    const auto candidate =
        static_cast<navigation::NavMeshDebugDrawMode>(index);
    if (value == navigation::navMeshDebugDrawModeName(candidate)) {
      out = candidate;
      return true;
    }
  }
  return false;
}

Json serializeNavMesh(const components::NavMeshComponent& component) {
  return Json{
      {"enabled", component.enabled},
      {"build_on_start", component.build_on_start},
      {"debug_draw", component.debug_draw},
      {"debug_draw_mode",
       navigation::navMeshDebugDrawModeName(component.debug_draw_mode)},
      {"source_mask", component.source_mask},
      {"cache", Json{{"enabled", component.cache.enabled},
                      {"read", component.cache.read},
                      {"write", component.cache.write}}},
      {"build_config", serializeNavBuildConfig(component.build_config)},
  };
}

std::optional<components::NavMeshComponent> deserializeNavMesh(
    const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::NavMeshComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readBool(json, "build_on_start", component.build_on_start) ||
      !readBool(json, "debug_draw", component.debug_draw) ||
      !readNavDebugMode(json, component.debug_draw_mode) ||
      !readUint32(json, "source_mask", component.source_mask)) {
    return std::nullopt;
  }
  if (const auto cache_it = json.find("cache"); cache_it != json.end()) {
    if (!cache_it->is_object() ||
        !readBool(*cache_it, "enabled", component.cache.enabled) ||
        !readBool(*cache_it, "read", component.cache.read) ||
        !readBool(*cache_it, "write", component.cache.write)) {
      return std::nullopt;
    }
  }
  if (const auto config_it = json.find("build_config");
      config_it != json.end() &&
      !deserializeNavBuildConfig(*config_it, component.build_config)) {
    return std::nullopt;
  }
  return component;
}

const char* navCrowdMovementModeName(components::NavCrowdMovementMode mode) {
  return mode == components::NavCrowdMovementMode::CharacterControllerVelocity
             ? "character_controller_velocity"
             : "transform";
}

bool readNavCrowdMovementMode(const Json& json,
                              components::NavCrowdMovementMode& out) {
  const auto it = json.find("movement_mode");
  if (it == json.end()) return true;
  if (!it->is_string()) return false;
  const std::string value = it->get<std::string>();
  if (value == "transform") out = components::NavCrowdMovementMode::Transform;
  else if (value == "character_controller_velocity") {
    out = components::NavCrowdMovementMode::CharacterControllerVelocity;
  } else return false;
  return true;
}

Json serializeNavCrowd(const components::NavCrowdComponent& component) {
  return Json{{"enabled", component.enabled},
              {"build_on_start", component.build_on_start},
              {"simulation_paused", component.simulation_paused},
              {"step_dt", component.step_dt},
              {"time_scale", component.time_scale},
              {"config", serializeNavCrowdConfig(component.config)}};
}

std::optional<components::NavCrowdComponent> deserializeNavCrowd(
    const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::NavCrowdComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readBool(json, "build_on_start", component.build_on_start) ||
      !readBool(json, "simulation_paused", component.simulation_paused) ||
      !readFloat(json, "step_dt", component.step_dt) ||
      !readFloat(json, "time_scale", component.time_scale) ||
      component.step_dt <= 0.0f || component.time_scale < 0.0f) {
    return std::nullopt;
  }
  if (const auto config_it = json.find("config");
      config_it != json.end() &&
      !deserializeNavCrowdConfig(*config_it, component.config)) {
    return std::nullopt;
  }
  return component;
}

Json serializeNavCrowdAgent(
    const components::NavCrowdAgentComponent& component,
    const ComponentSerializationContext& context) {
  return Json{
      {"enabled", component.enabled},
      {"crowd_entity",
       serializeEntityReference(component.crowd_entity, context)},
      {"params", serializeNavCrowdAgentParams(component.params)},
      {"destination", toJson(component.destination)},
      {"requested_velocity", toJson(component.requested_velocity)},
      {"search_extents", toJson(component.search_extents)},
      {"height_offset", component.height_offset},
      {"stopping_distance", component.stopping_distance},
      {"has_destination", component.has_destination},
      {"movement_mode", navCrowdMovementModeName(component.movement_mode)},
  };
}

std::optional<components::NavCrowdAgentComponent> deserializeNavCrowdAgent(
    const Json& json,
    const ComponentSerializationContext& context) {
  if (!json.is_object()) return std::nullopt;
  components::NavCrowdAgentComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readEntityReference(json,
                           "crowd_entity",
                           component.crowd_entity,
                           context) ||
      !readVec3(json, "destination", component.destination) ||
      !readVec3(json, "requested_velocity", component.requested_velocity) ||
      !readVec3(json, "search_extents", component.search_extents) ||
      !readFloat(json, "height_offset", component.height_offset) ||
      !readFloat(json, "stopping_distance", component.stopping_distance) ||
      !readBool(json, "has_destination", component.has_destination) ||
      !readNavCrowdMovementMode(json, component.movement_mode) ||
      component.stopping_distance < 0.0f) {
    return std::nullopt;
  }
  if (const auto params_it = json.find("params");
      params_it != json.end() &&
      !deserializeNavCrowdAgentParams(*params_it, component.params)) {
    return std::nullopt;
  }
  component.destination_requested = component.has_destination;
  return component;
}

Json serializeNavMeshAgent(
    const components::NavMeshAgentComponent& component,
    const ComponentSerializationContext& context) {
  return Json{
      {"enabled", component.enabled},
      {"speed", component.speed},
      {"stopping_distance", component.stopping_distance},
      {"height_offset", component.height_offset},
      {"update_vertical_position", component.update_vertical_position},
      {"accept_partial_paths", component.accept_partial_paths},
      {"destination", toJson(component.destination)},
      {"search_extents", toJson(component.search_extents)},
      {"nav_mesh_entity",
       serializeEntityReference(component.nav_mesh_entity, context)},
      {"query_filter", serializeNavQueryFilter(component.query_filter)},
      {"has_destination", component.has_destination},
  };
}

std::optional<components::NavMeshAgentComponent> deserializeNavMeshAgent(
    const Json& json,
    const ComponentSerializationContext& context) {
  if (!json.is_object()) return std::nullopt;
  components::NavMeshAgentComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readFloat(json, "speed", component.speed) ||
      !readFloat(json, "stopping_distance", component.stopping_distance) ||
      !readFloat(json, "height_offset", component.height_offset) ||
      !readBool(json,
                "update_vertical_position",
                component.update_vertical_position) ||
      !readBool(json,
                "accept_partial_paths",
                component.accept_partial_paths) ||
      !readVec3(json, "destination", component.destination) ||
      !readVec3(json, "search_extents", component.search_extents) ||
      !readEntityReference(json,
                           "nav_mesh_entity",
                           component.nav_mesh_entity,
                           context) ||
      !readBool(json, "has_destination", component.has_destination) ||
      component.speed < 0.0f || component.stopping_distance < 0.0f) {
    return std::nullopt;
  }
  if (const auto filter_it = json.find("query_filter");
      filter_it != json.end() &&
      !deserializeNavQueryFilter(*filter_it, component.query_filter)) {
    return std::nullopt;
  }
  component.path_requested = component.has_destination;
  component.status = component.has_destination
                         ? components::NavMeshAgentStatus::Requested
                         : components::NavMeshAgentStatus::Idle;
  return component;
}

const char* navTileCompressionName(
    navigation::NavTileCacheCompression compression) {
  return compression == navigation::NavTileCacheCompression::None ? "none"
                                                                   : "fast_lz";
}

bool readNavTileBuildConfig(const Json& json,
                            navigation::NavTileCacheBuildConfig& out) {
  if (!json.is_object()) return false;
  int32_t expected_layers = out.expected_layers_per_tile;
  int32_t max_obstacles = out.max_obstacles;
  int32_t max_layers = out.max_layers_per_tile;
  std::string compression = navTileCompressionName(out.compression);
  if (!readInt32(json, "expected_layers_per_tile", expected_layers) ||
      !readInt32(json, "max_obstacles", max_obstacles) ||
      !readInt32(json, "max_layers_per_tile", max_layers) ||
      !readSize(json, "allocator_size", out.allocator_size) ||
      !readString(json, "compression", compression) ||
      expected_layers <= 0 || max_obstacles <= 0 || max_layers <= 0 ||
      out.allocator_size == 0u) {
    return false;
  }
  out.expected_layers_per_tile = expected_layers;
  out.max_obstacles = max_obstacles;
  out.max_layers_per_tile = max_layers;
  if (compression == "none") out.compression = navigation::NavTileCacheCompression::None;
  else if (compression == "fast_lz") out.compression = navigation::NavTileCacheCompression::FastLz;
  else return false;
  return true;
}

Json serializeNavTileBuildConfig(
    const navigation::NavTileCacheBuildConfig& config) {
  return Json{{"expected_layers_per_tile", config.expected_layers_per_tile},
              {"max_obstacles", config.max_obstacles},
              {"max_layers_per_tile", config.max_layers_per_tile},
              {"allocator_size", config.allocator_size},
              {"compression", navTileCompressionName(config.compression)}};
}

Json serializeNavTileCache(
    const components::NavTileCacheComponent& component) {
  return Json{{"enabled", component.enabled},
              {"build_on_start", component.build_on_start},
              {"build_config",
               serializeNavTileBuildConfig(component.build_config)}};
}

std::optional<components::NavTileCacheComponent> deserializeNavTileCache(
    const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::NavTileCacheComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readBool(json, "build_on_start", component.build_on_start)) {
    return std::nullopt;
  }
  if (const auto config_it = json.find("build_config");
      config_it != json.end() &&
      !readNavTileBuildConfig(*config_it, component.build_config)) {
    return std::nullopt;
  }
  return component;
}

const char* navObstacleShapeName(
    navigation::NavTileCacheObstacleShape shape) {
  switch (shape) {
    case navigation::NavTileCacheObstacleShape::Cylinder: return "cylinder";
    case navigation::NavTileCacheObstacleShape::Box: return "box";
    case navigation::NavTileCacheObstacleShape::OrientedBox: return "oriented_box";
  }
  return "cylinder";
}

bool readNavObstacleShape(const Json& json,
                          navigation::NavTileCacheObstacleShape& out) {
  const auto it = json.find("shape");
  if (it == json.end()) return true;
  if (!it->is_string()) return false;
  const std::string value = it->get<std::string>();
  if (value == "cylinder") out = navigation::NavTileCacheObstacleShape::Cylinder;
  else if (value == "box") out = navigation::NavTileCacheObstacleShape::Box;
  else if (value == "oriented_box") out = navigation::NavTileCacheObstacleShape::OrientedBox;
  else return false;
  return true;
}

Json serializeNavTileCacheObstacle(
    const components::NavTileCacheObstacleComponent& component,
    const ComponentSerializationContext& context) {
  return Json{
      {"enabled", component.enabled},
      {"nav_mesh_entity",
       serializeEntityReference(component.nav_mesh_entity, context)},
      {"shape", navObstacleShapeName(component.shape)},
      {"offset", toJson(component.offset)},
      {"half_extents", toJson(component.half_extents)},
      {"bounds_min", toJson(component.bounds_min)},
      {"bounds_max", toJson(component.bounds_max)},
      {"radius", component.radius},
      {"height", component.height},
      {"yaw_radians", component.yaw_radians},
  };
}

std::optional<components::NavTileCacheObstacleComponent>
deserializeNavTileCacheObstacle(
    const Json& json,
    const ComponentSerializationContext& context) {
  if (!json.is_object()) return std::nullopt;
  components::NavTileCacheObstacleComponent component{};
  if (!readBool(json, "enabled", component.enabled) ||
      !readEntityReference(json,
                           "nav_mesh_entity",
                           component.nav_mesh_entity,
                           context) ||
      !readNavObstacleShape(json, component.shape) ||
      !readVec3(json, "offset", component.offset) ||
      !readVec3(json, "half_extents", component.half_extents) ||
      !readVec3(json, "bounds_min", component.bounds_min) ||
      !readVec3(json, "bounds_max", component.bounds_max) ||
      !readFloat(json, "radius", component.radius) ||
      !readFloat(json, "height", component.height) ||
      !readFloat(json, "yaw_radians", component.yaw_radians) ||
      component.radius <= 0.0f || component.height <= 0.0f) {
    return std::nullopt;
  }
  return component;
}

#endif  // defined(KARMA_ENABLE_NAVIGATION)

const char* constraintKindName(components::PhysicsConstraintKind kind) {
  switch (kind) {
    case components::PhysicsConstraintKind::Fixed: return "fixed";
    case components::PhysicsConstraintKind::Point: return "point";
    case components::PhysicsConstraintKind::Distance: return "distance";
    case components::PhysicsConstraintKind::Hinge: return "hinge";
    case components::PhysicsConstraintKind::Slider: return "slider";
    case components::PhysicsConstraintKind::Cone: return "cone";
    case components::PhysicsConstraintKind::SwingTwist: return "swing_twist";
    case components::PhysicsConstraintKind::SixDof: return "six_dof";
  }
  return "fixed";
}

bool readConstraintKind(const Json& json,
                        components::PhysicsConstraintKind& out) {
  const auto it = json.find("kind");
  if (it == json.end()) return true;
  if (!it->is_string()) return false;
  const std::string value = it->get<std::string>();
  if (value == "fixed") out = components::PhysicsConstraintKind::Fixed;
  else if (value == "point") out = components::PhysicsConstraintKind::Point;
  else if (value == "distance") out = components::PhysicsConstraintKind::Distance;
  else if (value == "hinge") out = components::PhysicsConstraintKind::Hinge;
  else if (value == "slider") out = components::PhysicsConstraintKind::Slider;
  else if (value == "cone") out = components::PhysicsConstraintKind::Cone;
  else if (value == "swing_twist") out = components::PhysicsConstraintKind::SwingTwist;
  else if (value == "six_dof") out = components::PhysicsConstraintKind::SixDof;
  else return false;
  return true;
}

const char* constraintSpaceName(
    components::PhysicsConstraintFrameSpace space) {
  return space == components::PhysicsConstraintFrameSpace::LocalToBodyCenterOfMass
             ? "local_to_body_center_of_mass"
             : "world";
}

bool readConstraintSpace(const Json& json,
                         components::PhysicsConstraintFrameSpace& out) {
  const auto it = json.find("space");
  if (it == json.end()) return true;
  if (!it->is_string()) return false;
  const std::string value = it->get<std::string>();
  if (value == "world") out = components::PhysicsConstraintFrameSpace::World;
  else if (value == "local_to_body_center_of_mass") {
    out = components::PhysicsConstraintFrameSpace::LocalToBodyCenterOfMass;
  } else return false;
  return true;
}

const char* constraintSpringModeName(
    components::PhysicsConstraintSpringMode mode) {
  return mode == components::PhysicsConstraintSpringMode::StiffnessAndDamping
             ? "stiffness_and_damping"
             : "frequency_and_damping";
}

Json serializeConstraintSpring(
    const components::PhysicsConstraintSpring& spring) {
  return Json{{"mode", constraintSpringModeName(spring.mode)},
              {"frequency_or_stiffness", spring.frequency_or_stiffness},
              {"damping", spring.damping}};
}

bool deserializeConstraintSpring(
    const Json& json,
    components::PhysicsConstraintSpring& out) {
  if (!json.is_object()) return false;
  std::string mode = constraintSpringModeName(out.mode);
  if (!readString(json, "mode", mode) ||
      !readFloat(json,
                 "frequency_or_stiffness",
                 out.frequency_or_stiffness) ||
      !readFloat(json, "damping", out.damping) ||
      out.frequency_or_stiffness < 0.0f || out.damping < 0.0f) {
    return false;
  }
  if (mode == "frequency_and_damping") {
    out.mode = components::PhysicsConstraintSpringMode::FrequencyAndDamping;
  } else if (mode == "stiffness_and_damping") {
    out.mode = components::PhysicsConstraintSpringMode::StiffnessAndDamping;
  } else {
    return false;
  }
  return true;
}

template <size_t Size>
Json floatArrayJson(const std::array<float, Size>& values) {
  Json out = Json::array();
  for (float value : values) out.push_back(value);
  return out;
}

template <size_t Size>
bool readFloatArray(const Json& object,
                    std::string_view key,
                    std::array<float, Size>& out) {
  const auto it = object.find(key);
  if (it == object.end()) return true;
  if (!it->is_array() || it->size() != Size) return false;
  for (size_t index = 0; index < Size; ++index) {
    if (!readFloatValue((*it)[index], out[index])) return false;
  }
  return true;
}

Json serializePhysicsConstraint(
    const components::PhysicsConstraintComponent& component,
    const ComponentSerializationContext& context) {
  return Json{
      {"body_a", serializeEntityReference(component.body_a, context)},
      {"body_b", serializeEntityReference(component.body_b, context)},
      {"kind", constraintKindName(component.kind)},
      {"space", constraintSpaceName(component.space)},
      {"enabled", component.enabled},
      {"priority", component.priority},
      {"velocity_solver_steps", component.velocity_solver_steps},
      {"position_solver_steps", component.position_solver_steps},
      {"draw_size", component.draw_size},
      {"user_data", component.user_data},
      {"auto_detect_point", component.auto_detect_point},
      {"point1", toJson(component.point1)},
      {"point2", toJson(component.point2)},
      {"axis1", toJson(component.axis1)},
      {"axis2", toJson(component.axis2)},
      {"normal1", toJson(component.normal1)},
      {"normal2", toJson(component.normal2)},
      {"plane_axis1", toJson(component.plane_axis1)},
      {"plane_axis2", toJson(component.plane_axis2)},
      {"min_distance", component.min_distance},
      {"max_distance", component.max_distance},
      {"limits_min", component.limits_min},
      {"limits_max", component.limits_max},
      {"half_cone_angle", component.half_cone_angle},
      {"normal_half_cone_angle", component.normal_half_cone_angle},
      {"plane_half_cone_angle", component.plane_half_cone_angle},
      {"twist_min_angle", component.twist_min_angle},
      {"twist_max_angle", component.twist_max_angle},
      {"max_friction_force", component.max_friction_force},
      {"max_friction_torque", component.max_friction_torque},
      {"limit_spring", serializeConstraintSpring(component.limit_spring)},
      {"six_dof_min_limits", floatArrayJson(component.six_dof_min_limits)},
      {"six_dof_max_limits", floatArrayJson(component.six_dof_max_limits)},
      {"six_dof_max_friction", floatArrayJson(component.six_dof_max_friction)},
  };
}

std::optional<components::PhysicsConstraintComponent>
deserializePhysicsConstraint(
    const Json& json,
    const ComponentSerializationContext& context) {
  if (!json.is_object()) return std::nullopt;
  components::PhysicsConstraintComponent component{};
  if (!readEntityReference(json, "body_a", component.body_a, context) ||
      !readEntityReference(json, "body_b", component.body_b, context) ||
      !readConstraintKind(json, component.kind) ||
      !readConstraintSpace(json, component.space) ||
      !readBool(json, "enabled", component.enabled) ||
      !readUint32(json, "priority", component.priority) ||
      !readUint32(json,
                  "velocity_solver_steps",
                  component.velocity_solver_steps) ||
      !readUint32(json,
                  "position_solver_steps",
                  component.position_solver_steps) ||
      !readFloat(json, "draw_size", component.draw_size) ||
      !readUint64(json, "user_data", component.user_data) ||
      !readBool(json, "auto_detect_point", component.auto_detect_point) ||
      !readVec3(json, "point1", component.point1) ||
      !readVec3(json, "point2", component.point2) ||
      !readVec3(json, "axis1", component.axis1) ||
      !readVec3(json, "axis2", component.axis2) ||
      !readVec3(json, "normal1", component.normal1) ||
      !readVec3(json, "normal2", component.normal2) ||
      !readVec3(json, "plane_axis1", component.plane_axis1) ||
      !readVec3(json, "plane_axis2", component.plane_axis2) ||
      !readFloat(json, "min_distance", component.min_distance) ||
      !readFloat(json, "max_distance", component.max_distance) ||
      !readFloat(json, "limits_min", component.limits_min) ||
      !readFloat(json, "limits_max", component.limits_max) ||
      !readFloat(json, "half_cone_angle", component.half_cone_angle) ||
      !readFloat(json,
                 "normal_half_cone_angle",
                 component.normal_half_cone_angle) ||
      !readFloat(json,
                 "plane_half_cone_angle",
                 component.plane_half_cone_angle) ||
      !readFloat(json, "twist_min_angle", component.twist_min_angle) ||
      !readFloat(json, "twist_max_angle", component.twist_max_angle) ||
      !readFloat(json,
                 "max_friction_force",
                 component.max_friction_force) ||
      !readFloat(json,
                 "max_friction_torque",
                 component.max_friction_torque) ||
      !readFloatArray(json,
                      "six_dof_min_limits",
                      component.six_dof_min_limits) ||
      !readFloatArray(json,
                      "six_dof_max_limits",
                      component.six_dof_max_limits) ||
      !readFloatArray(json,
                      "six_dof_max_friction",
                      component.six_dof_max_friction) ||
      component.draw_size < 0.0f ||
      component.max_friction_force < 0.0f ||
      component.max_friction_torque < 0.0f) {
    return std::nullopt;
  }
  if (const auto spring_it = json.find("limit_spring");
      spring_it != json.end() &&
      !deserializeConstraintSpring(*spring_it, component.limit_spring)) {
    return std::nullopt;
  }
  for (size_t index = 0; index < component.six_dof_min_limits.size(); ++index) {
    if (component.six_dof_min_limits[index] >
            component.six_dof_max_limits[index] ||
        component.six_dof_max_friction[index] < 0.0f) {
      return std::nullopt;
    }
  }
  return component;
}

const char* softBodyPresetName(components::PhysicsSoftBodyPresetKind preset) {
  switch (preset) {
    case components::PhysicsSoftBodyPresetKind::Custom: return "custom";
    case components::PhysicsSoftBodyPresetKind::Cloth: return "cloth";
    case components::PhysicsSoftBodyPresetKind::Cube: return "cube";
    case components::PhysicsSoftBodyPresetKind::Sphere: return "sphere";
  }
  return "custom";
}

bool readSoftBodyPreset(const Json& json,
                        components::PhysicsSoftBodyPresetKind& out) {
  const auto it = json.find("preset");
  if (it == json.end()) return true;
  if (!it->is_string()) return false;
  const std::string value = it->get<std::string>();
  if (value == "custom") out = components::PhysicsSoftBodyPresetKind::Custom;
  else if (value == "cloth") out = components::PhysicsSoftBodyPresetKind::Cloth;
  else if (value == "cube") out = components::PhysicsSoftBodyPresetKind::Cube;
  else if (value == "sphere") out = components::PhysicsSoftBodyPresetKind::Sphere;
  else return false;
  return true;
}

const char* softBodyBendName(components::PhysicsSoftBodyBendKind kind) {
  switch (kind) {
    case components::PhysicsSoftBodyBendKind::None: return "none";
    case components::PhysicsSoftBodyBendKind::Distance: return "distance";
    case components::PhysicsSoftBodyBendKind::Dihedral: return "dihedral";
  }
  return "distance";
}

const char* softBodyLraName(components::PhysicsSoftBodyLraKind kind) {
  switch (kind) {
    case components::PhysicsSoftBodyLraKind::None: return "none";
    case components::PhysicsSoftBodyLraKind::EuclideanDistance: return "euclidean_distance";
    case components::PhysicsSoftBodyLraKind::GeodesicDistance: return "geodesic_distance";
  }
  return "none";
}

Json serializePhysicsSoftBody(
    const components::PhysicsSoftBodyComponent& component) {
  Json vertices = Json::array();
  for (const auto& vertex : component.vertices) {
    vertices.push_back(Json{{"position", toJson(vertex.position)},
                            {"velocity", toJson(vertex.velocity)},
                            {"inverse_mass", vertex.inverse_mass}});
  }
  Json faces = Json::array();
  for (const auto& face : component.faces) {
    faces.push_back(Json{{"vertex0", face.vertex0},
                         {"vertex1", face.vertex1},
                         {"vertex2", face.vertex2},
                         {"material_index", face.material_index}});
  }
  Json edges = Json::array();
  for (const auto& edge : component.edges) {
    edges.push_back(Json{{"vertex0", edge.vertex0},
                         {"vertex1", edge.vertex1},
                         {"compliance", edge.compliance}});
  }
  Json volumes = Json::array();
  for (const auto& volume : component.volumes) {
    volumes.push_back(Json{{"vertex0", volume.vertex0},
                           {"vertex1", volume.vertex1},
                           {"vertex2", volume.vertex2},
                           {"vertex3", volume.vertex3},
                           {"compliance", volume.compliance}});
  }
  Json pinned = Json::array();
  for (uint32_t vertex : component.pinned_vertices) pinned.push_back(vertex);
  return Json{
      {"enabled", component.enabled},
      {"preset", softBodyPresetName(component.preset)},
      {"user_data", component.user_data},
      {"vertices", std::move(vertices)},
      {"faces", std::move(faces)},
      {"edges", std::move(edges)},
      {"volumes", std::move(volumes)},
      {"pinned_vertices", std::move(pinned)},
      {"grid_size_x", component.grid_size_x},
      {"grid_size_y", component.grid_size_y},
      {"grid_size_z", component.grid_size_z},
      {"grid_spacing", component.grid_spacing},
      {"radius", component.radius},
      {"sphere_theta", component.sphere_theta},
      {"sphere_phi", component.sphere_phi},
      {"pin_cloth_corners", component.pin_cloth_corners},
      {"create_constraints", component.create_constraints},
      {"optimize", component.optimize},
      {"bend_type", softBodyBendName(component.bend_type)},
      {"vertex_attributes",
       Json{{"compliance", component.vertex_attributes.compliance},
            {"shear_compliance",
             component.vertex_attributes.shear_compliance},
            {"bend_compliance",
             component.vertex_attributes.bend_compliance},
            {"lra_type",
             softBodyLraName(component.vertex_attributes.lra_type)},
            {"lra_max_distance_multiplier",
             component.vertex_attributes.lra_max_distance_multiplier}}},
      {"angle_tolerance", component.angle_tolerance},
      {"vertex_radius", component.vertex_radius},
      {"friction", component.friction},
      {"restitution", component.restitution},
      {"collision_layers", component.collision_layers},
      {"collides_with", component.collides_with},
      {"solver_iterations", component.solver_iterations},
      {"linear_damping", component.linear_damping},
      {"max_linear_velocity", component.max_linear_velocity},
      {"pressure", component.pressure},
      {"gravity_factor", component.gravity_factor},
      {"update_position", component.update_position},
      {"make_rotation_identity", component.make_rotation_identity},
      {"allow_sleeping", component.allow_sleeping},
      {"activate", component.activate},
  };
}

std::optional<components::PhysicsSoftBodyComponent>
deserializePhysicsSoftBody(const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::PhysicsSoftBodyComponent component{};
  std::string bend_type = softBodyBendName(component.bend_type);
  if (!readBool(json, "enabled", component.enabled) ||
      !readSoftBodyPreset(json, component.preset) ||
      !readUint64(json, "user_data", component.user_data) ||
      !readUint32(json, "grid_size_x", component.grid_size_x) ||
      !readUint32(json, "grid_size_y", component.grid_size_y) ||
      !readUint32(json, "grid_size_z", component.grid_size_z) ||
      !readFloat(json, "grid_spacing", component.grid_spacing) ||
      !readFloat(json, "radius", component.radius) ||
      !readUint32(json, "sphere_theta", component.sphere_theta) ||
      !readUint32(json, "sphere_phi", component.sphere_phi) ||
      !readBool(json, "pin_cloth_corners", component.pin_cloth_corners) ||
      !readBool(json,
                "create_constraints",
                component.create_constraints) ||
      !readBool(json, "optimize", component.optimize) ||
      !readString(json, "bend_type", bend_type) ||
      !readFloat(json, "angle_tolerance", component.angle_tolerance) ||
      !readFloat(json, "vertex_radius", component.vertex_radius) ||
      !readFloat(json, "friction", component.friction) ||
      !readFloat(json, "restitution", component.restitution) ||
      !readUint32(json, "collision_layers", component.collision_layers) ||
      !readUint32(json, "collides_with", component.collides_with) ||
      !readUint32(json, "solver_iterations", component.solver_iterations) ||
      !readFloat(json, "linear_damping", component.linear_damping) ||
      !readFloat(json,
                 "max_linear_velocity",
                 component.max_linear_velocity) ||
      !readFloat(json, "pressure", component.pressure) ||
      !readFloat(json, "gravity_factor", component.gravity_factor) ||
      !readBool(json, "update_position", component.update_position) ||
      !readBool(json,
                "make_rotation_identity",
                component.make_rotation_identity) ||
      !readBool(json, "allow_sleeping", component.allow_sleeping) ||
      !readBool(json, "activate", component.activate)) {
    return std::nullopt;
  }
  if (bend_type == "none") component.bend_type = components::PhysicsSoftBodyBendKind::None;
  else if (bend_type == "distance") component.bend_type = components::PhysicsSoftBodyBendKind::Distance;
  else if (bend_type == "dihedral") component.bend_type = components::PhysicsSoftBodyBendKind::Dihedral;
  else return std::nullopt;

  if (const auto attrs_it = json.find("vertex_attributes");
      attrs_it != json.end()) {
    if (!attrs_it->is_object()) return std::nullopt;
    std::string lra_type = softBodyLraName(component.vertex_attributes.lra_type);
    if (!readFloat(*attrs_it,
                   "compliance",
                   component.vertex_attributes.compliance) ||
        !readFloat(*attrs_it,
                   "shear_compliance",
                   component.vertex_attributes.shear_compliance) ||
        !readFloat(*attrs_it,
                   "bend_compliance",
                   component.vertex_attributes.bend_compliance) ||
        !readString(*attrs_it, "lra_type", lra_type) ||
        !readFloat(*attrs_it,
                   "lra_max_distance_multiplier",
                   component.vertex_attributes.lra_max_distance_multiplier)) {
      return std::nullopt;
    }
    if (lra_type == "none") component.vertex_attributes.lra_type = components::PhysicsSoftBodyLraKind::None;
    else if (lra_type == "euclidean_distance") component.vertex_attributes.lra_type = components::PhysicsSoftBodyLraKind::EuclideanDistance;
    else if (lra_type == "geodesic_distance") component.vertex_attributes.lra_type = components::PhysicsSoftBodyLraKind::GeodesicDistance;
    else return std::nullopt;
  }
  if (const auto vertices_it = json.find("vertices");
      vertices_it != json.end()) {
    if (!vertices_it->is_array()) return std::nullopt;
    for (const Json& entry : *vertices_it) {
      if (!entry.is_object()) return std::nullopt;
      components::PhysicsSoftBodyVertex vertex{};
      if (!readVec3(entry, "position", vertex.position) ||
          !readVec3(entry, "velocity", vertex.velocity) ||
          !readFloat(entry, "inverse_mass", vertex.inverse_mass) ||
          vertex.inverse_mass < 0.0f) {
        return std::nullopt;
      }
      component.vertices.push_back(vertex);
    }
  }
  auto valid_vertex = [&](uint32_t index) {
    return component.vertices.empty() || index < component.vertices.size();
  };
  if (const auto faces_it = json.find("faces"); faces_it != json.end()) {
    if (!faces_it->is_array()) return std::nullopt;
    for (const Json& entry : *faces_it) {
      if (!entry.is_object()) return std::nullopt;
      components::PhysicsSoftBodyFace face{};
      if (!readUint32(entry, "vertex0", face.vertex0) ||
          !readUint32(entry, "vertex1", face.vertex1) ||
          !readUint32(entry, "vertex2", face.vertex2) ||
          !readUint32(entry, "material_index", face.material_index) ||
          !valid_vertex(face.vertex0) || !valid_vertex(face.vertex1) ||
          !valid_vertex(face.vertex2)) {
        return std::nullopt;
      }
      component.faces.push_back(face);
    }
  }
  if (const auto edges_it = json.find("edges"); edges_it != json.end()) {
    if (!edges_it->is_array()) return std::nullopt;
    for (const Json& entry : *edges_it) {
      if (!entry.is_object()) return std::nullopt;
      components::PhysicsSoftBodyEdge edge{};
      if (!readUint32(entry, "vertex0", edge.vertex0) ||
          !readUint32(entry, "vertex1", edge.vertex1) ||
          !readFloat(entry, "compliance", edge.compliance) ||
          !valid_vertex(edge.vertex0) || !valid_vertex(edge.vertex1) ||
          edge.compliance < 0.0f) {
        return std::nullopt;
      }
      component.edges.push_back(edge);
    }
  }
  if (const auto volumes_it = json.find("volumes");
      volumes_it != json.end()) {
    if (!volumes_it->is_array()) return std::nullopt;
    for (const Json& entry : *volumes_it) {
      if (!entry.is_object()) return std::nullopt;
      components::PhysicsSoftBodyVolume volume{};
      if (!readUint32(entry, "vertex0", volume.vertex0) ||
          !readUint32(entry, "vertex1", volume.vertex1) ||
          !readUint32(entry, "vertex2", volume.vertex2) ||
          !readUint32(entry, "vertex3", volume.vertex3) ||
          !readFloat(entry, "compliance", volume.compliance) ||
          !valid_vertex(volume.vertex0) || !valid_vertex(volume.vertex1) ||
          !valid_vertex(volume.vertex2) || !valid_vertex(volume.vertex3) ||
          volume.compliance < 0.0f) {
        return std::nullopt;
      }
      component.volumes.push_back(volume);
    }
  }
  if (const auto pinned_it = json.find("pinned_vertices");
      pinned_it != json.end()) {
    if (!pinned_it->is_array()) return std::nullopt;
    for (const Json& entry : *pinned_it) {
      Json holder{{"value", entry}};
      uint32_t index = 0u;
      if (!readUint32(holder, "value", index) || !valid_vertex(index)) {
        return std::nullopt;
      }
      component.pinned_vertices.push_back(index);
    }
  }
  if (component.grid_size_x == 0u || component.grid_size_y == 0u ||
      component.grid_size_z == 0u || component.grid_spacing <= 0.0f ||
      component.radius <= 0.0f || component.sphere_theta < 3u ||
      component.sphere_phi < 2u || component.vertex_radius < 0.0f ||
      component.friction < 0.0f || component.restitution < 0.0f ||
      component.solver_iterations == 0u || component.linear_damping < 0.0f ||
      component.max_linear_velocity <= 0.0f ||
      component.vertex_attributes.lra_max_distance_multiplier < 0.0f) {
    return std::nullopt;
  }
  return component;
}

const char* vehicleControllerName(
    components::PhysicsVehicleControllerKind kind) {
  switch (kind) {
    case components::PhysicsVehicleControllerKind::Wheeled: return "wheeled";
    case components::PhysicsVehicleControllerKind::Motorcycle: return "motorcycle";
    case components::PhysicsVehicleControllerKind::Tracked: return "tracked";
  }
  return "wheeled";
}

const char* vehicleCollisionTesterName(
    components::PhysicsVehicleCollisionTesterKind kind) {
  switch (kind) {
    case components::PhysicsVehicleCollisionTesterKind::Ray: return "ray";
    case components::PhysicsVehicleCollisionTesterKind::SphereCast: return "sphere_cast";
    case components::PhysicsVehicleCollisionTesterKind::CylinderCast: return "cylinder_cast";
  }
  return "ray";
}

const char* vehicleSpringName(components::PhysicsVehicleSpringKind kind) {
  return kind == components::PhysicsVehicleSpringKind::StiffnessAndDamping
             ? "stiffness_and_damping"
             : "frequency_and_damping";
}

Json serializeVehicleCurve(
    const std::vector<components::PhysicsVehicleCurvePoint>& curve) {
  Json out = Json::array();
  for (const auto& point : curve) {
    out.push_back(Json{{"x", point.x}, {"y", point.y}});
  }
  return out;
}

bool deserializeVehicleCurve(
    const Json& json,
    std::vector<components::PhysicsVehicleCurvePoint>& out) {
  if (!json.is_array()) return false;
  std::vector<components::PhysicsVehicleCurvePoint> curve;
  curve.reserve(json.size());
  for (const Json& entry : json) {
    if (!entry.is_object()) return false;
    components::PhysicsVehicleCurvePoint point{};
    if (!readFloat(entry, "x", point.x) ||
        !readFloat(entry, "y", point.y)) {
      return false;
    }
    curve.push_back(point);
  }
  out = std::move(curve);
  return true;
}

Json serializeVehicleSpring(const components::PhysicsVehicleSpring& spring) {
  return Json{{"mode", vehicleSpringName(spring.mode)},
              {"frequency_or_stiffness", spring.frequency_or_stiffness},
              {"damping", spring.damping}};
}

bool deserializeVehicleSpring(const Json& json,
                              components::PhysicsVehicleSpring& out) {
  if (!json.is_object()) return false;
  std::string mode = vehicleSpringName(out.mode);
  if (!readString(json, "mode", mode) ||
      !readFloat(json,
                 "frequency_or_stiffness",
                 out.frequency_or_stiffness) ||
      !readFloat(json, "damping", out.damping) ||
      out.frequency_or_stiffness < 0.0f || out.damping < 0.0f) {
    return false;
  }
  if (mode == "frequency_and_damping") {
    out.mode = components::PhysicsVehicleSpringKind::FrequencyAndDamping;
  } else if (mode == "stiffness_and_damping") {
    out.mode = components::PhysicsVehicleSpringKind::StiffnessAndDamping;
  } else return false;
  return true;
}

Json serializeVehicleWheel(const components::PhysicsVehicleWheel& wheel) {
  return Json{
      {"position", toJson(wheel.position)},
      {"suspension_force_point", toJson(wheel.suspension_force_point)},
      {"suspension_direction", toJson(wheel.suspension_direction)},
      {"steering_axis", toJson(wheel.steering_axis)},
      {"wheel_up", toJson(wheel.wheel_up)},
      {"wheel_forward", toJson(wheel.wheel_forward)},
      {"suspension_min_length", wheel.suspension_min_length},
      {"suspension_max_length", wheel.suspension_max_length},
      {"suspension_preload_length", wheel.suspension_preload_length},
      {"suspension_spring", serializeVehicleSpring(wheel.suspension_spring)},
      {"radius", wheel.radius},
      {"width", wheel.width},
      {"enable_suspension_force_point", wheel.enable_suspension_force_point},
      {"inertia", wheel.inertia},
      {"angular_damping", wheel.angular_damping},
      {"max_steer_angle", wheel.max_steer_angle},
      {"longitudinal_friction",
       serializeVehicleCurve(wheel.longitudinal_friction)},
      {"lateral_friction", serializeVehicleCurve(wheel.lateral_friction)},
      {"max_brake_torque", wheel.max_brake_torque},
      {"max_hand_brake_torque", wheel.max_hand_brake_torque},
      {"tracked_longitudinal_friction", wheel.tracked_longitudinal_friction},
      {"tracked_lateral_friction", wheel.tracked_lateral_friction},
  };
}

bool deserializeVehicleWheel(const Json& json,
                             components::PhysicsVehicleWheel& wheel) {
  if (!json.is_object() ||
      !readVec3(json, "position", wheel.position) ||
      !readVec3(json,
                "suspension_force_point",
                wheel.suspension_force_point) ||
      !readVec3(json,
                "suspension_direction",
                wheel.suspension_direction) ||
      !readVec3(json, "steering_axis", wheel.steering_axis) ||
      !readVec3(json, "wheel_up", wheel.wheel_up) ||
      !readVec3(json, "wheel_forward", wheel.wheel_forward) ||
      !readFloat(json,
                 "suspension_min_length",
                 wheel.suspension_min_length) ||
      !readFloat(json,
                 "suspension_max_length",
                 wheel.suspension_max_length) ||
      !readFloat(json,
                 "suspension_preload_length",
                 wheel.suspension_preload_length) ||
      !readFloat(json, "radius", wheel.radius) ||
      !readFloat(json, "width", wheel.width) ||
      !readBool(json,
                "enable_suspension_force_point",
                wheel.enable_suspension_force_point) ||
      !readFloat(json, "inertia", wheel.inertia) ||
      !readFloat(json, "angular_damping", wheel.angular_damping) ||
      !readFloat(json, "max_steer_angle", wheel.max_steer_angle) ||
      !readFloat(json, "max_brake_torque", wheel.max_brake_torque) ||
      !readFloat(json,
                 "max_hand_brake_torque",
                 wheel.max_hand_brake_torque) ||
      !readFloat(json,
                 "tracked_longitudinal_friction",
                 wheel.tracked_longitudinal_friction) ||
      !readFloat(json,
                 "tracked_lateral_friction",
                 wheel.tracked_lateral_friction)) {
    return false;
  }
  if (const auto spring_it = json.find("suspension_spring");
      spring_it != json.end() &&
      !deserializeVehicleSpring(*spring_it, wheel.suspension_spring)) {
    return false;
  }
  if (const auto curve_it = json.find("longitudinal_friction");
      curve_it != json.end() &&
      !deserializeVehicleCurve(*curve_it, wheel.longitudinal_friction)) {
    return false;
  }
  if (const auto curve_it = json.find("lateral_friction");
      curve_it != json.end() &&
      !deserializeVehicleCurve(*curve_it, wheel.lateral_friction)) {
    return false;
  }
  return wheel.suspension_min_length >= 0.0f &&
         wheel.suspension_max_length >= wheel.suspension_min_length &&
         wheel.radius > 0.0f && wheel.width > 0.0f && wheel.inertia > 0.0f &&
         wheel.angular_damping >= 0.0f && wheel.max_brake_torque >= 0.0f &&
         wheel.max_hand_brake_torque >= 0.0f;
}

Json serializeVehicleEngine(const components::PhysicsVehicleEngine& engine) {
  return Json{{"max_torque", engine.max_torque},
              {"min_rpm", engine.min_rpm},
              {"max_rpm", engine.max_rpm},
              {"inertia", engine.inertia},
              {"angular_damping", engine.angular_damping},
              {"normalized_torque",
               serializeVehicleCurve(engine.normalized_torque)}};
}

bool deserializeVehicleEngine(const Json& json,
                              components::PhysicsVehicleEngine& engine) {
  if (!json.is_object() ||
      !readFloat(json, "max_torque", engine.max_torque) ||
      !readFloat(json, "min_rpm", engine.min_rpm) ||
      !readFloat(json, "max_rpm", engine.max_rpm) ||
      !readFloat(json, "inertia", engine.inertia) ||
      !readFloat(json, "angular_damping", engine.angular_damping)) {
    return false;
  }
  if (const auto curve_it = json.find("normalized_torque");
      curve_it != json.end() &&
      !deserializeVehicleCurve(*curve_it, engine.normalized_torque)) {
    return false;
  }
  return engine.max_torque >= 0.0f && engine.min_rpm >= 0.0f &&
         engine.max_rpm > engine.min_rpm && engine.inertia > 0.0f &&
         engine.angular_damping >= 0.0f;
}

const char* transmissionModeName(
    components::PhysicsVehicleTransmissionKind mode) {
  return mode == components::PhysicsVehicleTransmissionKind::Manual ? "manual"
                                                                     : "automatic";
}

Json serializeVehicleTransmission(
    const components::PhysicsVehicleTransmission& transmission) {
  return Json{
      {"mode", transmissionModeName(transmission.mode)},
      {"gear_ratios", floatVectorJson(transmission.gear_ratios)},
      {"reverse_gear_ratios",
       floatVectorJson(transmission.reverse_gear_ratios)},
      {"switch_time", transmission.switch_time},
      {"clutch_release_time", transmission.clutch_release_time},
      {"switch_latency", transmission.switch_latency},
      {"shift_up_rpm", transmission.shift_up_rpm},
      {"shift_down_rpm", transmission.shift_down_rpm},
      {"clutch_strength", transmission.clutch_strength},
  };
}

bool deserializeVehicleTransmission(
    const Json& json,
    components::PhysicsVehicleTransmission& transmission) {
  if (!json.is_object()) return false;
  std::string mode = transmissionModeName(transmission.mode);
  if (!readString(json, "mode", mode) ||
      !readFloatVector(json, "gear_ratios", transmission.gear_ratios) ||
      !readFloatVector(json,
                       "reverse_gear_ratios",
                       transmission.reverse_gear_ratios) ||
      !readFloat(json, "switch_time", transmission.switch_time) ||
      !readFloat(json,
                 "clutch_release_time",
                 transmission.clutch_release_time) ||
      !readFloat(json, "switch_latency", transmission.switch_latency) ||
      !readFloat(json, "shift_up_rpm", transmission.shift_up_rpm) ||
      !readFloat(json, "shift_down_rpm", transmission.shift_down_rpm) ||
      !readFloat(json, "clutch_strength", transmission.clutch_strength)) {
    return false;
  }
  if (mode == "automatic") transmission.mode = components::PhysicsVehicleTransmissionKind::Automatic;
  else if (mode == "manual") transmission.mode = components::PhysicsVehicleTransmissionKind::Manual;
  else return false;
  return !transmission.gear_ratios.empty() &&
         !transmission.reverse_gear_ratios.empty() &&
         transmission.switch_time >= 0.0f &&
         transmission.clutch_release_time >= 0.0f &&
         transmission.switch_latency >= 0.0f &&
         transmission.shift_up_rpm >= transmission.shift_down_rpm &&
         transmission.clutch_strength >= 0.0f;
}

Json serializeVehicleDifferential(
    const components::PhysicsVehicleDifferential& value) {
  return Json{{"left_wheel", value.left_wheel},
              {"right_wheel", value.right_wheel},
              {"differential_ratio", value.differential_ratio},
              {"left_right_split", value.left_right_split},
              {"limited_slip_ratio", value.limited_slip_ratio},
              {"engine_torque_ratio", value.engine_torque_ratio}};
}

bool deserializeVehicleDifferential(
    const Json& json,
    components::PhysicsVehicleDifferential& value) {
  int32_t left = value.left_wheel;
  int32_t right = value.right_wheel;
  if (!json.is_object() || !readInt32(json, "left_wheel", left) ||
      !readInt32(json, "right_wheel", right) ||
      !readFloat(json, "differential_ratio", value.differential_ratio) ||
      !readFloat(json, "left_right_split", value.left_right_split) ||
      !readFloat(json, "limited_slip_ratio", value.limited_slip_ratio) ||
      !readFloat(json, "engine_torque_ratio", value.engine_torque_ratio) ||
      value.differential_ratio <= 0.0f || value.left_right_split < 0.0f ||
      value.left_right_split > 1.0f || value.limited_slip_ratio < 0.0f ||
      value.engine_torque_ratio < 0.0f) {
    return false;
  }
  value.left_wheel = left;
  value.right_wheel = right;
  return true;
}

Json serializeVehicleTrack(const components::PhysicsVehicleTrack& track) {
  Json wheels = Json::array();
  for (uint32_t wheel : track.wheels) wheels.push_back(wheel);
  return Json{{"driven_wheel", track.driven_wheel},
              {"wheels", std::move(wheels)},
              {"inertia", track.inertia},
              {"angular_damping", track.angular_damping},
              {"max_brake_torque", track.max_brake_torque},
              {"differential_ratio", track.differential_ratio}};
}

bool deserializeVehicleTrack(const Json& json,
                             components::PhysicsVehicleTrack& track) {
  if (!json.is_object() ||
      !readUint32(json, "driven_wheel", track.driven_wheel) ||
      !readFloat(json, "inertia", track.inertia) ||
      !readFloat(json, "angular_damping", track.angular_damping) ||
      !readFloat(json, "max_brake_torque", track.max_brake_torque) ||
      !readFloat(json, "differential_ratio", track.differential_ratio)) {
    return false;
  }
  if (const auto wheels_it = json.find("wheels"); wheels_it != json.end()) {
    if (!wheels_it->is_array()) return false;
    track.wheels.clear();
    for (const Json& entry : *wheels_it) {
      Json holder{{"value", entry}};
      uint32_t wheel = 0u;
      if (!readUint32(holder, "value", wheel)) return false;
      track.wheels.push_back(wheel);
    }
  }
  return track.inertia > 0.0f && track.angular_damping >= 0.0f &&
         track.max_brake_torque >= 0.0f && track.differential_ratio > 0.0f;
}

Json serializePhysicsVehicle(
    const components::PhysicsVehicleComponent& component) {
  Json wheels = Json::array();
  for (const auto& wheel : component.wheels) {
    wheels.push_back(serializeVehicleWheel(wheel));
  }
  Json anti_roll_bars = Json::array();
  for (const auto& bar : component.anti_roll_bars) {
    anti_roll_bars.push_back(Json{{"left_wheel", bar.left_wheel},
                                  {"right_wheel", bar.right_wheel},
                                  {"stiffness", bar.stiffness}});
  }
  Json differentials = Json::array();
  for (const auto& differential : component.differentials) {
    differentials.push_back(serializeVehicleDifferential(differential));
  }
  Json tracks = Json::array();
  for (const auto& track : component.tracks) {
    tracks.push_back(serializeVehicleTrack(track));
  }
  return Json{
      {"enabled", component.enabled},
      {"controller", vehicleControllerName(component.controller)},
      {"collision_tester",
       vehicleCollisionTesterName(component.collision_tester)},
      {"up", toJson(component.up)},
      {"forward", toJson(component.forward)},
      {"max_pitch_roll_angle", component.max_pitch_roll_angle},
      {"collision_test_sphere_radius",
       component.collision_test_sphere_radius},
      {"collision_test_cylinder_convex_radius_fraction",
       component.collision_test_cylinder_convex_radius_fraction},
      {"collision_test_max_slope_angle",
       component.collision_test_max_slope_angle},
      {"collision_test_layer", component.collision_test_layer},
      {"num_steps_between_collision_test_active",
       component.num_steps_between_collision_test_active},
      {"num_steps_between_collision_test_inactive",
       component.num_steps_between_collision_test_inactive},
      {"override_gravity", component.override_gravity},
      {"gravity", toJson(component.gravity)},
      {"priority", component.priority},
      {"velocity_solver_steps", component.velocity_solver_steps},
      {"position_solver_steps", component.position_solver_steps},
      {"draw_size", component.draw_size},
      {"user_data", component.user_data},
      {"wheels", std::move(wheels)},
      {"anti_roll_bars", std::move(anti_roll_bars)},
      {"engine", serializeVehicleEngine(component.engine)},
      {"transmission", serializeVehicleTransmission(component.transmission)},
      {"differentials", std::move(differentials)},
      {"differential_limited_slip_ratio",
       component.differential_limited_slip_ratio},
      {"motorcycle",
       Json{{"max_lean_angle", component.motorcycle.max_lean_angle},
            {"lean_spring_constant",
             component.motorcycle.lean_spring_constant},
            {"lean_spring_damping",
             component.motorcycle.lean_spring_damping},
            {"lean_spring_integration_coefficient",
             component.motorcycle.lean_spring_integration_coefficient},
            {"lean_spring_integration_decay",
             component.motorcycle.lean_spring_integration_decay},
            {"lean_smoothing_factor",
             component.motorcycle.lean_smoothing_factor},
            {"enable_lean_controller",
             component.motorcycle.enable_lean_controller},
            {"enable_lean_steering_limit",
             component.motorcycle.enable_lean_steering_limit}}},
      {"tracks", std::move(tracks)},
  };
}

std::optional<components::PhysicsVehicleComponent> deserializePhysicsVehicle(
    const Json& json) {
  if (!json.is_object()) return std::nullopt;
  components::PhysicsVehicleComponent component{};
  std::string controller = vehicleControllerName(component.controller);
  std::string collision_tester =
      vehicleCollisionTesterName(component.collision_tester);
  if (!readBool(json, "enabled", component.enabled) ||
      !readString(json, "controller", controller) ||
      !readString(json, "collision_tester", collision_tester) ||
      !readVec3(json, "up", component.up) ||
      !readVec3(json, "forward", component.forward) ||
      !readFloat(json,
                 "max_pitch_roll_angle",
                 component.max_pitch_roll_angle) ||
      !readFloat(json,
                 "collision_test_sphere_radius",
                 component.collision_test_sphere_radius) ||
      !readFloat(json,
                 "collision_test_cylinder_convex_radius_fraction",
                 component.collision_test_cylinder_convex_radius_fraction) ||
      !readFloat(json,
                 "collision_test_max_slope_angle",
                 component.collision_test_max_slope_angle) ||
      !readUint32(json,
                  "collision_test_layer",
                  component.collision_test_layer) ||
      !readUint32(json,
                  "num_steps_between_collision_test_active",
                  component.num_steps_between_collision_test_active) ||
      !readUint32(json,
                  "num_steps_between_collision_test_inactive",
                  component.num_steps_between_collision_test_inactive) ||
      !readBool(json, "override_gravity", component.override_gravity) ||
      !readVec3(json, "gravity", component.gravity) ||
      !readUint32(json, "priority", component.priority) ||
      !readUint32(json,
                  "velocity_solver_steps",
                  component.velocity_solver_steps) ||
      !readUint32(json,
                  "position_solver_steps",
                  component.position_solver_steps) ||
      !readFloat(json, "draw_size", component.draw_size) ||
      !readUint64(json, "user_data", component.user_data) ||
      !readFloat(json,
                 "differential_limited_slip_ratio",
                 component.differential_limited_slip_ratio)) {
    return std::nullopt;
  }
  if (controller == "wheeled") component.controller = components::PhysicsVehicleControllerKind::Wheeled;
  else if (controller == "motorcycle") component.controller = components::PhysicsVehicleControllerKind::Motorcycle;
  else if (controller == "tracked") component.controller = components::PhysicsVehicleControllerKind::Tracked;
  else return std::nullopt;
  if (collision_tester == "ray") component.collision_tester = components::PhysicsVehicleCollisionTesterKind::Ray;
  else if (collision_tester == "sphere_cast") component.collision_tester = components::PhysicsVehicleCollisionTesterKind::SphereCast;
  else if (collision_tester == "cylinder_cast") component.collision_tester = components::PhysicsVehicleCollisionTesterKind::CylinderCast;
  else return std::nullopt;

  if (const auto wheels_it = json.find("wheels"); wheels_it != json.end()) {
    if (!wheels_it->is_array()) return std::nullopt;
    component.wheels.clear();
    for (const Json& entry : *wheels_it) {
      components::PhysicsVehicleWheel wheel{};
      if (!deserializeVehicleWheel(entry, wheel)) return std::nullopt;
      component.wheels.push_back(std::move(wheel));
    }
  }
  if (const auto bars_it = json.find("anti_roll_bars");
      bars_it != json.end()) {
    if (!bars_it->is_array()) return std::nullopt;
    component.anti_roll_bars.clear();
    for (const Json& entry : *bars_it) {
      if (!entry.is_object()) return std::nullopt;
      components::PhysicsVehicleAntiRollBar bar{};
      int32_t left = bar.left_wheel;
      int32_t right = bar.right_wheel;
      if (!readInt32(entry, "left_wheel", left) ||
          !readInt32(entry, "right_wheel", right) ||
          !readFloat(entry, "stiffness", bar.stiffness) ||
          left < 0 || right < 0 || bar.stiffness < 0.0f) {
        return std::nullopt;
      }
      bar.left_wheel = left;
      bar.right_wheel = right;
      component.anti_roll_bars.push_back(bar);
    }
  }
  if (const auto engine_it = json.find("engine");
      engine_it != json.end() &&
      !deserializeVehicleEngine(*engine_it, component.engine)) {
    return std::nullopt;
  }
  if (const auto transmission_it = json.find("transmission");
      transmission_it != json.end() &&
      !deserializeVehicleTransmission(*transmission_it,
                                      component.transmission)) {
    return std::nullopt;
  }
  if (const auto differentials_it = json.find("differentials");
      differentials_it != json.end()) {
    if (!differentials_it->is_array()) return std::nullopt;
    component.differentials.clear();
    for (const Json& entry : *differentials_it) {
      components::PhysicsVehicleDifferential differential{};
      if (!deserializeVehicleDifferential(entry, differential)) {
        return std::nullopt;
      }
      component.differentials.push_back(differential);
    }
  }
  if (const auto motorcycle_it = json.find("motorcycle");
      motorcycle_it != json.end()) {
    if (!motorcycle_it->is_object() ||
        !readFloat(*motorcycle_it,
                   "max_lean_angle",
                   component.motorcycle.max_lean_angle) ||
        !readFloat(*motorcycle_it,
                   "lean_spring_constant",
                   component.motorcycle.lean_spring_constant) ||
        !readFloat(*motorcycle_it,
                   "lean_spring_damping",
                   component.motorcycle.lean_spring_damping) ||
        !readFloat(*motorcycle_it,
                   "lean_spring_integration_coefficient",
                   component.motorcycle.lean_spring_integration_coefficient) ||
        !readFloat(*motorcycle_it,
                   "lean_spring_integration_decay",
                   component.motorcycle.lean_spring_integration_decay) ||
        !readFloat(*motorcycle_it,
                   "lean_smoothing_factor",
                   component.motorcycle.lean_smoothing_factor) ||
        !readBool(*motorcycle_it,
                  "enable_lean_controller",
                  component.motorcycle.enable_lean_controller) ||
        !readBool(*motorcycle_it,
                  "enable_lean_steering_limit",
                  component.motorcycle.enable_lean_steering_limit)) {
      return std::nullopt;
    }
  }
  if (const auto tracks_it = json.find("tracks"); tracks_it != json.end()) {
    if (!tracks_it->is_array() || tracks_it->size() != component.tracks.size()) {
      return std::nullopt;
    }
    for (size_t index = 0; index < component.tracks.size(); ++index) {
      if (!deserializeVehicleTrack((*tracks_it)[index],
                                   component.tracks[index])) {
        return std::nullopt;
      }
    }
  }
  if (component.max_pitch_roll_angle < 0.0f ||
      component.collision_test_sphere_radius <= 0.0f ||
      component.collision_test_cylinder_convex_radius_fraction < 0.0f ||
      component.collision_test_cylinder_convex_radius_fraction > 1.0f ||
      component.collision_test_max_slope_angle < 0.0f ||
      component.num_steps_between_collision_test_active == 0u ||
      component.num_steps_between_collision_test_inactive == 0u ||
      component.draw_size < 0.0f ||
      component.differential_limited_slip_ratio < 0.0f ||
      component.motorcycle.max_lean_angle < 0.0f ||
      component.motorcycle.lean_smoothing_factor < 0.0f ||
      component.motorcycle.lean_smoothing_factor > 1.0f) {
    return std::nullopt;
  }
  const auto valid_wheel_index = [&](int index) {
    return index < 0 || static_cast<size_t>(index) < component.wheels.size();
  };
  for (const auto& bar : component.anti_roll_bars) {
    if (!valid_wheel_index(bar.left_wheel) ||
        !valid_wheel_index(bar.right_wheel)) return std::nullopt;
  }
  for (const auto& differential : component.differentials) {
    if (!valid_wheel_index(differential.left_wheel) ||
        !valid_wheel_index(differential.right_wheel)) return std::nullopt;
  }
  for (const auto& track : component.tracks) {
    if (!component.wheels.empty() &&
        track.driven_wheel >= component.wheels.size()) return std::nullopt;
    for (uint32_t wheel : track.wheels) {
      if (wheel >= component.wheels.size()) return std::nullopt;
    }
  }
  return component;
}

}  // namespace

nlohmann::json serializeComponentPayload(
    const ComponentSerializer& serializer,
    const world::World& world,
    world::Entity entity,
    const ComponentSerializationContext& context) {
  if (serializer.serialize_with_context) {
    return serializer.serialize_with_context(world, entity, context);
  }
  return serializer.serialize(world, entity);
}

bool deserializeComponentPayload(
    const ComponentSerializer& serializer,
    world::World& world,
    world::Entity entity,
    const nlohmann::json& payload,
    const ComponentSerializationContext& context) {
  if (serializer.deserialize_with_context) {
    return serializer.deserialize_with_context(world, entity, payload, context);
  }
  return serializer.deserialize(world, entity, payload);
}

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
  registerComponent<components::StaticComponent>(
      registry, "StaticComponent", serializeStatic, deserializeStatic);
  registerComponent<components::AudioListenerComponent>(
      registry,
      "AudioListenerComponent",
      serializeAudioListener,
      deserializeAudioListener);
  registerComponent<components::AudioSourceComponent>(
      registry,
      "AudioSourceComponent",
      serializeAudioSource,
      deserializeAudioSource);
  registerComponent<components::CameraComponent>(
      registry, "CameraComponent", serializeCamera, deserializeCamera);
  registerComponent<components::EnvironmentComponent>(
      registry,
      "EnvironmentComponent",
      serializeEnvironment,
      deserializeEnvironment);
  registerRenderingAuthoringComponentSerializers(registry);
  registerComponent<components::AnimatorComponent>(
      registry, "AnimatorComponent", serializeAnimator, deserializeAnimator);
  registerComponent<components::RootMotionComponent>(
      registry,
      "RootMotionComponent",
      serializeRootMotion,
      deserializeRootMotion);
  registerContextualComponent<components::DeformableMeshComponent>(
      registry,
      "DeformableMeshComponent",
      serializeDeformableMesh,
      deserializeDeformableMesh);
  registerComponent<components::LightComponent>(
      registry, "LightComponent", serializeLight, deserializeLight);
  registerComponent<components::LightPulseComponent>(
      registry, "LightPulseComponent", serializeLightPulse, deserializeLightPulse);
  registerComponent<components::VisibilityComponent>(
      registry, "VisibilityComponent", serializeVisibility, deserializeVisibility);
  registerComponent<components::RenderTagsComponent>(
      registry, "RenderTagsComponent", serializeRenderTags, deserializeRenderTags);
  registerComponent<components::TerrainComponent>(
      registry, "TerrainComponent", serializeTerrain, deserializeTerrain);
  registerComponent<components::ColliderComponent>(
      registry, "ColliderComponent", serializeCollider, deserializeCollider);
  registerComponent<components::RigidbodyComponent>(
      registry, "RigidbodyComponent", serializeRigidbody, deserializeRigidbody);
  registerComponent<components::PhysicsMaterialComponent>(
      registry,
      "PhysicsMaterialComponent",
      serializePhysicsMaterial,
      deserializePhysicsMaterial);
  registerComponent<components::PhysicsCollisionFilterComponent>(
      registry,
      "PhysicsCollisionFilterComponent",
      serializePhysicsCollisionFilter,
      deserializePhysicsCollisionFilter);
  registerComponent<components::CharacterControllerComponent>(
      registry,
      "CharacterControllerComponent",
      serializeCharacterController,
      deserializeCharacterController);
  registerComponent<components::CollisionListenerComponent>(
      registry,
      "CollisionListenerComponent",
      serializeCollisionListener,
      deserializeCollisionListener);
  registerComponent<components::ContactListenerComponent>(
      registry,
      "ContactListenerComponent",
      serializeContactListener,
      deserializeContactListener);
  registerComponent<components::GroundContactComponent>(
      registry,
      "GroundContactComponent",
      serializeGroundContact,
      deserializeGroundContact);
  registerContextualComponent<components::PhysicsConstraintComponent>(
      registry,
      "PhysicsConstraintComponent",
      serializePhysicsConstraint,
      deserializePhysicsConstraint);
  registerComponent<components::PhysicsSoftBodyComponent>(
      registry,
      "PhysicsSoftBodyComponent",
      serializePhysicsSoftBody,
      deserializePhysicsSoftBody);
  registerComponent<components::PhysicsVehicleComponent>(
      registry,
      "PhysicsVehicleComponent",
      serializePhysicsVehicle,
      deserializePhysicsVehicle);
#if defined(KARMA_ENABLE_NAVIGATION)
  registerComponent<components::NavMeshSurfaceComponent>(
      registry,
      "NavMeshSurfaceComponent",
      serializeNavMeshSurface,
      deserializeNavMeshSurface);
  registerContextualComponent<components::NavOffMeshLinkComponent>(
      registry,
      "NavOffMeshLinkComponent",
      serializeNavOffMeshLink,
      deserializeNavOffMeshLink);
  registerComponent<components::NavConvexVolumeComponent>(
      registry,
      "NavConvexVolumeComponent",
      serializeNavConvexVolume,
      deserializeNavConvexVolume);
  registerComponent<components::NavMeshComponent>(
      registry, "NavMeshComponent", serializeNavMesh, deserializeNavMesh);
  registerComponent<components::NavCrowdComponent>(
      registry, "NavCrowdComponent", serializeNavCrowd, deserializeNavCrowd);
  registerContextualComponent<components::NavCrowdAgentComponent>(
      registry,
      "NavCrowdAgentComponent",
      serializeNavCrowdAgent,
      deserializeNavCrowdAgent);
  registerContextualComponent<components::NavMeshAgentComponent>(
      registry,
      "NavMeshAgentComponent",
      serializeNavMeshAgent,
      deserializeNavMeshAgent);
  registerComponent<components::NavTileCacheComponent>(
      registry,
      "NavTileCacheComponent",
      serializeNavTileCache,
      deserializeNavTileCache);
  registerContextualComponent<components::NavTileCacheObstacleComponent>(
      registry,
      "NavTileCacheObstacleComponent",
      serializeNavTileCacheObstacle,
      deserializeNavTileCacheObstacle);
#endif  // defined(KARMA_ENABLE_NAVIGATION)
  registerComponent<components::NetworkIdentityComponent>(
      registry,
      "NetworkIdentityComponent",
      serializeNetworkIdentity,
      deserializeNetworkIdentity);
  registerComponent<components::NetworkAuthorityComponent>(
      registry,
      "NetworkAuthorityComponent",
      serializeNetworkAuthority,
      deserializeNetworkAuthority);
  registerComponent<components::NetworkReplicatedComponent>(
      registry,
      "NetworkReplicatedComponent",
      serializeNetworkReplicated,
      deserializeNetworkReplicated);
  registerComponent<components::ScriptComponent>(
      registry, "ScriptComponent", serializeScript, deserializeScript);
  registerComponent<components::ParticleEffectComponent>(
      registry, "ParticleEffectComponent", serializeParticleEffect, deserializeParticleEffect);
  registerComponent<components::ParticleEffectOverrideComponent>(
      registry,
      "ParticleEffectOverrideComponent",
      serializeParticleEffectOverride,
      deserializeParticleEffectOverride);
  registerComponent<components::ParticleEmitterComponent>(
      registry, "ParticleEmitterComponent", serializeParticleEmitter, deserializeParticleEmitter);
  registerComponent<components::ParticleBeamComponent>(
      registry, "ParticleBeamComponent", serializeParticleBeam, deserializeParticleBeam);
  registerComponent<components::VolumetricComponent>(
      registry, "VolumetricComponent", serializeVolumetric, deserializeVolumetric);
}

void ensureBuiltinComponentSerializers() {
  ComponentSerializerRegistry& registry = componentSerializerRegistry();
  static const ComponentSerializerRegistry builtins = [] {
    ComponentSerializerRegistry value;
    registerBuiltinComponentSerializers(value);
    return value;
  }();
  for (const ComponentSerializer& serializer : builtins.serializers()) {
    if (registry.find(serializer.type_name) == nullptr) {
      registry.registerSerializer(serializer);
    }
  }
}

}  // namespace karma::prefabs
