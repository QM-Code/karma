#include "asset_cache_serializers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "karma/assets.h"

namespace karma::assets::detail {

namespace {

using Json = nlohmann::json;

constexpr std::array<char, 8> kMagic{'K', 'A', 'S', 'S', 'E', 'T', '0', '2'};
constexpr uint32_t kKindMaterialAsset = 3u;
constexpr uint32_t kKindMaterialVariant = 4u;
constexpr uint32_t kKindParticleEffect = 5u;
constexpr uint32_t kKindGltfScene = 6u;
constexpr uint32_t kKindAnimationClip = 7u;
constexpr uint32_t kKindSkeleton = 8u;
constexpr uint32_t kKindSkin = 9u;
constexpr uint32_t kChunkJson = 0x4a534f4eu;      // JSON
constexpr uint32_t kChunkAnimationClip = 0x414e494du;  // ANIM

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

void appendU64(std::vector<uint8_t>& out, uint64_t value) {
  for (uint32_t i = 0u; i < 8u; ++i) {
    out.push_back(static_cast<uint8_t>((value >> (i * 8u)) & 0xffu));
  }
}

void appendF32(std::vector<uint8_t>& out, float value) {
  static_assert(sizeof(float) == sizeof(uint32_t));
  uint32_t bits = 0u;
  std::memcpy(&bits, &value, sizeof(bits));
  appendU32(out, bits);
}

void appendU8(std::vector<uint8_t>& out, uint8_t value) {
  out.push_back(value);
}

bool readU32(const std::vector<uint8_t>& bytes, std::size_t& offset, uint32_t& out) {
  if (offset + 4u > bytes.size()) {
    return false;
  }
  out = static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1u]) << 8u) |
        (static_cast<uint32_t>(bytes[offset + 2u]) << 16u) |
        (static_cast<uint32_t>(bytes[offset + 3u]) << 24u);
  offset += 4u;
  return true;
}

bool readU64(const std::vector<uint8_t>& bytes, std::size_t& offset, uint64_t& out) {
  if (offset + 8u > bytes.size()) {
    return false;
  }
  out = 0u;
  for (uint32_t i = 0u; i < 8u; ++i) {
    out |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8u);
  }
  offset += 8u;
  return true;
}

bool readF32(const std::vector<uint8_t>& bytes, std::size_t& offset, float& out) {
  uint32_t bits = 0u;
  if (!readU32(bytes, offset, bits)) {
    return false;
  }
  std::memcpy(&out, &bits, sizeof(out));
  return true;
}

bool readU8(const std::vector<uint8_t>& bytes, std::size_t& offset, uint8_t& out) {
  if (offset + 1u > bytes.size()) {
    return false;
  }
  out = bytes[offset++];
  return true;
}

void appendChunk(std::vector<uint8_t>& out, uint32_t id, const std::vector<uint8_t>& payload) {
  appendU32(out, id);
  appendU64(out, static_cast<uint64_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

Json floatJson(float value) {
  if (std::isinf(value)) {
    return value > 0.0f ? Json("inf") : Json("-inf");
  }
  if (std::isnan(value)) {
    return Json("nan");
  }
  return value;
}

bool readFloatJson(const Json& value, float& out) {
  if (value.is_number()) {
    out = value.get<float>();
    return true;
  }
  if (!value.is_string()) {
    return false;
  }
  const std::string text = value.get<std::string>();
  if (text == "inf") {
    out = std::numeric_limits<float>::infinity();
    return true;
  }
  if (text == "-inf") {
    out = -std::numeric_limits<float>::infinity();
    return true;
  }
  if (text == "nan") {
    out = std::numeric_limits<float>::quiet_NaN();
    return true;
  }
  return false;
}

bool readCount(const std::vector<uint8_t>& bytes, std::size_t& offset, std::size_t& out) {
  uint64_t count = 0u;
  if (!readU64(bytes, offset, count) ||
      count > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return false;
  }
  out = static_cast<std::size_t>(count);
  return true;
}

void appendStringBinary(std::vector<uint8_t>& out, const std::string& value) {
  appendU64(out, static_cast<uint64_t>(value.size()));
  const auto* bytes = reinterpret_cast<const uint8_t*>(value.data());
  out.insert(out.end(), bytes, bytes + value.size());
}

bool readStringBinary(const std::vector<uint8_t>& bytes,
                      std::size_t& offset,
                      std::string& out) {
  std::size_t size = 0u;
  if (!readCount(bytes, offset, size) || size > bytes.size() - offset) {
    return false;
  }
  out.assign(reinterpret_cast<const char*>(bytes.data() + offset), size);
  offset += size;
  return true;
}

void appendMathVec3Binary(std::vector<uint8_t>& out, const math::Vec3& value) {
  appendF32(out, value.x);
  appendF32(out, value.y);
  appendF32(out, value.z);
}

bool readMathVec3Binary(const std::vector<uint8_t>& bytes,
                        std::size_t& offset,
                        math::Vec3& out) {
  return readF32(bytes, offset, out.x) &&
         readF32(bytes, offset, out.y) &&
         readF32(bytes, offset, out.z);
}

void appendQuatBinary(std::vector<uint8_t>& out, const math::Quat& value) {
  appendF32(out, value.x);
  appendF32(out, value.y);
  appendF32(out, value.z);
  appendF32(out, value.w);
}

bool readQuatBinary(const std::vector<uint8_t>& bytes,
                    std::size_t& offset,
                    math::Quat& out) {
  return readF32(bytes, offset, out.x) &&
         readF32(bytes, offset, out.y) &&
         readF32(bytes, offset, out.z) &&
         readF32(bytes, offset, out.w);
}

template <typename T, typename Writer>
void appendBinaryVector(std::vector<uint8_t>& out,
                        const std::vector<T>& values,
                        Writer writer) {
  appendU64(out, static_cast<uint64_t>(values.size()));
  for (const T& value : values) {
    writer(out, value);
  }
}

template <typename T, typename Reader>
bool readBinaryVector(const std::vector<uint8_t>& bytes,
                      std::size_t& offset,
                      std::vector<T>& out,
                      Reader reader) {
  std::size_t count = 0u;
  if (!readCount(bytes, offset, count)) {
    return false;
  }
  std::vector<T> parsed;
  parsed.reserve(count);
  for (std::size_t i = 0u; i < count; ++i) {
    T value{};
    if (!reader(bytes, offset, value)) {
      return false;
    }
    parsed.push_back(std::move(value));
  }
  out = std::move(parsed);
  return true;
}

Json vec2Json(const glm::vec2& value) {
  return Json::array({floatJson(value.x), floatJson(value.y)});
}

Json vec3Json(const glm::vec3& value) {
  return Json::array({floatJson(value.x), floatJson(value.y), floatJson(value.z)});
}

Json vec4Json(const glm::vec4& value) {
  return Json::array({floatJson(value.x), floatJson(value.y), floatJson(value.z), floatJson(value.w)});
}

Json uvec4Json(const glm::uvec4& value) {
  return Json::array({value.x, value.y, value.z, value.w});
}

Json mathVec3Json(const math::Vec3& value) {
  return Json::array({floatJson(value.x), floatJson(value.y), floatJson(value.z)});
}

Json quatJson(const math::Quat& value) {
  return Json::array({floatJson(value.x), floatJson(value.y), floatJson(value.z), floatJson(value.w)});
}

Json colorJson(const math::Color& value) {
  return Json::array({floatJson(value.r), floatJson(value.g), floatJson(value.b), floatJson(value.a)});
}

Json mat4Json(const glm::mat4& value) {
  Json out = Json::array();
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      out.push_back(floatJson(value[column][row]));
    }
  }
  return out;
}

bool readVec2Json(const Json& value, glm::vec2& out) {
  if (!value.is_array() || value.size() != 2u) {
    return false;
  }
  return readFloatJson(value[0], out.x) && readFloatJson(value[1], out.y);
}

bool readVec3Json(const Json& value, glm::vec3& out) {
  if (!value.is_array() || value.size() != 3u) {
    return false;
  }
  return readFloatJson(value[0], out.x) &&
         readFloatJson(value[1], out.y) &&
         readFloatJson(value[2], out.z);
}

bool readVec4Json(const Json& value, glm::vec4& out) {
  if (!value.is_array() || value.size() != 4u) {
    return false;
  }
  return readFloatJson(value[0], out.x) &&
         readFloatJson(value[1], out.y) &&
         readFloatJson(value[2], out.z) &&
         readFloatJson(value[3], out.w);
}

bool readUvec4Json(const Json& value, glm::uvec4& out) {
  if (!value.is_array() || value.size() != 4u) {
    return false;
  }
  for (const Json& element : value) {
    if (!element.is_number_unsigned() && !element.is_number_integer()) {
      return false;
    }
  }
  out = glm::uvec4(value[0].get<uint32_t>(),
                   value[1].get<uint32_t>(),
                   value[2].get<uint32_t>(),
                   value[3].get<uint32_t>());
  return true;
}

bool readMathVec3Json(const Json& value, math::Vec3& out) {
  if (!value.is_array() || value.size() != 3u) {
    return false;
  }
  return readFloatJson(value[0], out.x) &&
         readFloatJson(value[1], out.y) &&
         readFloatJson(value[2], out.z);
}

bool readQuatJson(const Json& value, math::Quat& out) {
  if (!value.is_array() || value.size() != 4u) {
    return false;
  }
  return readFloatJson(value[0], out.x) &&
         readFloatJson(value[1], out.y) &&
         readFloatJson(value[2], out.z) &&
         readFloatJson(value[3], out.w);
}

bool readColorJson(const Json& value, math::Color& out) {
  if (!value.is_array() || value.size() != 4u) {
    return false;
  }
  return readFloatJson(value[0], out.r) &&
         readFloatJson(value[1], out.g) &&
         readFloatJson(value[2], out.b) &&
         readFloatJson(value[3], out.a);
}

bool readMat4Json(const Json& value, glm::mat4& out) {
  if (!value.is_array() || value.size() != 16u) {
    return false;
  }
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      if (!readFloatJson(value[static_cast<std::size_t>(column * 4 + row)], out[column][row])) {
        return false;
      }
    }
  }
  return true;
}

template <typename T, typename Writer>
Json vectorJson(const std::vector<T>& values, Writer writer) {
  Json out = Json::array();
  for (const T& value : values) {
    out.push_back(writer(value));
  }
  return out;
}

template <typename T, typename Reader>
bool readVectorJson(const Json& value, std::vector<T>& out, Reader reader) {
  if (!value.is_array()) {
    return false;
  }
  std::vector<T> parsed;
  parsed.reserve(value.size());
  for (const Json& element : value) {
    T item{};
    if (!reader(element, item)) {
      return false;
    }
    parsed.push_back(std::move(item));
  }
  out = std::move(parsed);
  return true;
}

Json stringVectorJson(const std::vector<std::string>& values) {
  Json out = Json::array();
  for (const std::string& value : values) {
    out.push_back(value);
  }
  return out;
}

bool readStringVectorJson(const Json& value, std::vector<std::string>& out) {
  if (!value.is_array()) {
    return false;
  }
  std::vector<std::string> parsed;
  parsed.reserve(value.size());
  for (const Json& element : value) {
    if (!element.is_string()) {
      return false;
    }
    parsed.push_back(element.get<std::string>());
  }
  out = std::move(parsed);
  return true;
}

Json u32VectorJson(const std::vector<uint32_t>& values) {
  Json out = Json::array();
  for (uint32_t value : values) {
    out.push_back(value);
  }
  return out;
}

bool readU32VectorJson(const Json& value, std::vector<uint32_t>& out) {
  if (!value.is_array()) {
    return false;
  }
  std::vector<uint32_t> parsed;
  parsed.reserve(value.size());
  for (const Json& element : value) {
    if (!element.is_number_unsigned() && !element.is_number_integer()) {
      return false;
    }
    parsed.push_back(element.get<uint32_t>());
  }
  out = std::move(parsed);
  return true;
}

Json floatVectorJson(const std::vector<float>& values) {
  Json out = Json::array();
  for (float value : values) {
    out.push_back(floatJson(value));
  }
  return out;
}

bool readFloatVectorJson(const Json& value, std::vector<float>& out) {
  if (!value.is_array()) {
    return false;
  }
  std::vector<float> parsed;
  parsed.reserve(value.size());
  for (const Json& element : value) {
    float item = 0.0f;
    if (!readFloatJson(element, item)) {
      return false;
    }
    parsed.push_back(item);
  }
  out = std::move(parsed);
  return true;
}

Json materialDescJson(const rendering::MaterialDesc& material) {
  return Json{
      {"base_color", colorJson(material.base_color)},
      {"emissive_color", colorJson(material.emissive_color)},
      {"metallic", floatJson(material.metallic)},
      {"roughness", floatJson(material.roughness)},
      {"normal_scale", floatJson(material.normal_scale)},
      {"occlusion_strength", floatJson(material.occlusion_strength)},
      {"emissive_strength", floatJson(material.emissive_strength)},
      {"clearcoat", floatJson(material.clearcoat)},
      {"clearcoat_roughness", floatJson(material.clearcoat_roughness)},
      {"sheen_color", colorJson(material.sheen_color)},
      {"sheen_roughness", floatJson(material.sheen_roughness)},
      {"anisotropy", floatJson(material.anisotropy)},
      {"transmission", floatJson(material.transmission)},
      {"ior", floatJson(material.ior)},
      {"thickness", floatJson(material.thickness)},
      {"attenuation_distance", floatJson(material.attenuation_distance)},
      {"attenuation_color", colorJson(material.attenuation_color)},
      {"analytic_sphere_normals", material.analytic_sphere_normals},
      {"unlit", material.unlit},
      {"alpha_mode", static_cast<uint32_t>(material.alpha_mode)},
      {"alpha_cutoff", floatJson(material.alpha_cutoff)},
      {"alpha_softness", floatJson(material.alpha_softness)},
      {"alpha_dither", material.alpha_dither},
      {"alpha_to_coverage", material.alpha_to_coverage},
      {"transparent", material.transparent},
      {"blend_mode", static_cast<uint32_t>(material.blend_mode)},
      {"depth_test", material.depth_test},
      {"depth_write", material.depth_write},
      {"wireframe", material.wireframe},
      {"double_sided", material.double_sided},
  };
}

bool readMaterialDescJson(const Json& json, rendering::MaterialDesc& material) {
  rendering::MaterialDesc parsed{};
  auto read_float_field = [&](const char* key, float& out) {
    const auto it = json.find(key);
    return it == json.end() || readFloatJson(*it, out);
  };
  auto read_color_field = [&](const char* key, math::Color& out) {
    const auto it = json.find(key);
    return it == json.end() || readColorJson(*it, out);
  };
  if (!json.is_object() ||
      !read_color_field("base_color", parsed.base_color) ||
      !read_color_field("emissive_color", parsed.emissive_color) ||
      !read_float_field("metallic", parsed.metallic) ||
      !read_float_field("roughness", parsed.roughness) ||
      !read_float_field("normal_scale", parsed.normal_scale) ||
      !read_float_field("occlusion_strength", parsed.occlusion_strength) ||
      !read_float_field("emissive_strength", parsed.emissive_strength) ||
      !read_float_field("clearcoat", parsed.clearcoat) ||
      !read_float_field("clearcoat_roughness", parsed.clearcoat_roughness) ||
      !read_color_field("sheen_color", parsed.sheen_color) ||
      !read_float_field("sheen_roughness", parsed.sheen_roughness) ||
      !read_float_field("anisotropy", parsed.anisotropy) ||
      !read_float_field("transmission", parsed.transmission) ||
      !read_float_field("ior", parsed.ior) ||
      !read_float_field("thickness", parsed.thickness) ||
      !read_float_field("attenuation_distance", parsed.attenuation_distance) ||
      !read_color_field("attenuation_color", parsed.attenuation_color)) {
    return false;
  }
  parsed.analytic_sphere_normals = json.value("analytic_sphere_normals", parsed.analytic_sphere_normals);
  parsed.unlit = json.value("unlit", parsed.unlit);
  parsed.alpha_mode = static_cast<rendering::MaterialDesc::AlphaMode>(
      json.value("alpha_mode", static_cast<uint32_t>(parsed.alpha_mode)));
  if (!read_float_field("alpha_cutoff", parsed.alpha_cutoff) ||
      !read_float_field("alpha_softness", parsed.alpha_softness)) {
    return false;
  }
  parsed.alpha_dither = json.value("alpha_dither", parsed.alpha_dither);
  parsed.alpha_to_coverage = json.value("alpha_to_coverage", parsed.alpha_to_coverage);
  parsed.transparent = json.value("transparent", parsed.transparent);
  if (parsed.transparent &&
      parsed.alpha_mode == rendering::MaterialDesc::AlphaMode::Opaque) {
    parsed.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
  }
  parsed.blend_mode = static_cast<rendering::MaterialDesc::BlendMode>(
      json.value("blend_mode", static_cast<uint32_t>(parsed.blend_mode)));
  parsed.depth_test = json.value("depth_test", parsed.depth_test);
  parsed.depth_write = json.value("depth_write", parsed.depth_write);
  parsed.wireframe = json.value("wireframe", parsed.wireframe);
  parsed.double_sided = json.value("double_sided", parsed.double_sided);
  material = parsed;
  return true;
}

Json pipelineJson(const rendering::MaterialPipelineDesc& pipeline) {
  return Json{
      {"name", pipeline.name},
      {"vertex_shader_path", pipeline.vertex_shader_path.generic_string()},
      {"fragment_shader_path", pipeline.fragment_shader_path.generic_string()},
      {"vertex_entry_point", pipeline.vertex_entry_point},
      {"fragment_entry_point", pipeline.fragment_entry_point},
      {"defines", stringVectorJson(pipeline.defines)},
  };
}

bool readPipelineJson(const Json& json, rendering::MaterialPipelineDesc& pipeline) {
  if (!json.is_object()) {
    return false;
  }
  rendering::MaterialPipelineDesc parsed{};
  parsed.name = json.value("name", parsed.name);
  parsed.vertex_shader_path = json.value("vertex_shader_path", std::string{});
  parsed.fragment_shader_path = json.value("fragment_shader_path", std::string{});
  parsed.vertex_entry_point = json.value("vertex_entry_point", parsed.vertex_entry_point);
  parsed.fragment_entry_point = json.value("fragment_entry_point", parsed.fragment_entry_point);
  if (const auto it = json.find("defines"); it != json.end() &&
      !readStringVectorJson(*it, parsed.defines)) {
    return false;
  }
  pipeline = std::move(parsed);
  return true;
}

Json materialParameterJson(const rendering::MaterialParameterValue& value) {
  Json out;
  std::visit(
      [&](const auto& typed) {
        using Value = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Value, bool>) {
          out = Json{{"type", "bool"}, {"value", typed}};
        } else if constexpr (std::is_same_v<Value, int32_t>) {
          out = Json{{"type", "int32"}, {"value", typed}};
        } else if constexpr (std::is_same_v<Value, uint32_t>) {
          out = Json{{"type", "uint32"}, {"value", typed}};
        } else if constexpr (std::is_same_v<Value, float>) {
          out = Json{{"type", "float"}, {"value", floatJson(typed)}};
        } else if constexpr (std::is_same_v<Value, rendering::Color>) {
          out = Json{{"type", "color"}, {"value", colorJson(typed)}};
        } else if constexpr (std::is_same_v<Value, glm::vec2>) {
          out = Json{{"type", "vec2"}, {"value", vec2Json(typed)}};
        } else if constexpr (std::is_same_v<Value, glm::vec3>) {
          out = Json{{"type", "vec3"}, {"value", vec3Json(typed)}};
        } else if constexpr (std::is_same_v<Value, glm::vec4>) {
          out = Json{{"type", "vec4"}, {"value", vec4Json(typed)}};
        } else if constexpr (std::is_same_v<Value, std::string>) {
          out = Json{{"type", "string"}, {"value", typed}};
        }
      },
      value);
  return out;
}

bool readMaterialParameterJson(const Json& json, rendering::MaterialParameterValue& out) {
  if (!json.is_object() || !json.contains("type") || !json["type"].is_string() ||
      !json.contains("value")) {
    return false;
  }
  const std::string type = json["type"].get<std::string>();
  const Json& value = json["value"];
  if (type == "bool" && value.is_boolean()) {
    out = value.get<bool>();
    return true;
  }
  if (type == "int32" && value.is_number_integer()) {
    out = value.get<int32_t>();
    return true;
  }
  if (type == "uint32" && (value.is_number_unsigned() || value.is_number_integer())) {
    out = value.get<uint32_t>();
    return true;
  }
  if (type == "float") {
    float parsed = 0.0f;
    if (!readFloatJson(value, parsed)) {
      return false;
    }
    out = parsed;
    return true;
  }
  if (type == "color") {
    rendering::Color parsed{};
    if (!readColorJson(value, parsed)) {
      return false;
    }
    out = parsed;
    return true;
  }
  if (type == "vec2") {
    glm::vec2 parsed{};
    if (!readVec2Json(value, parsed)) {
      return false;
    }
    out = parsed;
    return true;
  }
  if (type == "vec3") {
    glm::vec3 parsed{};
    if (!readVec3Json(value, parsed)) {
      return false;
    }
    out = parsed;
    return true;
  }
  if (type == "vec4") {
    glm::vec4 parsed{};
    if (!readVec4Json(value, parsed)) {
      return false;
    }
    out = parsed;
    return true;
  }
  if (type == "string" && value.is_string()) {
    out = value.get<std::string>();
    return true;
  }
  return false;
}

Json materialParamsJson(const std::unordered_map<std::string, rendering::MaterialParameterValue>& params) {
  Json out = Json::object();
  for (const auto& [key, value] : params) {
    out[key] = materialParameterJson(value);
  }
  return out;
}

bool readMaterialParamsJson(const Json& json,
                            std::unordered_map<std::string, rendering::MaterialParameterValue>& out) {
  if (!json.is_object()) {
    return false;
  }
  std::unordered_map<std::string, rendering::MaterialParameterValue> parsed;
  for (const auto& [key, value] : json.items()) {
    rendering::MaterialParameterValue parameter{};
    if (!readMaterialParameterJson(value, parameter)) {
      return false;
    }
    parsed[key] = std::move(parameter);
  }
  out = std::move(parsed);
  return true;
}

Json stringMapJson(const std::unordered_map<std::string, std::string>& values) {
  Json out = Json::object();
  for (const auto& [key, value] : values) {
    out[key] = value;
  }
  return out;
}

bool readStringMapJson(const Json& json, std::unordered_map<std::string, std::string>& out) {
  if (!json.is_object()) {
    return false;
  }
  std::unordered_map<std::string, std::string> parsed;
  for (const auto& [key, value] : json.items()) {
    if (!value.is_string()) {
      return false;
    }
    parsed[key] = value.get<std::string>();
  }
  out = std::move(parsed);
  return true;
}

Json importedTexcoordRowsJson(
    const std::array<glm::vec4, rendering::kImportedMaterialTextureCoordSlotCount>& rows) {
  Json out = Json::array();
  for (const glm::vec4& row : rows) {
    out.push_back(vec4Json(row));
  }
  return out;
}

bool readImportedTexcoordRowsJson(
    const Json& json,
    std::array<glm::vec4, rendering::kImportedMaterialTextureCoordSlotCount>& rows) {
  if (!json.is_array() ||
      json.size() != rendering::kImportedMaterialTextureCoordSlotCount) {
    return false;
  }
  std::array<glm::vec4, rendering::kImportedMaterialTextureCoordSlotCount> parsed{};
  for (std::size_t i = 0u; i < parsed.size(); ++i) {
    if (!readVec4Json(json[i], parsed[i])) {
      return false;
    }
  }
  rows = parsed;
  return true;
}

Json importedMaterialMetadataJson(const rendering::ImportedMaterialData& imported) {
  return Json{
      {"material", materialDescJson(imported.material)},
      {"texcoord_row0", importedTexcoordRowsJson(imported.texcoord_row0)},
      {"texcoord_row1", importedTexcoordRowsJson(imported.texcoord_row1)},
  };
}

bool readImportedMaterialMetadataJson(const Json& json,
                                      rendering::ImportedMaterialData& imported) {
  if (!json.is_object()) {
    return false;
  }
  rendering::ImportedMaterialData parsed{};
  if (!readMaterialDescJson(json.value("material", Json::object()), parsed.material) ||
      !readImportedTexcoordRowsJson(json.value("texcoord_row0", Json::array()),
                                    parsed.texcoord_row0) ||
      !readImportedTexcoordRowsJson(json.value("texcoord_row1", Json::array()),
                                    parsed.texcoord_row1)) {
    return false;
  }
  parsed.textures.clear();
  imported = std::move(parsed);
  return true;
}

Json materialAssetJson(const rendering::MaterialAssetDesc& material) {
  Json json{
      {"material_key", material.material_key},
      {"pipeline", pipelineJson(material.pipeline)},
      {"surface", materialDescJson(material.surface)},
      {"params", materialParamsJson(material.params)},
      {"textures", stringMapJson(material.textures)},
      {"material_asset_path", material.material_asset_path.generic_string()},
      {"material_asset_index", material.material_asset_index},
  };
  if (material.imported_material) {
    json["imported_material"] =
        importedMaterialMetadataJson(*material.imported_material);
  }
  return json;
}

bool readMaterialAssetJson(const Json& json, rendering::MaterialAssetDesc& material) {
  if (!json.is_object()) {
    return false;
  }
  rendering::MaterialAssetDesc parsed{};
  parsed.material_key = json.value("material_key", std::string{});
  if (!readPipelineJson(json.value("pipeline", Json::object()), parsed.pipeline) ||
      !readMaterialDescJson(json.value("surface", Json::object()), parsed.surface) ||
      !readMaterialParamsJson(json.value("params", Json::object()), parsed.params) ||
      !readStringMapJson(json.value("textures", Json::object()), parsed.textures)) {
    return false;
  }
  parsed.material_asset_path = json.value("material_asset_path", std::string{});
  parsed.material_asset_index =
      json.value("material_asset_index", parsed.material_asset_index);
  if (const auto it = json.find("imported_material"); it != json.end()) {
    rendering::ImportedMaterialData imported{};
    if (!readImportedMaterialMetadataJson(*it, imported)) {
      return false;
    }
    parsed.imported_material =
        std::make_shared<rendering::ImportedMaterialData>(std::move(imported));
  }
  material = std::move(parsed);
  return true;
}

Json materialVariantJson(const rendering::MaterialVariantDesc& material) {
  return Json{
      {"material_key", material.material_key},
      {"base_material_key", material.base_material_key},
      {"params", materialParamsJson(material.params)},
      {"textures", stringMapJson(material.textures)},
  };
}

bool readMaterialVariantJson(const Json& json, rendering::MaterialVariantDesc& material) {
  if (!json.is_object()) {
    return false;
  }
  rendering::MaterialVariantDesc parsed{};
  parsed.material_key = json.value("material_key", std::string{});
  parsed.base_material_key = json.value("base_material_key", std::string{});
  if (!readMaterialParamsJson(json.value("params", Json::object()), parsed.params) ||
      !readStringMapJson(json.value("textures", Json::object()), parsed.textures)) {
    return false;
  }
  material = std::move(parsed);
  return true;
}

Json emitterJson(const components::ParticleEmitterComponent& emitter) {
  return Json{
      {"enabled", emitter.enabled},
      {"playing", emitter.playing},
      {"loop", emitter.loop},
      {"emit_burst_on_start", emitter.emit_burst_on_start},
      {"local_space", emitter.local_space},
      {"layer", emitter.layer},
      {"depth_test", emitter.depth_test},
      {"blend_mode", static_cast<uint32_t>(emitter.blend_mode)},
      {"alignment", static_cast<uint32_t>(emitter.alignment)},
      {"shading_mode", static_cast<uint32_t>(emitter.shading_mode)},
      {"use_soft_mask", emitter.use_soft_mask},
      {"soft_particle_distance", floatJson(emitter.soft_particle_distance)},
      {"distortion_strength", floatJson(emitter.distortion_strength)},
      {"fresnel_power", floatJson(emitter.fresnel_power)},
      {"fresnel_strength", floatJson(emitter.fresnel_strength)},
      {"refraction_strength", floatJson(emitter.refraction_strength)},
      {"interior_glow", floatJson(emitter.interior_glow)},
      {"texture_key", emitter.texture_key},
      {"atlas_columns", emitter.atlas_columns},
      {"atlas_rows", emitter.atlas_rows},
      {"atlas_frame_count", emitter.atlas_frame_count},
      {"atlas_frame_width", emitter.atlas_frame_width},
      {"atlas_frame_height", emitter.atlas_frame_height},
      {"atlas_border_x", emitter.atlas_border_x},
      {"atlas_border_y", emitter.atlas_border_y},
      {"atlas_spacing_x", emitter.atlas_spacing_x},
      {"atlas_spacing_y", emitter.atlas_spacing_y},
      {"animation_fps", floatJson(emitter.animation_fps)},
      {"animate_over_lifetime", emitter.animate_over_lifetime},
      {"random_start_frame", emitter.random_start_frame},
      {"max_particles", emitter.max_particles},
      {"burst_count", emitter.burst_count},
      {"seed", emitter.seed},
      {"time_scale", floatJson(emitter.time_scale)},
      {"start_delay", floatJson(emitter.start_delay)},
      {"duration", floatJson(emitter.duration)},
      {"spawn_rate", floatJson(emitter.spawn_rate)},
      {"particle_lifetime_min", floatJson(emitter.particle_lifetime_min)},
      {"particle_lifetime_max", floatJson(emitter.particle_lifetime_max)},
      {"start_size_min", floatJson(emitter.start_size_min)},
      {"start_size_max", floatJson(emitter.start_size_max)},
      {"end_size_min", floatJson(emitter.end_size_min)},
      {"end_size_max", floatJson(emitter.end_size_max)},
      {"size_curve_exponent", floatJson(emitter.size_curve_exponent)},
      {"alpha_curve_exponent", floatJson(emitter.alpha_curve_exponent)},
      {"initial_rotation_min", floatJson(emitter.initial_rotation_min)},
      {"initial_rotation_max", floatJson(emitter.initial_rotation_max)},
      {"angular_velocity_min", floatJson(emitter.angular_velocity_min)},
      {"angular_velocity_max", floatJson(emitter.angular_velocity_max)},
      {"source_shape", static_cast<uint32_t>(emitter.source_shape)},
      {"source_box_extents", mathVec3Json(emitter.source_box_extents)},
      {"source_dimensions", mathVec3Json(emitter.source_dimensions)},
      {"source_radius_min", floatJson(emitter.source_radius_min)},
      {"source_radius_max", floatJson(emitter.source_radius_max)},
      {"source_inner_radius", floatJson(emitter.source_inner_radius)},
      {"source_outer_radius", floatJson(emitter.source_outer_radius)},
      {"source_height", floatJson(emitter.source_height)},
      {"source_angle", floatJson(emitter.source_angle)},
      {"source_path_points", vectorJson(emitter.source_path_points, mathVec3Json)},
      {"source_closed_loop", emitter.source_closed_loop},
      {"source_sampling", static_cast<uint32_t>(emitter.source_sampling)},
      {"source_jitter_radius", floatJson(emitter.source_jitter_radius)},
      {"source_mesh_asset_key", emitter.source_mesh_asset_key},
      {"source_distribution", static_cast<uint32_t>(emitter.source_distribution)},
      {"radial_speed_min", floatJson(emitter.radial_speed_min)},
      {"radial_speed_max", floatJson(emitter.radial_speed_max)},
      {"velocity_min", mathVec3Json(emitter.velocity_min)},
      {"velocity_max", mathVec3Json(emitter.velocity_max)},
      {"acceleration", mathVec3Json(emitter.acceleration)},
      {"drag", floatJson(emitter.drag)},
      {"collide_with_ground", emitter.collide_with_ground},
      {"ground_height", floatJson(emitter.ground_height)},
      {"bounce_damping", floatJson(emitter.bounce_damping)},
      {"collision_friction", floatJson(emitter.collision_friction)},
      {"rest_speed_threshold", floatJson(emitter.rest_speed_threshold)},
      {"start_color", colorJson(emitter.start_color)},
      {"end_color", colorJson(emitter.end_color)},
  };
}

bool readEmitterJson(const Json& json, components::ParticleEmitterComponent& emitter) {
  if (!json.is_object()) {
    return false;
  }
  components::ParticleEmitterComponent parsed{};
  auto read_float_field = [&](const char* key, float& out) {
    const auto it = json.find(key);
    return it == json.end() || readFloatJson(*it, out);
  };
  parsed.enabled = json.value("enabled", parsed.enabled);
  parsed.playing = json.value("playing", parsed.playing);
  parsed.loop = json.value("loop", parsed.loop);
  parsed.emit_burst_on_start = json.value("emit_burst_on_start", parsed.emit_burst_on_start);
  parsed.local_space = json.value("local_space", parsed.local_space);
  parsed.layer = json.value("layer", parsed.layer);
  parsed.depth_test = json.value("depth_test", parsed.depth_test);
  parsed.blend_mode = static_cast<components::ParticleBlendMode>(json.value("blend_mode", 0u));
  parsed.alignment = static_cast<components::ParticleAlignment>(json.value("alignment", 0u));
  parsed.shading_mode = static_cast<components::ParticleShadingMode>(json.value("shading_mode", 0u));
  parsed.use_soft_mask = json.value("use_soft_mask", parsed.use_soft_mask);
  if (!read_float_field("soft_particle_distance", parsed.soft_particle_distance) ||
      !read_float_field("distortion_strength", parsed.distortion_strength) ||
      !read_float_field("fresnel_power", parsed.fresnel_power) ||
      !read_float_field("fresnel_strength", parsed.fresnel_strength) ||
      !read_float_field("refraction_strength", parsed.refraction_strength) ||
      !read_float_field("interior_glow", parsed.interior_glow)) {
    return false;
  }
  parsed.texture_key = json.value("texture_key", parsed.texture_key);
  parsed.atlas_columns = json.value("atlas_columns", parsed.atlas_columns);
  parsed.atlas_rows = json.value("atlas_rows", parsed.atlas_rows);
  parsed.atlas_frame_count = json.value("atlas_frame_count", parsed.atlas_frame_count);
  parsed.atlas_frame_width = json.value("atlas_frame_width", parsed.atlas_frame_width);
  parsed.atlas_frame_height = json.value("atlas_frame_height", parsed.atlas_frame_height);
  parsed.atlas_border_x = json.value("atlas_border_x", parsed.atlas_border_x);
  parsed.atlas_border_y = json.value("atlas_border_y", parsed.atlas_border_y);
  parsed.atlas_spacing_x = json.value("atlas_spacing_x", parsed.atlas_spacing_x);
  parsed.atlas_spacing_y = json.value("atlas_spacing_y", parsed.atlas_spacing_y);
  parsed.animate_over_lifetime = json.value("animate_over_lifetime", parsed.animate_over_lifetime);
  parsed.random_start_frame = json.value("random_start_frame", parsed.random_start_frame);
  parsed.max_particles = json.value("max_particles", parsed.max_particles);
  parsed.burst_count = json.value("burst_count", parsed.burst_count);
  parsed.seed = json.value("seed", parsed.seed);
  if (!read_float_field("animation_fps", parsed.animation_fps) ||
      !read_float_field("time_scale", parsed.time_scale) ||
      !read_float_field("start_delay", parsed.start_delay) ||
      !read_float_field("duration", parsed.duration) ||
      !read_float_field("spawn_rate", parsed.spawn_rate) ||
      !read_float_field("particle_lifetime_min", parsed.particle_lifetime_min) ||
      !read_float_field("particle_lifetime_max", parsed.particle_lifetime_max) ||
      !read_float_field("start_size_min", parsed.start_size_min) ||
      !read_float_field("start_size_max", parsed.start_size_max) ||
      !read_float_field("end_size_min", parsed.end_size_min) ||
      !read_float_field("end_size_max", parsed.end_size_max) ||
      !read_float_field("size_curve_exponent", parsed.size_curve_exponent) ||
      !read_float_field("alpha_curve_exponent", parsed.alpha_curve_exponent) ||
      !read_float_field("initial_rotation_min", parsed.initial_rotation_min) ||
      !read_float_field("initial_rotation_max", parsed.initial_rotation_max) ||
      !read_float_field("angular_velocity_min", parsed.angular_velocity_min) ||
      !read_float_field("angular_velocity_max", parsed.angular_velocity_max)) {
    return false;
  }
  parsed.source_shape = static_cast<components::ParticleSourceShape>(json.value("source_shape", 0u));
  parsed.source_sampling = static_cast<components::ParticleSourceSamplingMode>(json.value("source_sampling", 0u));
  parsed.source_distribution =
      static_cast<components::ParticleSourceDistribution>(json.value("source_distribution", 0u));
  if (!readMathVec3Json(json.value("source_box_extents", Json::array({0.0f, 0.0f, 0.0f})),
                        parsed.source_box_extents) ||
      !readMathVec3Json(json.value("source_dimensions", Json::array({0.0f, 0.0f, 0.0f})),
                        parsed.source_dimensions) ||
      !readVectorJson(json.value("source_path_points", Json::array()),
                      parsed.source_path_points,
                      readMathVec3Json) ||
      !read_float_field("source_radius_min", parsed.source_radius_min) ||
      !read_float_field("source_radius_max", parsed.source_radius_max) ||
      !read_float_field("source_inner_radius", parsed.source_inner_radius) ||
      !read_float_field("source_outer_radius", parsed.source_outer_radius) ||
      !read_float_field("source_height", parsed.source_height) ||
      !read_float_field("source_angle", parsed.source_angle) ||
      !read_float_field("source_jitter_radius", parsed.source_jitter_radius) ||
      !read_float_field("radial_speed_min", parsed.radial_speed_min) ||
      !read_float_field("radial_speed_max", parsed.radial_speed_max)) {
    return false;
  }
  parsed.source_closed_loop = json.value("source_closed_loop", parsed.source_closed_loop);
  parsed.source_mesh_asset_key = json.value("source_mesh_asset_key", parsed.source_mesh_asset_key);
  if (!readMathVec3Json(json.value("velocity_min", Json::array({0.0f, 0.0f, 0.0f})),
                        parsed.velocity_min) ||
      !readMathVec3Json(json.value("velocity_max", Json::array({0.0f, 0.0f, 0.0f})),
                        parsed.velocity_max) ||
      !readMathVec3Json(json.value("acceleration", Json::array({0.0f, 0.0f, 0.0f})),
                        parsed.acceleration) ||
      !read_float_field("drag", parsed.drag) ||
      !read_float_field("ground_height", parsed.ground_height) ||
      !read_float_field("bounce_damping", parsed.bounce_damping) ||
      !read_float_field("collision_friction", parsed.collision_friction) ||
      !read_float_field("rest_speed_threshold", parsed.rest_speed_threshold) ||
      !readColorJson(json.value("start_color", Json::array({1.0f, 1.0f, 1.0f, 1.0f})),
                     parsed.start_color) ||
      !readColorJson(json.value("end_color", Json::array({1.0f, 1.0f, 1.0f, 1.0f})),
                     parsed.end_color)) {
    return false;
  }
  parsed.collide_with_ground = json.value("collide_with_ground", parsed.collide_with_ground);
  emitter = std::move(parsed);
  return true;
}

Json particleEffectJson(const visual::particles::ParticleEffectAsset& effect) {
  Json emitters = Json::array();
  for (const auto& emitter : effect.emitters) {
    emitters.push_back(Json{
        {"texture_key", emitter.texture_key},
        {"emitter", emitterJson(emitter.emitter)},
    });
  }
  return Json{{"emitters", std::move(emitters)}};
}

bool readParticleEffectJson(const Json& json, visual::particles::ParticleEffectAsset& effect) {
  if (!json.is_object() || !json.contains("emitters") || !json["emitters"].is_array()) {
    return false;
  }
  visual::particles::ParticleEffectAsset parsed{};
  parsed.emitters.reserve(json["emitters"].size());
  for (const Json& emitter_json : json["emitters"]) {
    if (!emitter_json.is_object()) {
      return false;
    }
    visual::particles::ParticleEmitterDesc emitter{};
    emitter.texture_key = emitter_json.value("texture_key", std::string{});
    if (!readEmitterJson(emitter_json.value("emitter", Json::object()), emitter.emitter)) {
      return false;
    }
    parsed.emitters.push_back(std::move(emitter));
  }
  effect = std::move(parsed);
  return true;
}

Json lightJson(const components::LightComponent& light) {
  return Json{
      {"type", static_cast<uint32_t>(light.type)},
      {"color", colorJson(light.color)},
      {"intensity", floatJson(light.intensity)},
      {"range", floatJson(light.range)},
      {"inner_cone_degrees", floatJson(light.inner_cone_degrees)},
      {"outer_cone_degrees", floatJson(light.outer_cone_degrees)},
      {"casts_shadows", light.casts_shadows},
      {"shadow_extent", floatJson(light.shadow_extent)},
  };
}

bool readLightJson(const Json& json, components::LightComponent& light) {
  if (!json.is_object()) {
    return false;
  }
  components::LightComponent parsed{};
  parsed.type = static_cast<components::LightComponent::Type>(json.value("type", 1u));
  if (!readColorJson(json.value("color", colorJson(parsed.color)), parsed.color) ||
      !readFloatJson(json.value("intensity", Json(parsed.intensity)), parsed.intensity) ||
      !readFloatJson(json.value("range", Json(parsed.range)), parsed.range) ||
      !readFloatJson(json.value("inner_cone_degrees", Json(parsed.inner_cone_degrees)),
                     parsed.inner_cone_degrees) ||
      !readFloatJson(json.value("outer_cone_degrees", Json(parsed.outer_cone_degrees)),
                     parsed.outer_cone_degrees) ||
      !readFloatJson(json.value("shadow_extent", Json(parsed.shadow_extent)),
                     parsed.shadow_extent)) {
    return false;
  }
  parsed.casts_shadows = json.value("casts_shadows", false);
  light = parsed;
  return true;
}

Json gltfScenePrimitiveJson(const GltfSceneAssetPrimitive& primitive) {
  return Json{
      {"name", primitive.name},
      {"mesh_key", primitive.mesh_key},
      {"material_key", primitive.material_key},
      {"skin_index", primitive.skin_index},
      {"morph_weights", floatVectorJson(primitive.morph_weights)},
      {"joint_node_indices", u32VectorJson(primitive.joint_node_indices)},
      {"inverse_bind_matrices", vectorJson(primitive.inverse_bind_matrices, mat4Json)},
  };
}

bool readGltfScenePrimitiveJson(const Json& json, GltfSceneAssetPrimitive& primitive) {
  if (!json.is_object()) {
    return false;
  }
  GltfSceneAssetPrimitive parsed{};
  parsed.name = json.value("name", std::string{});
  parsed.mesh_key = json.value("mesh_key", std::string{});
  parsed.material_key = json.value("material_key", std::string{});
  parsed.skin_index = json.value("skin_index", parsed.skin_index);
  if (!readFloatVectorJson(json.value("morph_weights", Json::array()), parsed.morph_weights) ||
      !readU32VectorJson(json.value("joint_node_indices", Json::array()),
                         parsed.joint_node_indices) ||
      !readVectorJson(json.value("inverse_bind_matrices", Json::array()),
                      parsed.inverse_bind_matrices,
                      readMat4Json)) {
    return false;
  }
  primitive = std::move(parsed);
  return true;
}

Json gltfSceneNodeJson(const GltfSceneAssetNode& node) {
  return Json{
      {"name", node.name},
      {"local_position", mathVec3Json(node.local_position)},
      {"local_rotation", quatJson(node.local_rotation)},
      {"local_scale", mathVec3Json(node.local_scale)},
      {"world_position", mathVec3Json(node.world_position)},
      {"world_rotation", quatJson(node.world_rotation)},
      {"world_scale", mathVec3Json(node.world_scale)},
      {"has_light", node.has_light},
      {"light", lightJson(node.light)},
      {"primitives", vectorJson(node.primitives, gltfScenePrimitiveJson)},
      {"children", u32VectorJson(node.children)},
  };
}

bool readGltfSceneNodeJson(const Json& json, GltfSceneAssetNode& node) {
  if (!json.is_object()) {
    return false;
  }
  GltfSceneAssetNode parsed{};
  parsed.name = json.value("name", std::string{});
  parsed.has_light = json.value("has_light", false);
  if (!readMathVec3Json(json.value("local_position", mathVec3Json(parsed.local_position)),
                        parsed.local_position) ||
      !readQuatJson(json.value("local_rotation", quatJson(parsed.local_rotation)),
                    parsed.local_rotation) ||
      !readMathVec3Json(json.value("local_scale", mathVec3Json(parsed.local_scale)),
                        parsed.local_scale) ||
      !readMathVec3Json(json.value("world_position", mathVec3Json(parsed.world_position)),
                        parsed.world_position) ||
      !readQuatJson(json.value("world_rotation", quatJson(parsed.world_rotation)),
                    parsed.world_rotation) ||
      !readMathVec3Json(json.value("world_scale", mathVec3Json(parsed.world_scale)),
                        parsed.world_scale) ||
      !readLightJson(json.value("light", lightJson(parsed.light)), parsed.light) ||
      !readVectorJson(json.value("primitives", Json::array()),
                      parsed.primitives,
                      readGltfScenePrimitiveJson) ||
      !readU32VectorJson(json.value("children", Json::array()), parsed.children)) {
    return false;
  }
  node = std::move(parsed);
  return true;
}

Json gltfSceneJson(const GltfSceneAsset& scene) {
  return Json{
      {"source_path", scene.source_path.generic_string()},
      {"root_node", scene.root_node},
      {"nodes", vectorJson(scene.nodes, gltfSceneNodeJson)},
      {"mesh_asset_keys", stringVectorJson(scene.mesh_asset_keys)},
      {"texture_asset_keys", stringVectorJson(scene.texture_asset_keys)},
      {"material_keys", stringVectorJson(scene.material_keys)},
      {"animation_clip_keys", stringVectorJson(scene.animation_clip_keys)},
      {"skeleton_keys", stringVectorJson(scene.skeleton_keys)},
      {"skin_keys", stringVectorJson(scene.skin_keys)},
      {"scene_key", scene.scene_key},
  };
}

bool readGltfSceneJson(const Json& json, GltfSceneAsset& scene) {
  if (!json.is_object()) {
    return false;
  }
  GltfSceneAsset parsed{};
  parsed.source_path = json.value("source_path", std::string{});
  parsed.scene_key = json.value("scene_key", std::string{});
  parsed.root_node = json.value("root_node", parsed.root_node);
  if (!readVectorJson(json.value("nodes", Json::array()),
                      parsed.nodes,
                      readGltfSceneNodeJson) ||
      !readStringVectorJson(json.value("mesh_asset_keys", Json::array()), parsed.mesh_asset_keys) ||
      !readStringVectorJson(json.value("texture_asset_keys", Json::array()), parsed.texture_asset_keys) ||
      !readStringVectorJson(json.value("material_keys", Json::array()), parsed.material_keys) ||
      !readStringVectorJson(json.value("animation_clip_keys", Json::array()), parsed.animation_clip_keys) ||
      !readStringVectorJson(json.value("skeleton_keys", Json::array()), parsed.skeleton_keys) ||
      !readStringVectorJson(json.value("skin_keys", Json::array()), parsed.skin_keys)) {
    return false;
  }
  scene = std::move(parsed);
  return true;
}

Json vec3KeyJson(const world::Vec3Keyframe& key) {
  return Json{{"time", floatJson(key.time_seconds)},
              {"value", mathVec3Json(key.value)},
              {"in_tangent", mathVec3Json(key.in_tangent)},
              {"out_tangent", mathVec3Json(key.out_tangent)}};
}

bool readVec3KeyJson(const Json& json, world::Vec3Keyframe& key) {
  return json.is_object() &&
         readFloatJson(json.value("time", Json(0.0f)), key.time_seconds) &&
         readMathVec3Json(json.value("value", Json::array({0.0f, 0.0f, 0.0f})), key.value) &&
         readMathVec3Json(json.value("in_tangent", Json::array({0.0f, 0.0f, 0.0f})), key.in_tangent) &&
         readMathVec3Json(json.value("out_tangent", Json::array({0.0f, 0.0f, 0.0f})), key.out_tangent);
}

Json quatKeyJson(const world::QuatKeyframe& key) {
  return Json{{"time", floatJson(key.time_seconds)},
              {"value", quatJson(key.value)},
              {"in_tangent", quatJson(key.in_tangent)},
              {"out_tangent", quatJson(key.out_tangent)}};
}

bool readQuatKeyJson(const Json& json, world::QuatKeyframe& key) {
  return json.is_object() &&
         readFloatJson(json.value("time", Json(0.0f)), key.time_seconds) &&
         readQuatJson(json.value("value", Json::array({0.0f, 0.0f, 0.0f, 1.0f})), key.value) &&
         readQuatJson(json.value("in_tangent", Json::array({0.0f, 0.0f, 0.0f, 1.0f})), key.in_tangent) &&
         readQuatJson(json.value("out_tangent", Json::array({0.0f, 0.0f, 0.0f, 1.0f})), key.out_tangent);
}

Json morphKeyJson(const world::MorphWeightKeyframe& key) {
  return Json{{"time", floatJson(key.time_seconds)},
              {"values", floatVectorJson(key.values)},
              {"in_tangents", floatVectorJson(key.in_tangents)},
              {"out_tangents", floatVectorJson(key.out_tangents)}};
}

bool readMorphKeyJson(const Json& json, world::MorphWeightKeyframe& key) {
  return json.is_object() &&
         readFloatJson(json.value("time", Json(0.0f)), key.time_seconds) &&
         readFloatVectorJson(json.value("values", Json::array()), key.values) &&
         readFloatVectorJson(json.value("in_tangents", Json::array()), key.in_tangents) &&
         readFloatVectorJson(json.value("out_tangents", Json::array()), key.out_tangents);
}

Json animationChannelJson(const world::AnimationChannel& channel) {
  return Json{
      {"target_node_index", channel.target_node_index},
      {"target_skin_index", channel.target_skin_index},
      {"target_joint_index", channel.target_joint_index},
      {"position_interpolation", static_cast<uint32_t>(channel.position_interpolation)},
      {"rotation_interpolation", static_cast<uint32_t>(channel.rotation_interpolation)},
      {"scale_interpolation", static_cast<uint32_t>(channel.scale_interpolation)},
      {"position_keys", vectorJson(channel.position_keys, vec3KeyJson)},
      {"rotation_keys", vectorJson(channel.rotation_keys, quatKeyJson)},
      {"scale_keys", vectorJson(channel.scale_keys, vec3KeyJson)},
  };
}

bool readAnimationChannelJson(const Json& json, world::AnimationChannel& channel) {
  if (!json.is_object()) {
    return false;
  }
  world::AnimationChannel parsed{};
  parsed.target_node_index = json.value("target_node_index", parsed.target_node_index);
  parsed.target_skin_index = json.value("target_skin_index", parsed.target_skin_index);
  parsed.target_joint_index = json.value("target_joint_index", parsed.target_joint_index);
  parsed.position_interpolation =
      static_cast<world::InterpolationMode>(json.value("position_interpolation", 1u));
  parsed.rotation_interpolation =
      static_cast<world::InterpolationMode>(json.value("rotation_interpolation", 1u));
  parsed.scale_interpolation =
      static_cast<world::InterpolationMode>(json.value("scale_interpolation", 1u));
  if (!readVectorJson(json.value("position_keys", Json::array()), parsed.position_keys, readVec3KeyJson) ||
      !readVectorJson(json.value("rotation_keys", Json::array()), parsed.rotation_keys, readQuatKeyJson) ||
      !readVectorJson(json.value("scale_keys", Json::array()), parsed.scale_keys, readVec3KeyJson)) {
    return false;
  }
  channel = std::move(parsed);
  return true;
}

Json morphTrackJson(const world::MorphTargetTrack& track) {
  return Json{
      {"target_node_index", track.target_node_index},
      {"target_mesh_index", track.target_mesh_index},
      {"interpolation", static_cast<uint32_t>(track.interpolation)},
      {"weight_keys", vectorJson(track.weight_keys, morphKeyJson)},
  };
}

bool readMorphTrackJson(const Json& json, world::MorphTargetTrack& track) {
  if (!json.is_object()) {
    return false;
  }
  world::MorphTargetTrack parsed{};
  parsed.target_node_index = json.value("target_node_index", parsed.target_node_index);
  parsed.target_mesh_index = json.value("target_mesh_index", parsed.target_mesh_index);
  parsed.interpolation = static_cast<world::InterpolationMode>(json.value("interpolation", 1u));
  if (!readVectorJson(json.value("weight_keys", Json::array()), parsed.weight_keys, readMorphKeyJson)) {
    return false;
  }
  track = std::move(parsed);
  return true;
}

Json rootMotionJson(const world::RootMotionTrack& track) {
  return Json{
      {"target_node_index", track.target_node_index},
      {"position_interpolation", static_cast<uint32_t>(track.position_interpolation)},
      {"rotation_interpolation", static_cast<uint32_t>(track.rotation_interpolation)},
      {"position_keys", vectorJson(track.position_keys, vec3KeyJson)},
      {"rotation_keys", vectorJson(track.rotation_keys, quatKeyJson)},
  };
}

bool readRootMotionJson(const Json& json, world::RootMotionTrack& track) {
  if (!json.is_object()) {
    return false;
  }
  world::RootMotionTrack parsed{};
  parsed.target_node_index = json.value("target_node_index", parsed.target_node_index);
  parsed.position_interpolation =
      static_cast<world::InterpolationMode>(json.value("position_interpolation", 1u));
  parsed.rotation_interpolation =
      static_cast<world::InterpolationMode>(json.value("rotation_interpolation", 1u));
  if (!readVectorJson(json.value("position_keys", Json::array()), parsed.position_keys, readVec3KeyJson) ||
      !readVectorJson(json.value("rotation_keys", Json::array()), parsed.rotation_keys, readQuatKeyJson)) {
    return false;
  }
  track = std::move(parsed);
  return true;
}

Json animationClipJson(const world::AnimationClip& clip) {
  Json events = Json::array();
  for (const auto& event : clip.events) {
    events.push_back(Json{{"name", event.name},
                          {"time", floatJson(event.time_seconds)},
                          {"payload", event.payload}});
  }
  Json root{{"name", clip.name},
            {"duration_seconds", floatJson(clip.duration_seconds)},
            {"ticks_per_second", floatJson(clip.ticks_per_second)},
            {"source_index", clip.source_index},
            {"channels", vectorJson(clip.channels, animationChannelJson)},
            {"morph_target_tracks", vectorJson(clip.morph_target_tracks, morphTrackJson)},
            {"events", std::move(events)}};
  if (clip.root_motion.has_value()) {
    root["root_motion"] = rootMotionJson(*clip.root_motion);
  }
  return root;
}

bool readAnimationClipJson(const Json& json, world::AnimationClip& clip) {
  if (!json.is_object()) {
    return false;
  }
  world::AnimationClip parsed{};
  parsed.name = json.value("name", std::string{});
  parsed.source_index = json.value("source_index", parsed.source_index);
  if (!readFloatJson(json.value("duration_seconds", Json(0.0f)), parsed.duration_seconds) ||
      !readFloatJson(json.value("ticks_per_second", Json(1.0f)), parsed.ticks_per_second) ||
      !readVectorJson(json.value("channels", Json::array()), parsed.channels, readAnimationChannelJson) ||
      !readVectorJson(json.value("morph_target_tracks", Json::array()),
                      parsed.morph_target_tracks,
                      readMorphTrackJson)) {
    return false;
  }
  if (const auto it = json.find("events"); it != json.end()) {
    if (!it->is_array()) {
      return false;
    }
    parsed.events.reserve(it->size());
    for (const Json& event_json : *it) {
      if (!event_json.is_object()) {
        return false;
      }
      world::AnimationEvent event{};
      event.name = event_json.value("name", std::string{});
      event.payload = event_json.value("payload", std::string{});
      if (!readFloatJson(event_json.value("time", Json(0.0f)), event.time_seconds)) {
        return false;
      }
      parsed.events.push_back(std::move(event));
    }
  }
  if (const auto it = json.find("root_motion"); it != json.end()) {
    world::RootMotionTrack root_motion{};
    if (!readRootMotionJson(*it, root_motion)) {
      return false;
    }
    parsed.root_motion = std::move(root_motion);
  }
  clip = std::move(parsed);
  return true;
}

void appendVec3KeyframeBinary(std::vector<uint8_t>& out,
                              const world::Vec3Keyframe& key) {
  appendF32(out, key.time_seconds);
  appendMathVec3Binary(out, key.value);
  appendMathVec3Binary(out, key.in_tangent);
  appendMathVec3Binary(out, key.out_tangent);
}

bool readVec3KeyframeBinary(const std::vector<uint8_t>& bytes,
                            std::size_t& offset,
                            world::Vec3Keyframe& key) {
  return readF32(bytes, offset, key.time_seconds) &&
         readMathVec3Binary(bytes, offset, key.value) &&
         readMathVec3Binary(bytes, offset, key.in_tangent) &&
         readMathVec3Binary(bytes, offset, key.out_tangent);
}

void appendQuatKeyframeBinary(std::vector<uint8_t>& out,
                              const world::QuatKeyframe& key) {
  appendF32(out, key.time_seconds);
  appendQuatBinary(out, key.value);
  appendQuatBinary(out, key.in_tangent);
  appendQuatBinary(out, key.out_tangent);
}

bool readQuatKeyframeBinary(const std::vector<uint8_t>& bytes,
                            std::size_t& offset,
                            world::QuatKeyframe& key) {
  return readF32(bytes, offset, key.time_seconds) &&
         readQuatBinary(bytes, offset, key.value) &&
         readQuatBinary(bytes, offset, key.in_tangent) &&
         readQuatBinary(bytes, offset, key.out_tangent);
}

void appendFloatVectorBinary(std::vector<uint8_t>& out, const std::vector<float>& values) {
  appendBinaryVector(out, values, [](std::vector<uint8_t>& payload, float value) {
    appendF32(payload, value);
  });
}

bool readFloatVectorBinary(const std::vector<uint8_t>& bytes,
                           std::size_t& offset,
                           std::vector<float>& values) {
  return readBinaryVector(bytes,
                          offset,
                          values,
                          [](const std::vector<uint8_t>& payload,
                             std::size_t& payload_offset,
                             float& value) {
                            return readF32(payload, payload_offset, value);
                          });
}

void appendMorphWeightKeyframeBinary(std::vector<uint8_t>& out,
                                     const world::MorphWeightKeyframe& key) {
  appendF32(out, key.time_seconds);
  appendFloatVectorBinary(out, key.values);
  appendFloatVectorBinary(out, key.in_tangents);
  appendFloatVectorBinary(out, key.out_tangents);
}

bool readMorphWeightKeyframeBinary(const std::vector<uint8_t>& bytes,
                                   std::size_t& offset,
                                   world::MorphWeightKeyframe& key) {
  return readF32(bytes, offset, key.time_seconds) &&
         readFloatVectorBinary(bytes, offset, key.values) &&
         readFloatVectorBinary(bytes, offset, key.in_tangents) &&
         readFloatVectorBinary(bytes, offset, key.out_tangents);
}

void appendInterpolationBinary(std::vector<uint8_t>& out,
                               world::InterpolationMode interpolation) {
  appendU8(out, static_cast<uint8_t>(interpolation));
}

bool readInterpolationBinary(const std::vector<uint8_t>& bytes,
                             std::size_t& offset,
                             world::InterpolationMode& interpolation) {
  uint8_t value = 0u;
  if (!readU8(bytes, offset, value) ||
      value > static_cast<uint8_t>(world::InterpolationMode::CubicSpline)) {
    return false;
  }
  interpolation = static_cast<world::InterpolationMode>(value);
  return true;
}

void appendAnimationChannelBinary(std::vector<uint8_t>& out,
                                  const world::AnimationChannel& channel) {
  appendU32(out, channel.target_node_index);
  appendU32(out, channel.target_skin_index);
  appendU32(out, channel.target_joint_index);
  appendInterpolationBinary(out, channel.position_interpolation);
  appendInterpolationBinary(out, channel.rotation_interpolation);
  appendInterpolationBinary(out, channel.scale_interpolation);
  appendBinaryVector(out, channel.position_keys, appendVec3KeyframeBinary);
  appendBinaryVector(out, channel.rotation_keys, appendQuatKeyframeBinary);
  appendBinaryVector(out, channel.scale_keys, appendVec3KeyframeBinary);
}

bool readAnimationChannelBinary(const std::vector<uint8_t>& bytes,
                                std::size_t& offset,
                                world::AnimationChannel& channel) {
  return readU32(bytes, offset, channel.target_node_index) &&
         readU32(bytes, offset, channel.target_skin_index) &&
         readU32(bytes, offset, channel.target_joint_index) &&
         readInterpolationBinary(bytes, offset, channel.position_interpolation) &&
         readInterpolationBinary(bytes, offset, channel.rotation_interpolation) &&
         readInterpolationBinary(bytes, offset, channel.scale_interpolation) &&
         readBinaryVector(bytes, offset, channel.position_keys, readVec3KeyframeBinary) &&
         readBinaryVector(bytes, offset, channel.rotation_keys, readQuatKeyframeBinary) &&
         readBinaryVector(bytes, offset, channel.scale_keys, readVec3KeyframeBinary);
}

void appendMorphTargetTrackBinary(std::vector<uint8_t>& out,
                                  const world::MorphTargetTrack& track) {
  appendU32(out, track.target_node_index);
  appendU32(out, track.target_mesh_index);
  appendInterpolationBinary(out, track.interpolation);
  appendBinaryVector(out, track.weight_keys, appendMorphWeightKeyframeBinary);
}

bool readMorphTargetTrackBinary(const std::vector<uint8_t>& bytes,
                                std::size_t& offset,
                                world::MorphTargetTrack& track) {
  return readU32(bytes, offset, track.target_node_index) &&
         readU32(bytes, offset, track.target_mesh_index) &&
         readInterpolationBinary(bytes, offset, track.interpolation) &&
         readBinaryVector(bytes, offset, track.weight_keys, readMorphWeightKeyframeBinary);
}

void appendAnimationEventBinary(std::vector<uint8_t>& out,
                                const world::AnimationEvent& event) {
  appendStringBinary(out, event.name);
  appendF32(out, event.time_seconds);
  appendStringBinary(out, event.payload);
}

bool readAnimationEventBinary(const std::vector<uint8_t>& bytes,
                              std::size_t& offset,
                              world::AnimationEvent& event) {
  return readStringBinary(bytes, offset, event.name) &&
         readF32(bytes, offset, event.time_seconds) &&
         readStringBinary(bytes, offset, event.payload);
}

void appendRootMotionTrackBinary(std::vector<uint8_t>& out,
                                 const world::RootMotionTrack& track) {
  appendU32(out, track.target_node_index);
  appendInterpolationBinary(out, track.position_interpolation);
  appendInterpolationBinary(out, track.rotation_interpolation);
  appendBinaryVector(out, track.position_keys, appendVec3KeyframeBinary);
  appendBinaryVector(out, track.rotation_keys, appendQuatKeyframeBinary);
}

bool readRootMotionTrackBinary(const std::vector<uint8_t>& bytes,
                               std::size_t& offset,
                               world::RootMotionTrack& track) {
  return readU32(bytes, offset, track.target_node_index) &&
         readInterpolationBinary(bytes, offset, track.position_interpolation) &&
         readInterpolationBinary(bytes, offset, track.rotation_interpolation) &&
         readBinaryVector(bytes, offset, track.position_keys, readVec3KeyframeBinary) &&
         readBinaryVector(bytes, offset, track.rotation_keys, readQuatKeyframeBinary);
}

std::vector<uint8_t> serializeAnimationClipPayload(const world::AnimationClip& clip) {
  std::vector<uint8_t> payload;
  appendStringBinary(payload, clip.name);
  appendF32(payload, clip.duration_seconds);
  appendF32(payload, clip.ticks_per_second);
  appendU32(payload, clip.source_index);
  appendBinaryVector(payload, clip.channels, appendAnimationChannelBinary);
  appendBinaryVector(payload, clip.morph_target_tracks, appendMorphTargetTrackBinary);
  appendBinaryVector(payload, clip.events, appendAnimationEventBinary);
  appendU8(payload, clip.root_motion.has_value() ? uint8_t{1} : uint8_t{0});
  if (clip.root_motion.has_value()) {
    appendRootMotionTrackBinary(payload, *clip.root_motion);
  }
  return payload;
}

bool parseAnimationClipPayload(const std::vector<uint8_t>& payload,
                               world::AnimationClip& clip) {
  std::size_t offset = 0u;
  world::AnimationClip parsed{};
  uint8_t has_root_motion = 0u;
  if (!readStringBinary(payload, offset, parsed.name) ||
      !readF32(payload, offset, parsed.duration_seconds) ||
      !readF32(payload, offset, parsed.ticks_per_second) ||
      !readU32(payload, offset, parsed.source_index) ||
      !readBinaryVector(payload, offset, parsed.channels, readAnimationChannelBinary) ||
      !readBinaryVector(payload,
                        offset,
                        parsed.morph_target_tracks,
                        readMorphTargetTrackBinary) ||
      !readBinaryVector(payload, offset, parsed.events, readAnimationEventBinary) ||
      !readU8(payload, offset, has_root_motion) ||
      has_root_motion > 1u) {
    return false;
  }
  if (has_root_motion != 0u) {
    world::RootMotionTrack track;
    if (!readRootMotionTrackBinary(payload, offset, track)) {
      return false;
    }
    parsed.root_motion = std::move(track);
  }
  if (offset != payload.size()) {
    return false;
  }
  clip = std::move(parsed);
  return true;
}

std::vector<uint8_t> serializeAnimationClipBinary(const world::AnimationClip& clip) {
  std::vector<uint8_t> out;
  out.insert(out.end(), kMagic.begin(), kMagic.end());
  appendU32(out, AssetCache::kSchemaVersion);
  appendU32(out, kKindAnimationClip);
  appendChunk(out, kChunkAnimationClip, serializeAnimationClipPayload(clip));
  return out;
}

std::optional<world::AnimationClip> deserializeAnimationClipBinary(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic) {
  if (bytes.size() < kMagic.size() + 8u ||
      !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    if (diagnostic != nullptr) {
      *diagnostic = "cache blob magic mismatch";
    }
    return std::nullopt;
  }

  std::size_t offset = kMagic.size();
  uint32_t schema = 0u;
  uint32_t kind = 0u;
  if (!readU32(bytes, offset, schema) || !readU32(bytes, offset, kind) ||
      schema != AssetCache::kSchemaVersion || kind != kKindAnimationClip) {
    if (diagnostic != nullptr) {
      *diagnostic = "cache blob version or kind mismatch";
    }
    return std::nullopt;
  }

  while (offset < bytes.size()) {
    uint32_t chunk_id = 0u;
    uint64_t chunk_size = 0u;
    if (!readU32(bytes, offset, chunk_id) || !readU64(bytes, offset, chunk_size) ||
        chunk_size > bytes.size() - offset) {
      if (diagnostic != nullptr) {
        *diagnostic = "cache blob chunk is truncated";
      }
      return std::nullopt;
    }
    if (chunk_id == kChunkAnimationClip) {
      try {
        const std::vector<uint8_t> payload(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
        world::AnimationClip clip{};
        if (!parseAnimationClipPayload(payload, clip)) {
          if (diagnostic != nullptr) {
            *diagnostic = "cache animation clip payload failed validation";
          }
          return std::nullopt;
        }
        return clip;
      } catch (const std::exception& e) {
        if (diagnostic != nullptr) {
          *diagnostic = e.what();
        }
        return std::nullopt;
      }
    }
    offset += static_cast<std::size_t>(chunk_size);
  }
  if (diagnostic != nullptr) {
    *diagnostic = "cache blob is missing animation clip payload";
  }
  return std::nullopt;
}

Json skeletonJson(const world::Skeleton& skeleton) {
  Json joints = Json::array();
  for (const auto& joint : skeleton.joints) {
    joints.push_back(Json{
        {"name", joint.name},
        {"parent_joint_index", joint.parent_joint_index},
        {"node_index", joint.node_index},
        {"inverse_bind_matrix", mat4Json(joint.inverse_bind_matrix)},
    });
  }
  return Json{{"name", skeleton.name},
              {"joints", std::move(joints)},
              {"root_joint_indices", u32VectorJson(skeleton.root_joint_indices)}};
}

bool readSkeletonJson(const Json& json, world::Skeleton& skeleton) {
  if (!json.is_object()) {
    return false;
  }
  world::Skeleton parsed{};
  parsed.name = json.value("name", std::string{});
  if (!readU32VectorJson(json.value("root_joint_indices", Json::array()), parsed.root_joint_indices)) {
    return false;
  }
  const Json joints_json = json.value("joints", Json::array());
  if (!joints_json.is_array()) {
    return false;
  }
  parsed.joints.reserve(joints_json.size());
  for (const Json& joint_json : joints_json) {
    if (!joint_json.is_object()) {
      return false;
    }
    world::Joint joint{};
    joint.name = joint_json.value("name", std::string{});
    joint.parent_joint_index =
        joint_json.value("parent_joint_index", world::kInvalidAnimationIndex);
    joint.node_index = joint_json.value("node_index", world::kInvalidAnimationIndex);
    if (!readMat4Json(joint_json.value("inverse_bind_matrix", mat4Json(glm::mat4(1.0f))),
                      joint.inverse_bind_matrix)) {
      return false;
    }
    parsed.joints.push_back(std::move(joint));
  }
  skeleton = std::move(parsed);
  return true;
}

Json skinJson(const world::Skin& skin) {
  return Json{{"name", skin.name},
              {"skeleton_index", skin.skeleton_index},
              {"joint_node_indices", u32VectorJson(skin.joint_node_indices)},
              {"inverse_bind_matrices", vectorJson(skin.inverse_bind_matrices, mat4Json)}};
}

bool readSkinJson(const Json& json, world::Skin& skin) {
  if (!json.is_object()) {
    return false;
  }
  world::Skin parsed{};
  parsed.name = json.value("name", std::string{});
  parsed.skeleton_index = json.value("skeleton_index", parsed.skeleton_index);
  if (!readU32VectorJson(json.value("joint_node_indices", Json::array()), parsed.joint_node_indices) ||
      !readVectorJson(json.value("inverse_bind_matrices", Json::array()),
                      parsed.inverse_bind_matrices,
                      readMat4Json)) {
    return false;
  }
  skin = std::move(parsed);
  return true;
}

std::vector<uint8_t> serializeJsonAsset(uint32_t kind, const Json& payload) {
  std::vector<uint8_t> out;
  out.insert(out.end(), kMagic.begin(), kMagic.end());
  appendU32(out, AssetCache::kSchemaVersion);
  appendU32(out, kind);
  const std::string text = payload.dump();
  const auto* begin = reinterpret_cast<const uint8_t*>(text.data());
  appendChunk(out, kChunkJson, std::vector<uint8_t>(begin, begin + text.size()));
  return out;
}

std::optional<Json> deserializeJsonPayload(const std::vector<uint8_t>& bytes,
                                           uint32_t expected_kind,
                                           std::string* diagnostic) {
  if (bytes.size() < kMagic.size() + 8u ||
      !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    if (diagnostic != nullptr) {
      *diagnostic = "cache blob magic mismatch";
    }
    return std::nullopt;
  }

  std::size_t offset = kMagic.size();
  uint32_t schema = 0u;
  uint32_t kind = 0u;
  if (!readU32(bytes, offset, schema) || !readU32(bytes, offset, kind) ||
      schema != AssetCache::kSchemaVersion || kind != expected_kind) {
    if (diagnostic != nullptr) {
      *diagnostic = "cache blob version or kind mismatch";
    }
    return std::nullopt;
  }

  while (offset < bytes.size()) {
    uint32_t chunk_id = 0u;
    uint64_t chunk_size = 0u;
    if (!readU32(bytes, offset, chunk_id) || !readU64(bytes, offset, chunk_size) ||
        chunk_size > bytes.size() - offset) {
      if (diagnostic != nullptr) {
        *diagnostic = "cache blob chunk is truncated";
      }
      return std::nullopt;
    }
    if (chunk_id == kChunkJson) {
      try {
        return Json::parse(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                           bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
      } catch (const std::exception& e) {
        if (diagnostic != nullptr) {
          *diagnostic = e.what();
        }
        return std::nullopt;
      }
    }
    offset += static_cast<std::size_t>(chunk_size);
  }
  if (diagnostic != nullptr) {
    *diagnostic = "cache blob is missing JSON payload";
  }
  return std::nullopt;
}


template <typename T, typename Reader>
std::optional<T> deserializeJsonAsset(const std::vector<uint8_t>& bytes,
                                      uint32_t kind,
                                      Reader reader,
                                      std::string* diagnostic) {
  auto json = deserializeJsonPayload(bytes, kind, diagnostic);
  if (!json.has_value()) {
    return std::nullopt;
  }
  try {
    T value{};
    if (!reader(*json, value)) {
      if (diagnostic != nullptr) {
        *diagnostic = "cache JSON payload failed validation";
      }
      return std::nullopt;
    }
    return value;
  } catch (const std::exception& e) {
    if (diagnostic != nullptr) {
      *diagnostic = e.what();
    }
    return std::nullopt;
  }
}

}  // namespace

std::vector<uint8_t> serializeMaterialAsset(const rendering::MaterialAssetDesc& material) {
  return serializeJsonAsset(kKindMaterialAsset, materialAssetJson(material));
}

std::optional<rendering::MaterialAssetDesc> deserializeMaterialAsset(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic) {
  return deserializeJsonAsset<rendering::MaterialAssetDesc>(bytes,
                                                           kKindMaterialAsset,
                                                           readMaterialAssetJson,
                                                           diagnostic);
}

std::vector<uint8_t> serializeMaterialVariant(const rendering::MaterialVariantDesc& material) {
  return serializeJsonAsset(kKindMaterialVariant, materialVariantJson(material));
}

std::optional<rendering::MaterialVariantDesc> deserializeMaterialVariant(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic) {
  return deserializeJsonAsset<rendering::MaterialVariantDesc>(bytes,
                                                             kKindMaterialVariant,
                                                             readMaterialVariantJson,
                                                             diagnostic);
}

std::vector<uint8_t> serializeParticleEffect(const visual::particles::ParticleEffectAsset& effect) {
  return serializeJsonAsset(kKindParticleEffect, particleEffectJson(effect));
}

std::optional<visual::particles::ParticleEffectAsset> deserializeParticleEffect(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic) {
  return deserializeJsonAsset<visual::particles::ParticleEffectAsset>(bytes,
                                                              kKindParticleEffect,
                                                              readParticleEffectJson,
                                                              diagnostic);
}

std::vector<uint8_t> serializeGltfScene(const GltfSceneAsset& scene) {
  return serializeJsonAsset(kKindGltfScene, gltfSceneJson(scene));
}

std::optional<GltfSceneAsset> deserializeGltfScene(const std::vector<uint8_t>& bytes,
                                                   std::string* diagnostic) {
  return deserializeJsonAsset<GltfSceneAsset>(bytes,
                                              kKindGltfScene,
                                              readGltfSceneJson,
                                              diagnostic);
}

std::vector<uint8_t> serializeAnimationClip(const world::AnimationClip& clip) {
  return serializeAnimationClipBinary(clip);
}

std::optional<world::AnimationClip> deserializeAnimationClip(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic) {
  return deserializeAnimationClipBinary(bytes, diagnostic);
}

std::vector<uint8_t> serializeSkeleton(const world::Skeleton& skeleton) {
  return serializeJsonAsset(kKindSkeleton, skeletonJson(skeleton));
}

std::optional<world::Skeleton> deserializeSkeleton(const std::vector<uint8_t>& bytes,
                                                       std::string* diagnostic) {
  return deserializeJsonAsset<world::Skeleton>(bytes,
                                                   kKindSkeleton,
                                                   readSkeletonJson,
                                                   diagnostic);
}

std::vector<uint8_t> serializeSkin(const world::Skin& skin) {
  return serializeJsonAsset(kKindSkin, skinJson(skin));
}

std::optional<world::Skin> deserializeSkin(const std::vector<uint8_t>& bytes,
                                               std::string* diagnostic) {
  return deserializeJsonAsset<world::Skin>(bytes,
                                               kKindSkin,
                                               readSkinJson,
                                               diagnostic);
}

}  // namespace karma::assets::detail
