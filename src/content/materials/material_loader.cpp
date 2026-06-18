#include "karma/content/materials/material_loader.h"

#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

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
      out["version"].get<int>() != 1) {
    return fail(diagnostic, "Material file version must be integer 1");
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
  std::string type = "standard";
  if (const auto type_it = pipeline.find("type"); type_it != pipeline.end()) {
    if (!type_it->is_string()) {
      return fail(diagnostic, "pipeline.type must be a string");
    }
    type = type_it->get<std::string>();
  }
  if (type == "standard") {
    out.type = renderer::MaterialPipelineDesc::Type::Standard;
  } else if (type == "custom") {
    out.type = renderer::MaterialPipelineDesc::Type::Custom;
  } else {
    return fail(diagnostic, "pipeline.type must be 'standard' or 'custom'");
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
  if (out.type == renderer::MaterialPipelineDesc::Type::Custom &&
      (out.vertex_shader_path.empty() || out.fragment_shader_path.empty())) {
    return fail(diagnostic, "custom material pipelines require vertex and fragment shader paths");
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
      !readColor(surface, "attenuation_color", out.attenuation_color, diagnostic) ||
      !readBool(surface, "unlit", out.unlit, diagnostic) ||
      !readBool(surface, "transparent", out.transparent, diagnostic)) {
    return false;
  }
  if (const auto shading_it = surface.find("shading_model"); shading_it != surface.end()) {
    if (!shading_it->is_string()) {
      return fail(diagnostic, "surface.shading_model must be a string");
    }
    const std::string shading = shading_it->get<std::string>();
    if (shading == "standard") out.shading_model = renderer::MaterialDesc::ShadingModel::Standard;
    else if (shading == "energy_shell") out.shading_model = renderer::MaterialDesc::ShadingModel::EnergyShell;
    else if (shading == "wave_volume") out.shading_model = renderer::MaterialDesc::ShadingModel::WaveVolume;
    else if (shading == "sphere_halo") out.shading_model = renderer::MaterialDesc::ShadingModel::SphereHalo;
    else if (shading == "screen_wave") out.shading_model = renderer::MaterialDesc::ShadingModel::ScreenWave;
    else if (shading == "sphere_glow_volume") out.shading_model = renderer::MaterialDesc::ShadingModel::SphereGlowVolume;
    else if (shading == "volumetric_solid") out.shading_model = renderer::MaterialDesc::ShadingModel::VolumetricSolid;
    else return fail(diagnostic, "Unknown surface.shading_model value");
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
  if (!readBool(state, "transparent", out.transparent, diagnostic) ||
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
                   const std::filesystem::path& base_dir,
                   std::unordered_map<std::string, std::filesystem::path>& out,
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
    out[name] = resolvePath(base_dir, value.get<std::string>());
  }
  return true;
}

bool isInstanceDocument(const Json& root) {
  if (const auto kind_it = root.find("kind"); kind_it != root.end() && kind_it->is_string()) {
    return kind_it->get<std::string>() == "instance";
  }
  return root.contains("parent");
}

}  // namespace

std::optional<renderer::MaterialAssetDesc> loadMaterialAssetDesc(
    const std::filesystem::path& path,
    std::string* diagnostic) {
  Json root;
  if (!readJsonFile(path, root, diagnostic)) {
    return std::nullopt;
  }
  if (isInstanceDocument(root)) {
    fail(diagnostic, "Material file describes an instance, not an asset");
    return std::nullopt;
  }

  renderer::MaterialAssetDesc desc{};
  const std::filesystem::path base_dir = path.parent_path();
  if (!parsePipeline(root, base_dir, desc.pipeline, diagnostic) ||
      !parseSurface(root, desc.surface, diagnostic) ||
      !parseRenderState(root, desc.surface, diagnostic) ||
      !parseParams(root, desc.params, diagnostic) ||
      !parseTextures(root, base_dir, desc.textures, diagnostic)) {
    return std::nullopt;
  }
  return desc;
}

std::optional<renderer::MaterialInstanceDesc> loadMaterialInstanceDesc(
    const std::filesystem::path& path,
    std::string* diagnostic) {
  Json root;
  if (!readJsonFile(path, root, diagnostic)) {
    return std::nullopt;
  }
  if (!isInstanceDocument(root)) {
    fail(diagnostic, "Material file describes an asset, not an instance");
    return std::nullopt;
  }
  const auto parent_it = root.find("parent");
  if (parent_it == root.end() || !parent_it->is_string() || parent_it->get<std::string>().empty()) {
    fail(diagnostic, "material instances require a non-empty parent string");
    return std::nullopt;
  }

  renderer::MaterialInstanceDesc desc{};
  desc.parent_material_key = parent_it->get<std::string>();
  const std::filesystem::path base_dir = path.parent_path();
  if (!parseSectionAsParams(root, "surface", desc.params, diagnostic) ||
      !parseSectionAsParams(root, "render_state", desc.params, diagnostic) ||
      !parseParams(root, desc.params, diagnostic) ||
      !parseTextures(root, base_dir, desc.textures, diagnostic)) {
    return std::nullopt;
  }
  return desc;
}

MaterialLoadResult loadMaterialFile(renderer::MaterialLibrary& library,
                                    const std::string& key,
                                    const std::filesystem::path& path) {
  MaterialLoadResult result{};
  std::string diagnostic;
  Json root;
  if (!readJsonFile(path, root, &diagnostic)) {
    result.diagnostic = std::move(diagnostic);
    return result;
  }

  if (isInstanceDocument(root)) {
    auto instance = loadMaterialInstanceDesc(path, &diagnostic);
    if (!instance.has_value()) {
      result.diagnostic = std::move(diagnostic);
      return result;
    }
    library.registerMaterialInstance(key, std::move(*instance));
  } else {
    auto asset = loadMaterialAssetDesc(path, &diagnostic);
    if (!asset.has_value()) {
      result.diagnostic = std::move(diagnostic);
      return result;
    }
    library.registerMaterialAsset(key, std::move(*asset));
  }
  result.success = true;
  return result;
}

}  // namespace karma::content
