#include "karma/content/materials/material_loader.h"

#include <fstream>
#include <initializer_list>
#include <utility>

#include <nlohmann/json.hpp>

#include "karma/content/assets/asset_registry.h"

namespace karma::content {

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

bool readColorValue(const Json& value, renderer::Color& out) {
  if (!value.is_array() || value.size() != 4u) {
    return false;
  }
  for (const Json& element : value) {
    if (!element.is_number()) {
      return false;
    }
  }
  out = renderer::Color{
      value[0].get<float>(),
      value[1].get<float>(),
      value[2].get<float>(),
      value[3].get<float>(),
  };
  return true;
}

bool readColor(const Json& object,
               const char* key,
               renderer::Color& out,
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
                   renderer::MaterialPipelineDesc& out,
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

bool parseSurface(const Json& root, renderer::MaterialDesc& out, std::string* diagnostic) {
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
                     "occlusion_strength",
                     "emissive_strength",
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
  return true;
}

bool parseRenderState(const Json& root, renderer::MaterialDesc& out, std::string* diagnostic) {
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
      out.blend_mode = renderer::MaterialDesc::BlendMode::Alpha;
    } else if (blend == "additive") {
      out.blend_mode = renderer::MaterialDesc::BlendMode::Additive;
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
      out.alpha_mode = renderer::MaterialDesc::AlphaMode::Opaque;
      out.transparent = false;
    } else if (alpha == "masked") {
      out.alpha_mode = renderer::MaterialDesc::AlphaMode::Masked;
      out.transparent = false;
    } else if (alpha == "blend") {
      out.alpha_mode = renderer::MaterialDesc::AlphaMode::Blend;
      out.transparent = true;
    } else {
      return fail(diagnostic, "render_state.alpha_mode must be 'opaque', 'masked', or 'blend'");
    }
  } else if (out.transparent && out.alpha_mode == renderer::MaterialDesc::AlphaMode::Opaque) {
    out.alpha_mode = renderer::MaterialDesc::AlphaMode::Blend;
  }
  return true;
}

bool parseParameterValue(const Json& value,
                         renderer::MaterialParameterValue& out,
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
      out = renderer::Color{
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
                 std::unordered_map<std::string, renderer::MaterialParameterValue>& out,
                 std::string* diagnostic) {
  const auto it = root.find("params");
  if (it == root.end()) {
    return true;
  }
  if (!it->is_object()) {
    return fail(diagnostic, "params must be an object");
  }
  for (const auto& [name, value] : it->items()) {
    renderer::MaterialParameterValue parsed{};
    if (!parseParameterValue(value, parsed, diagnostic)) {
      return false;
    }
    out[name] = std::move(parsed);
  }
  return true;
}

bool parseSectionAsParams(const Json& root,
                          const char* section,
                          std::unordered_map<std::string, renderer::MaterialParameterValue>& out,
                          std::string* diagnostic) {
  const auto it = root.find(section);
  if (it == root.end()) {
    return true;
  }
  if (!it->is_object()) {
    return fail(diagnostic, std::string(section) + " must be an object");
  }
  for (const auto& [name, value] : it->items()) {
    renderer::MaterialParameterValue parsed{};
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

}  // namespace

std::optional<renderer::MaterialAssetDesc> loadMaterialAssetDesc(
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

  renderer::MaterialAssetDesc desc{};
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

std::optional<renderer::MaterialVariantDesc> loadMaterialVariantDesc(
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

  renderer::MaterialVariantDesc desc{};
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

}  // namespace karma::content
