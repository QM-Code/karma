#include "karma/assets.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <system_error>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

#include "karma/assets.h"

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace karma::assets {

namespace {

using Json = nlohmann::json;

bool fail(std::string* diagnostic, std::string message) {
  if (diagnostic != nullptr) {
    *diagnostic = std::move(message);
  }
  return false;
}

std::filesystem::path resolvePath(const std::filesystem::path& base,
                                  const std::string& value) {
  std::filesystem::path path(value);
  if (path.is_relative()) {
    path = base / path;
  }
  return path.lexically_normal();
}

bool readJsonFile(const std::filesystem::path& path, Json& out, std::string* diagnostic) {
  std::ifstream file(path);
  if (!file) {
    return fail(diagnostic, "Failed to open material file: " + path.string());
  }
  try {
    file >> out;
  } catch (const std::exception& e) {
    return fail(diagnostic, std::string("Failed to parse material JSON: ") + e.what());
  }
  if (!out.is_object()) {
    return fail(diagnostic, "Material file root must be an object");
  }
  if (!out.contains("version") || !out["version"].is_number_integer() ||
      out["version"].get<int>() != 2) {
    return fail(diagnostic, "Material file version must be integer 2");
  }
  return true;
}

bool fieldAllowed(const Json& object,
                  std::initializer_list<std::string_view> names,
                  std::string_view section,
                  std::string* diagnostic) {
  for (const auto& [name, value] : object.items()) {
    (void)value;
    bool found = false;
    for (std::string_view allowed : names) {
      if (name == allowed) {
        found = true;
        break;
      }
    }
    if (!found) {
      return fail(diagnostic,
                  std::string("unsupported ") + std::string(section) + " field: " + name);
    }
  }
  return true;
}

bool readFloat(const Json& object, const char* key, float& out, std::string* diagnostic) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_number()) {
    return fail(diagnostic, std::string("Expected numeric field: ") + key);
  }
  out = it->get<float>();
  return true;
}

bool readBool(const Json& object, const char* key, bool& out, std::string* diagnostic) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_boolean()) {
    return fail(diagnostic, std::string("Expected boolean field: ") + key);
  }
  out = it->get<bool>();
  return true;
}

bool readColorValue(const Json& value, rendering::Color& out) {
  if (!value.is_array() || value.size() != 4u) {
    return false;
  }
  for (const Json& element : value) {
    if (!element.is_number()) {
      return false;
    }
  }
  out = rendering::Color{
      value[0].get<float>(),
      value[1].get<float>(),
      value[2].get<float>(),
      value[3].get<float>(),
  };
  return true;
}

bool readColor(const Json& object,
               const char* key,
               rendering::Color& out,
               std::string* diagnostic) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!readColorValue(*it, out)) {
    return fail(diagnostic, std::string("Expected four-number color field: ") + key);
  }
  return true;
}

bool readVec3(const Json& object, const char* key, glm::vec3& out, std::string* diagnostic) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_array() || it->size() != 3u ||
      !(*it)[0].is_number() || !(*it)[1].is_number() || !(*it)[2].is_number()) {
    return fail(diagnostic, std::string("Expected three-number vector field: ") + key);
  }
  out = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
  return true;
}

bool parsePipeline(const Json& root,
                   const std::filesystem::path& base_dir,
                   rendering::MaterialPipelineDesc& out,
                   std::string* diagnostic) {
  const auto it = root.find("pipeline");
  if (it == root.end()) {
    return true;
  }
  if (!it->is_object()) {
    return fail(diagnostic, "pipeline must be an object");
  }
  const Json& pipeline = *it;
  if (!fieldAllowed(pipeline,
                    {"name",
                     "vertex",
                     "fragment",
                     "vertex_entry",
                     "fragment_entry",
                     "defines"},
                    "pipeline",
                    diagnostic)) {
    return false;
  }

  out.name = "standard";
  if (const auto name_it = pipeline.find("name"); name_it != pipeline.end()) {
    if (!name_it->is_string()) {
      return fail(diagnostic, "pipeline.name must be a string");
    }
    out.name = name_it->get<std::string>();
  }
  const bool built_in = out.name == "standard" ||
                        out.name == "foliage" ||
                        out.name == "energy_shell" ||
                        out.name == "wave_volume" ||
                        out.name == "screen_wave" ||
                        out.name == "sphere_halo" ||
                        out.name == "sphere_glow_volume" ||
                        out.name == "volumetric_solid";
  if (!built_in && out.name != "custom") {
    return fail(diagnostic, "pipeline.name is not a supported built-in pipeline or 'custom'");
  }

  auto read_path = [&](const char* key, std::filesystem::path& dst) {
    const auto path_it = pipeline.find(key);
    if (path_it == pipeline.end()) {
      return true;
    }
    if (!path_it->is_string()) {
      return fail(diagnostic, std::string("pipeline.") + key + " must be a string");
    }
    dst = resolvePath(base_dir, path_it->get<std::string>());
    return true;
  };
  if (!read_path("vertex", out.vertex_shader_path) ||
      !read_path("fragment", out.fragment_shader_path)) {
    return false;
  }
  if (out.name == "custom" &&
      (out.vertex_shader_path.empty() || out.fragment_shader_path.empty())) {
    return fail(diagnostic, "custom material pipelines require vertex and fragment shader paths");
  }
  if (out.name != "custom" &&
      (!out.vertex_shader_path.empty() || !out.fragment_shader_path.empty())) {
    return fail(diagnostic, "shader paths are only valid for custom material pipelines");
  }
  if (out.name == "custom") {
    auto require_shader_file = [&](const char* label, const std::filesystem::path& shader_path) {
      std::ifstream shader(shader_path);
      if (!shader) {
        return fail(diagnostic,
                    std::string("custom material pipeline ") + label +
                        " shader is missing or unreadable: " + shader_path.string());
      }
      return true;
    };
    if (!require_shader_file("vertex", out.vertex_shader_path) ||
        !require_shader_file("fragment", out.fragment_shader_path)) {
      return false;
    }
  }

  auto read_entry = [&](const char* key, std::string& dst) {
    const auto entry_it = pipeline.find(key);
    if (entry_it == pipeline.end()) {
      return true;
    }
    if (!entry_it->is_string()) {
      return fail(diagnostic, std::string("pipeline.") + key + " must be a string");
    }
    dst = entry_it->get<std::string>();
    return true;
  };
  if (!read_entry("vertex_entry", out.vertex_entry_point) ||
      !read_entry("fragment_entry", out.fragment_entry_point)) {
    return false;
  }

  if (const auto defines_it = pipeline.find("defines"); defines_it != pipeline.end()) {
    if (!defines_it->is_array()) {
      return fail(diagnostic, "pipeline.defines must be an array");
    }
    out.defines.clear();
    for (const Json& define : *defines_it) {
      if (!define.is_string()) {
        return fail(diagnostic, "pipeline.defines entries must be strings");
      }
      out.defines.push_back(define.get<std::string>());
    }
  }
  return true;
}

bool parseSurface(const Json& root, rendering::MaterialDesc& out, std::string* diagnostic) {
  const auto it = root.find("surface");
  if (it == root.end()) {
    return true;
  }
  if (!it->is_object()) {
    return fail(diagnostic, "surface must be an object");
  }
  const Json& surface = *it;
  if (!fieldAllowed(surface,
                    {"base_color",
                     "emissive_color",
                     "metallic",
                     "roughness",
                     "normal_scale",
                     "normal_map_convention",
                     "occlusion_strength",
                     "emissive_strength",
                     "specular_factor",
                     "specular_color",
                     "clearcoat",
                     "clearcoat_roughness",
                     "sheen_color",
                     "sheen_roughness",
                     "anisotropy",
                     "transmission",
                     "ior",
                     "thickness",
                     "attenuation_distance",
                     "attenuation_color",
                     "unlit",
                     "analytic_sphere_normals"},
                    "surface",
                    diagnostic)) {
    return false;
  }
  if (!readColor(surface, "base_color", out.base_color, diagnostic) ||
      !readColor(surface, "emissive_color", out.emissive_color, diagnostic) ||
      !readFloat(surface, "metallic", out.metallic, diagnostic) ||
      !readFloat(surface, "roughness", out.roughness, diagnostic) ||
      !readFloat(surface, "normal_scale", out.normal_scale, diagnostic) ||
      !readFloat(surface, "occlusion_strength", out.occlusion_strength, diagnostic) ||
      !readFloat(surface, "emissive_strength", out.emissive_strength, diagnostic) ||
      !readFloat(surface, "specular_factor", out.specular_factor, diagnostic) ||
      !readColor(surface, "specular_color", out.specular_color, diagnostic) ||
      !readFloat(surface, "clearcoat", out.clearcoat, diagnostic) ||
      !readFloat(surface, "clearcoat_roughness", out.clearcoat_roughness, diagnostic) ||
      !readColor(surface, "sheen_color", out.sheen_color, diagnostic) ||
      !readFloat(surface, "sheen_roughness", out.sheen_roughness, diagnostic) ||
      !readFloat(surface, "anisotropy", out.anisotropy, diagnostic) ||
      !readFloat(surface, "transmission", out.transmission, diagnostic) ||
      !readFloat(surface, "ior", out.ior, diagnostic) ||
      !readFloat(surface, "thickness", out.thickness, diagnostic) ||
      !readFloat(surface, "attenuation_distance", out.attenuation_distance, diagnostic) ||
      !readColor(surface, "attenuation_color", out.attenuation_color, diagnostic) ||
      !readBool(surface, "unlit", out.unlit, diagnostic) ||
      !readBool(surface, "analytic_sphere_normals", out.analytic_sphere_normals, diagnostic)) {
    return false;
  }
  if (const auto convention_it = surface.find("normal_map_convention");
      convention_it != surface.end()) {
    if (!convention_it->is_string()) {
      return fail(diagnostic, "surface.normal_map_convention must be a string");
    }
    const std::string convention = convention_it->get<std::string>();
    if (convention == "opengl") {
      out.normal_map_convention =
          rendering::MaterialDesc::NormalMapConvention::OpenGL;
    } else if (convention == "directx") {
      out.normal_map_convention =
          rendering::MaterialDesc::NormalMapConvention::DirectX;
    } else {
      return fail(diagnostic,
                  "surface.normal_map_convention must be 'opengl' or 'directx'");
    }
  }
  if (!std::isfinite(out.specular_factor) || out.specular_factor < 0.0f ||
      out.specular_factor > 1.0f) {
    return fail(diagnostic, "surface.specular_factor must be finite and in [0, 1]");
  }
  const auto valid_specular_channel = [](float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
  };
  if (!valid_specular_channel(out.specular_color.r) ||
      !valid_specular_channel(out.specular_color.g) ||
      !valid_specular_channel(out.specular_color.b) ||
      !valid_specular_channel(out.specular_color.a)) {
    return fail(diagnostic, "surface.specular_color channels must be finite and in [0, 1]");
  }
  return true;
}

bool parseRenderState(const Json& root, rendering::MaterialDesc& out, std::string* diagnostic) {
  const auto it = root.find("render_state");
  if (it == root.end()) {
    return true;
  }
  if (!it->is_object()) {
    return fail(diagnostic, "render_state must be an object");
  }
  const Json& state = *it;
  if (!fieldAllowed(state,
                    {"transparent",
                     "alpha_mode",
                     "alpha_cutoff",
                     "alpha_softness",
                     "alpha_dither",
                     "alpha_to_coverage",
                     "depth_test",
                     "depth_write",
                     "wireframe",
                     "double_sided",
                     "blend_mode"},
                    "render_state",
                    diagnostic)) {
    return false;
  }
  if (!readBool(state, "transparent", out.transparent, diagnostic) ||
      !readFloat(state, "alpha_cutoff", out.alpha_cutoff, diagnostic) ||
      !readFloat(state, "alpha_softness", out.alpha_softness, diagnostic) ||
      !readBool(state, "alpha_dither", out.alpha_dither, diagnostic) ||
      !readBool(state, "alpha_to_coverage", out.alpha_to_coverage, diagnostic) ||
      !readBool(state, "depth_test", out.depth_test, diagnostic) ||
      !readBool(state, "depth_write", out.depth_write, diagnostic) ||
      !readBool(state, "wireframe", out.wireframe, diagnostic) ||
      !readBool(state, "double_sided", out.double_sided, diagnostic)) {
    return false;
  }
  if (const auto blend_it = state.find("blend_mode"); blend_it != state.end()) {
    if (!blend_it->is_string()) {
      return fail(diagnostic, "render_state.blend_mode must be a string");
    }
    const std::string blend = blend_it->get<std::string>();
    if (blend == "alpha") {
      out.blend_mode = rendering::MaterialDesc::BlendMode::Alpha;
    } else if (blend == "additive") {
      out.blend_mode = rendering::MaterialDesc::BlendMode::Additive;
    } else {
      return fail(diagnostic, "render_state.blend_mode must be 'alpha' or 'additive'");
    }
  }
  if (const auto alpha_it = state.find("alpha_mode"); alpha_it != state.end()) {
    if (!alpha_it->is_string()) {
      return fail(diagnostic, "render_state.alpha_mode must be a string");
    }
    const std::string alpha = alpha_it->get<std::string>();
    if (alpha == "opaque") {
      out.alpha_mode = rendering::MaterialDesc::AlphaMode::Opaque;
      out.transparent = false;
    } else if (alpha == "masked") {
      out.alpha_mode = rendering::MaterialDesc::AlphaMode::Masked;
      out.transparent = false;
    } else if (alpha == "blend") {
      out.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
      out.transparent = true;
    } else {
      return fail(diagnostic, "render_state.alpha_mode must be 'opaque', 'masked', or 'blend'");
    }
  } else if (out.transparent && out.alpha_mode == rendering::MaterialDesc::AlphaMode::Opaque) {
    out.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
  }
  return true;
}

bool parseParameterValue(const Json& value,
                         rendering::MaterialParameterValue& out,
                         std::string* diagnostic) {
  if (value.is_boolean()) {
    out = value.get<bool>();
    return true;
  }
  if (value.is_number()) {
    out = value.get<float>();
    return true;
  }
  if (value.is_string()) {
    out = value.get<std::string>();
    return true;
  }
  if (value.is_array()) {
    for (const Json& element : value) {
      if (!element.is_number()) {
        return fail(diagnostic, "material parameter arrays must contain only numbers");
      }
    }
    if (value.size() == 2u) {
      out = glm::vec2(value[0].get<float>(), value[1].get<float>());
      return true;
    }
    if (value.size() == 3u) {
      out = glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
      return true;
    }
    if (value.size() == 4u) {
      out = rendering::Color{
          value[0].get<float>(),
          value[1].get<float>(),
          value[2].get<float>(),
          value[3].get<float>(),
      };
      return true;
    }
  }
  return fail(diagnostic, "unsupported material parameter value");
}

bool parseParams(const Json& root,
                 std::unordered_map<std::string, rendering::MaterialParameterValue>& out,
                 std::string* diagnostic) {
  const auto it = root.find("params");
  if (it == root.end()) {
    return true;
  }
  if (!it->is_object()) {
    return fail(diagnostic, "params must be an object");
  }
  for (const auto& [name, value] : it->items()) {
    rendering::MaterialParameterValue parsed{};
    if (!parseParameterValue(value, parsed, diagnostic)) {
      return false;
    }
    out[name] = std::move(parsed);
  }
  return true;
}

bool parseSectionAsParams(const Json& root,
                          const char* section,
                          std::unordered_map<std::string, rendering::MaterialParameterValue>& out,
                          std::string* diagnostic) {
  const auto it = root.find(section);
  if (it == root.end()) {
    return true;
  }
  if (!it->is_object()) {
    return fail(diagnostic, std::string(section) + " must be an object");
  }
  for (const auto& [name, value] : it->items()) {
    rendering::MaterialParameterValue parsed{};
    if (!parseParameterValue(value, parsed, diagnostic)) {
      return false;
    }
    out[name] = std::move(parsed);
  }
  return true;
}

bool parseTextures(const Json& root,
                   std::unordered_map<std::string, std::string>& out,
                   std::string* diagnostic) {
  const auto it = root.find("textures");
  if (it == root.end()) {
    return true;
  }
  if (!it->is_object()) {
    return fail(diagnostic, "textures must be an object");
  }
  for (const auto& [name, value] : it->items()) {
    if (!value.is_string()) {
      return fail(diagnostic, "texture values must be strings");
    }
    const std::string texture_key = value.get<std::string>();
    if (!AssetRegistry::isValidAssetKey(texture_key)) {
      return fail(diagnostic,
                  "invalid texture asset key '" + texture_key + "': " +
                      AssetRegistry::assetKeyValidationError(texture_key));
    }
    out[name] = texture_key;
  }
  return true;
}

bool isVariantDocument(const Json& root) {
  if (const auto kind_it = root.find("kind"); kind_it != root.end() && kind_it->is_string()) {
    return kind_it->get<std::string>() == "variant";
  }
  return root.contains("base");
}

Json colorJson(const rendering::Color& color) {
  return Json::array({color.r, color.g, color.b, color.a});
}

Json parameterJson(const rendering::MaterialParameterValue& value) {
  return std::visit(
      [](const auto& typed) -> Json {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, rendering::Color>) {
          return colorJson(typed);
        } else if constexpr (std::is_same_v<T, glm::vec2>) {
          return Json::array({typed.x, typed.y});
        } else if constexpr (std::is_same_v<T, glm::vec3>) {
          return Json::array({typed.x, typed.y, typed.z});
        } else if constexpr (std::is_same_v<T, glm::vec4>) {
          return Json::array({typed.x, typed.y, typed.z, typed.w});
        } else {
          return Json(typed);
        }
      },
      value);
}

Json parametersJson(
    const std::unordered_map<std::string, rendering::MaterialParameterValue>& params) {
  Json json = Json::object();
  for (const auto& [name, value] : params) {
    json[name] = parameterJson(value);
  }
  return json;
}

Json texturesJson(const std::unordered_map<std::string, std::string>& textures) {
  Json json = Json::object();
  for (const auto& [name, key] : textures) {
    json[name] = key;
  }
  return json;
}

std::string portableShaderPath(const std::filesystem::path& path,
                               const std::filesystem::path& base_dir) {
  if (path.empty()) {
    return {};
  }
  if (path.is_relative()) {
    return path.generic_string();
  }
  const std::filesystem::path relative = path.lexically_relative(base_dir);
  return relative.empty() ? path.generic_string() : relative.generic_string();
}

Json materialAssetFileJson(const rendering::MaterialAssetDesc& material,
                           const std::filesystem::path& base_dir) {
  const auto convention = material.surface.normal_map_convention ==
                                  rendering::MaterialDesc::NormalMapConvention::DirectX
                              ? "directx"
                              : "opengl";
  Json pipeline{
      {"name", material.pipeline.name},
      {"vertex_entry", material.pipeline.vertex_entry_point},
      {"fragment_entry", material.pipeline.fragment_entry_point},
      {"defines", material.pipeline.defines},
  };
  if (!material.pipeline.vertex_shader_path.empty()) {
    pipeline["vertex"] =
        portableShaderPath(material.pipeline.vertex_shader_path, base_dir);
  }
  if (!material.pipeline.fragment_shader_path.empty()) {
    pipeline["fragment"] =
        portableShaderPath(material.pipeline.fragment_shader_path, base_dir);
  }

  Json json{
      {"version", 2},
      {"pipeline", std::move(pipeline)},
      {"surface",
       Json{{"base_color", colorJson(material.surface.base_color)},
            {"emissive_color", colorJson(material.surface.emissive_color)},
            {"metallic", material.surface.metallic},
            {"roughness", material.surface.roughness},
            {"normal_scale", material.surface.normal_scale},
            {"normal_map_convention", convention},
            {"occlusion_strength", material.surface.occlusion_strength},
            {"emissive_strength", material.surface.emissive_strength},
            {"specular_factor", material.surface.specular_factor},
            {"specular_color", colorJson(material.surface.specular_color)},
            {"clearcoat", material.surface.clearcoat},
            {"clearcoat_roughness", material.surface.clearcoat_roughness},
            {"sheen_color", colorJson(material.surface.sheen_color)},
            {"sheen_roughness", material.surface.sheen_roughness},
            {"anisotropy", material.surface.anisotropy},
            {"transmission", material.surface.transmission},
            {"ior", material.surface.ior},
            {"thickness", material.surface.thickness},
            {"attenuation_distance", material.surface.attenuation_distance},
            {"attenuation_color", colorJson(material.surface.attenuation_color)},
            {"unlit", material.surface.unlit},
            {"analytic_sphere_normals", material.surface.analytic_sphere_normals}}},
      {"render_state",
       Json{{"transparent", material.surface.transparent},
            {"alpha_mode",
             material.surface.alpha_mode == rendering::MaterialDesc::AlphaMode::Masked
                 ? "masked"
                 : material.surface.alpha_mode == rendering::MaterialDesc::AlphaMode::Blend
                       ? "blend"
                       : "opaque"},
            {"alpha_cutoff", material.surface.alpha_cutoff},
            {"alpha_softness", material.surface.alpha_softness},
            {"alpha_dither", material.surface.alpha_dither},
            {"alpha_to_coverage", material.surface.alpha_to_coverage},
            {"depth_test", material.surface.depth_test},
            {"depth_write", material.surface.depth_write},
            {"wireframe", material.surface.wireframe},
            {"double_sided", material.surface.double_sided},
            {"blend_mode",
             material.surface.blend_mode == rendering::MaterialDesc::BlendMode::Additive
                 ? "additive"
                 : "alpha"}}},
      {"params", parametersJson(material.params)},
      {"textures", texturesJson(material.textures)},
  };
  if (!std::isfinite(material.surface.attenuation_distance)) {
    json["surface"].erase("attenuation_distance");
  }
  return json;
}

Json materialVariantFileJson(const rendering::MaterialVariantDesc& material) {
  return Json{
      {"version", 2},
      {"kind", "variant"},
      {"base", material.base_material_key},
      {"params", parametersJson(material.params)},
      {"textures", texturesJson(material.textures)},
  };
}

std::filesystem::path temporaryMaterialPath(const std::filesystem::path& path) {
  static std::atomic<uint64_t> sequence{0u};
  const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return path.parent_path() /
         (path.filename().string() + ".tmp." + std::to_string(timestamp) + "." +
          std::to_string(sequence.fetch_add(1u, std::memory_order_relaxed)));
}

void removeTemporaryMaterial(const std::filesystem::path& path) {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

MaterialSaveResult saveMaterialJsonAtomic(const Json& json,
                                          const std::filesystem::path& path) {
  MaterialSaveResult result{.path = path};
  if (path.empty()) {
    result.diagnostic = "material save path must not be empty";
    return result;
  }
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      result.diagnostic = "failed to create material directory: " + ec.message();
      return result;
    }
  }

  const std::filesystem::path temporary = temporaryMaterialPath(path);
  try {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      result.diagnostic = "failed to open temporary material file: " + temporary.string();
      return result;
    }
    stream << json.dump(2) << '\n';
    stream.flush();
    if (!stream) {
      removeTemporaryMaterial(temporary);
      result.diagnostic = "failed to write temporary material file: " + temporary.string();
      return result;
    }
    stream.close();
    if (!stream) {
      removeTemporaryMaterial(temporary);
      result.diagnostic = "failed to close temporary material file: " + temporary.string();
      return result;
    }
  } catch (const std::exception& error) {
    removeTemporaryMaterial(temporary);
    result.diagnostic = std::string("failed to serialize material: ") + error.what();
    return result;
  }

#if defined(_WIN32)
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
  }
#else
  std::filesystem::rename(temporary, path, ec);
#endif
  if (ec) {
    removeTemporaryMaterial(temporary);
    result.diagnostic = "failed to atomically replace material file: " + ec.message();
  }
  return result;
}

}  // namespace

std::optional<rendering::MaterialAssetDesc> loadMaterialAssetDesc(
    const std::filesystem::path& path,
    std::string* diagnostic) {
  Json root;
  if (!readJsonFile(path, root, diagnostic)) {
    return std::nullopt;
  }
  if (isVariantDocument(root)) {
    fail(diagnostic, "Material file describes a variant, not an asset");
    return std::nullopt;
  }

  rendering::MaterialAssetDesc desc{};
  const std::filesystem::path base_dir = path.parent_path();
  if (!parsePipeline(root, base_dir, desc.pipeline, diagnostic) ||
      !parseSurface(root, desc.surface, diagnostic) ||
      !parseRenderState(root, desc.surface, diagnostic) ||
      !parseParams(root, desc.params, diagnostic) ||
      !parseTextures(root, desc.textures, diagnostic)) {
    return std::nullopt;
  }
  return desc;
}

std::optional<rendering::MaterialVariantDesc> loadMaterialVariantDesc(
    const std::filesystem::path& path,
    std::string* diagnostic) {
  Json root;
  if (!readJsonFile(path, root, diagnostic)) {
    return std::nullopt;
  }
  if (!isVariantDocument(root)) {
    fail(diagnostic, "Material file describes an asset, not a variant");
    return std::nullopt;
  }
  const auto base_it = root.find("base");
  if (base_it == root.end() || !base_it->is_string() || base_it->get<std::string>().empty()) {
    fail(diagnostic, "material variants require a non-empty base string");
    return std::nullopt;
  }

  rendering::MaterialVariantDesc desc{};
  desc.base_material_key = base_it->get<std::string>();
  if (!AssetRegistry::isValidAssetKey(desc.base_material_key)) {
    fail(diagnostic,
         "invalid base material key '" + desc.base_material_key + "': " +
             AssetRegistry::assetKeyValidationError(desc.base_material_key));
    return std::nullopt;
  }
  if (!parseSectionAsParams(root, "surface", desc.params, diagnostic) ||
      !parseSectionAsParams(root, "render_state", desc.params, diagnostic) ||
      !parseParams(root, desc.params, diagnostic) ||
      !parseTextures(root, desc.textures, diagnostic)) {
    return std::nullopt;
  }
  return desc;
}

MaterialSaveResult saveMaterialAssetDesc(
    const rendering::MaterialAssetDesc& material,
    const std::filesystem::path& path) {
  MaterialSaveResult result{.path = path};
  if (material.imported_material != nullptr ||
      !material.material_asset_path.empty() ||
      material.material_asset_index != std::numeric_limits<uint32_t>::max()) {
    result.diagnostic =
        "import-backed material descriptors cannot be saved as standalone .mat files";
    return result;
  }
  const Json json = materialAssetFileJson(material, path.parent_path());
  rendering::MaterialAssetDesc validated{};
  std::string diagnostic;
  if (!parsePipeline(json, path.parent_path(), validated.pipeline, &diagnostic) ||
      !parseSurface(json, validated.surface, &diagnostic) ||
      !parseRenderState(json, validated.surface, &diagnostic) ||
      !parseParams(json, validated.params, &diagnostic) ||
      !parseTextures(json, validated.textures, &diagnostic)) {
    result.diagnostic = diagnostic.empty() ? "material validation failed" : diagnostic;
    return result;
  }
  return saveMaterialJsonAtomic(json, path);
}

MaterialSaveResult saveMaterialVariantDesc(
    const rendering::MaterialVariantDesc& material,
    const std::filesystem::path& path) {
  MaterialSaveResult result{.path = path};
  const Json json = materialVariantFileJson(material);
  if (material.base_material_key.empty() ||
      !AssetRegistry::isValidAssetKey(material.base_material_key)) {
    result.diagnostic = "material variant requires a valid base material key";
    return result;
  }
  rendering::MaterialVariantDesc validated{};
  std::string diagnostic;
  if (!parseParams(json, validated.params, &diagnostic) ||
      !parseTextures(json, validated.textures, &diagnostic)) {
    result.diagnostic = diagnostic.empty() ? "material variant validation failed" : diagnostic;
    return result;
  }
  return saveMaterialJsonAtomic(json, path);
}

MaterialLoadResult loadMaterialFile(AssetRegistry& assets,
                                    const std::string& key,
                                    const std::filesystem::path& path) {
  MaterialLoadResult result{};
  std::string diagnostic;
  Json root;
  if (!readJsonFile(path, root, &diagnostic)) {
    result.diagnostic = std::move(diagnostic);
    return result;
  }

  if (isVariantDocument(root)) {
    auto variant = loadMaterialVariantDesc(path, &diagnostic);
    if (!variant.has_value()) {
      result.diagnostic = std::move(diagnostic);
      return result;
    }
    if (!assets.registerMaterialVariant(key, std::move(*variant))) {
      result.diagnostic = AssetRegistry::assetKeyValidationError(key);
      return result;
    }
  } else {
    auto asset = loadMaterialAssetDesc(path, &diagnostic);
    if (!asset.has_value()) {
      result.diagnostic = std::move(diagnostic);
      return result;
    }
    if (!assets.registerMaterialAsset(key, std::move(*asset))) {
      result.diagnostic = AssetRegistry::assetKeyValidationError(key);
      return result;
    }
  }
  result.success = true;
  return result;
}

}  // namespace karma::assets
