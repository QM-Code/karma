#include "karma/assets.h"
#include "karma/scenes.h"

#include "asset_cache_serializers.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <future>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <thread>
#include <utility>
#include <vector>

#include <assimp/version.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "karma/assets.h"

#include "asset_texture_internal.h"
#include "asset_source_import.h"
#include "../importers/gltf_scene_import_internal.h"

namespace karma::assets {

namespace {

using Json = nlohmann::json;

constexpr std::string_view kPackageCacheContentVersion =
    "package-cache-v8-scene-assets-imported-texture-paths";

bool envFlagEnabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") != 0 &&
         std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "FALSE") != 0 &&
         std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "OFF") != 0;
}

bool startupDiagnosticsEnabled() {
  static const bool enabled = envFlagEnabled(std::getenv("KARMA_ENGINE_STARTUP_DIAG"));
  return enabled;
}

uint32_t envUint(const char* value, uint32_t fallback) {
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (end == value || parsed == 0ul) {
    return fallback;
  }
  return static_cast<uint32_t>(std::min<unsigned long>(parsed, 1024ul));
}

uint32_t textureRestoreJobLimit() {
  static const uint32_t limit = [] {
    uint32_t fallback = std::thread::hardware_concurrency();
    if (fallback == 0u) {
      fallback = 4u;
    }
    fallback = std::clamp(fallback, 1u, 4u);
    return std::max(1u, envUint(std::getenv("KARMA_ASSET_TEXTURE_RESTORE_JOBS"), fallback));
  }();
  return limit;
}

void logAssetPackageDiag(const std::filesystem::path& manifest_path,
                         const char* stage,
                         core::SteadyClock::time_point start,
                         core::SteadyClock::time_point end) {
  if (!startupDiagnosticsEnabled()) {
    return;
  }
  spdlog::info("Engine startup diag: area=asset_package package='{}' stage={} ms={:.2f}",
               manifest_path.string(),
               stage ? stage : "unknown",
               core::elapsedMilliseconds(start, end));
}

void logAssetPackageDiag(const std::filesystem::path& manifest_path,
                         const char* stage,
                         core::SteadyClock::time_point start,
                         core::SteadyClock::time_point end,
                         std::size_t count) {
  if (!startupDiagnosticsEnabled()) {
    return;
  }
  spdlog::info(
      "Engine startup diag: area=asset_package package='{}' stage={} ms={:.2f} count={}",
      manifest_path.string(),
      stage ? stage : "unknown",
      core::elapsedMilliseconds(start, end),
      count);
}

void logAssetPackageEntryDiag(const std::filesystem::path& manifest_path,
                              const std::string& type,
                              const std::string& key,
                              const std::filesystem::path& source_path,
                              core::SteadyClock::time_point start,
                              core::SteadyClock::time_point end) {
  if (!startupDiagnosticsEnabled()) {
    return;
  }
  spdlog::info(
      "Engine startup diag: area=asset_package_entry package='{}' type={} key='{}' source='{}' ms={:.2f}",
      manifest_path.string(),
      type,
      key,
      source_path.empty() ? std::string{} : source_path.string(),
      core::elapsedMilliseconds(start, end));
}

void logAssetPackageCacheAssetDiag(const std::filesystem::path& manifest_path,
                                   const std::string& type,
                                   const std::string& key,
                                   const std::string& blob_type,
                                   core::SteadyClock::time_point start,
                                   core::SteadyClock::time_point end) {
  if (!startupDiagnosticsEnabled()) {
    return;
  }
  spdlog::info(
      "Engine startup diag: area=asset_package_cache_asset package='{}' type={} blob_type={} key='{}' ms={:.2f}",
      manifest_path.string(),
      type,
      blob_type,
      key,
      core::elapsedMilliseconds(start, end));
}

bool fail(std::string* diagnostic, std::string message) {
  if (diagnostic != nullptr) {
    *diagnostic = std::move(message);
  }
  return false;
}

struct CachedAssetRecord {
  std::string type;
  std::string key;
  std::string blob_key;
  std::string blob_type;
};

bool readCachedAssetRecord(const Json& entry,
                           CachedAssetRecord& out,
                           std::string* diagnostic) {
  if (!entry.is_object() ||
      !entry.contains("type") ||
      !entry.contains("key") ||
      !entry["type"].is_string() ||
      !entry["key"].is_string()) {
    return fail(diagnostic, "package cache asset record is malformed");
  }
  out.type = entry["type"].get<std::string>();
  out.key = entry["key"].get<std::string>();
  out.blob_key = entry.value("blob_key", std::string{});
  out.blob_type = entry.value("blob_type", out.type);
  return true;
}

bool isTextureBlobType(std::string_view blob_type) {
  return blob_type == "texture" || blob_type == "texture_rgba8";
}

std::filesystem::path packageDirectory(const std::filesystem::path& manifest_path) {
  const std::filesystem::path parent = manifest_path.parent_path();
  return parent.empty() ? std::filesystem::path(".") : parent;
}

std::filesystem::path resolveEntryPath(const std::filesystem::path& base,
                                       const std::string& value) {
  std::filesystem::path path(value);
  if (path.is_relative()) {
    path = base / path;
  }
  return path.lexically_normal();
}

bool readRequiredString(const Json& object,
                        const char* field,
                        std::string& out,
                        std::string* diagnostic) {
  const auto it = object.find(field);
  if (it == object.end() || !it->is_string() || it->get<std::string>().empty()) {
    return fail(diagnostic, std::string("asset package entry requires string field: ") + field);
  }
  out = it->get<std::string>();
  return true;
}

bool readRequiredPath(const Json& object,
                      const std::filesystem::path& base,
                      std::filesystem::path& out,
                      std::string* diagnostic) {
  std::string value;
  if (!readRequiredString(object, "path", value, diagnostic)) {
    return false;
  }
  out = resolveEntryPath(base, value);
  return true;
}

bool readJsonObjectFile(const std::filesystem::path& path,
                        Json& out,
                        std::string* diagnostic,
                        std::string_view label) {
  std::ifstream stream(path);
  if (!stream) {
    return fail(diagnostic, "failed to open " + std::string(label) + ": " + path.string());
  }
  try {
    stream >> out;
  } catch (const std::exception& e) {
    return fail(diagnostic,
                "failed to parse " + std::string(label) + " JSON: " + e.what());
  }
  if (!out.is_object()) {
    return fail(diagnostic, std::string(label) + " root must be an object");
  }
  const auto version_it = out.find("version");
  if (version_it == out.end() ||
      (!version_it->is_number_integer() && !version_it->is_number_unsigned()) ||
      version_it->get<int>() != 1) {
    return fail(diagnostic, std::string(label) + " version must be integer 1");
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
                  "unsupported " + std::string(section) + " field: " + name);
    }
  }
  return true;
}

bool readGltfSceneMaterialOverrides(const Json& entry,
                                    world::GltfSceneLoadOptions& out,
                                    std::string* diagnostic) {
  const auto overrides_it = entry.find("material_overrides");
  if (overrides_it == entry.end()) {
    return true;
  }
  if (!overrides_it->is_array()) {
    return fail(diagnostic, "gltf_scene material_overrides must be an array");
  }

  out.material_overrides.clear();
  out.material_overrides.reserve(overrides_it->size());
  for (const Json& override_json : *overrides_it) {
    if (!override_json.is_object()) {
      return fail(diagnostic, "gltf_scene material override must be an object");
    }

    world::GltfSceneLoadOptions::MaterialOverride override{};
    bool has_selector = false;
    if (const auto all_materials_it = override_json.find("all_materials");
        all_materials_it != override_json.end()) {
      if (!all_materials_it->is_boolean()) {
        return fail(diagnostic, "gltf_scene material override all_materials must be boolean");
      }
      override.all_materials = all_materials_it->get<bool>();
      has_selector = has_selector || override.all_materials;
    }
    if (const auto material_index_it = override_json.find("material_index");
        material_index_it != override_json.end()) {
      if (!material_index_it->is_number_unsigned()) {
        return fail(diagnostic, "gltf_scene material override material_index must be unsigned");
      }
      override.material_index = material_index_it->get<uint32_t>();
      has_selector = true;
    }
    if (const auto material_name_it = override_json.find("material_name");
        material_name_it != override_json.end()) {
      if (!material_name_it->is_string() || material_name_it->get<std::string>().empty()) {
        return fail(diagnostic, "gltf_scene material override material_name must be non-empty");
      }
      override.material_name = material_name_it->get<std::string>();
      has_selector = true;
    }
    if (!has_selector) {
      return fail(diagnostic,
                  "gltf_scene material override requires material_index, material_name, or all_materials");
    }

    if (const auto normal_scale_it = override_json.find("normal_scale");
        normal_scale_it != override_json.end()) {
      if (!normal_scale_it->is_number()) {
        return fail(diagnostic, "gltf_scene material override normal_scale must be numeric");
      }
      override.normal_scale = normal_scale_it->get<float>();
      override.has_normal_scale = true;
    }
    if (const auto casts_shadows_it = override_json.find("casts_shadows");
        casts_shadows_it != override_json.end()) {
      if (!casts_shadows_it->is_boolean()) {
        return fail(diagnostic, "gltf_scene material override casts_shadows must be boolean");
      }
      override.casts_shadows = casts_shadows_it->get<bool>();
      override.has_casts_shadows = true;
    }
    if (const auto diffuse_only_it = override_json.find("diffuse_only");
        diffuse_only_it != override_json.end()) {
      if (!diffuse_only_it->is_boolean()) {
        return fail(diagnostic, "gltf_scene material override diffuse_only must be boolean");
      }
      override.diffuse_only = diffuse_only_it->get<bool>();
      override.has_diffuse_only = true;
    }
    if (const auto keep_normal_maps_it = override_json.find("keep_normal_maps");
        keep_normal_maps_it != override_json.end()) {
      if (!keep_normal_maps_it->is_boolean()) {
        return fail(diagnostic, "gltf_scene material override keep_normal_maps must be boolean");
      }
      override.keep_normal_maps = keep_normal_maps_it->get<bool>();
      override.has_keep_normal_maps = true;
    }
    if (const auto disable_mr_it = override_json.find("disable_metallic_roughness");
        disable_mr_it != override_json.end()) {
      if (!disable_mr_it->is_boolean()) {
        return fail(diagnostic,
                    "gltf_scene material override disable_metallic_roughness must be boolean");
      }
      override.disable_metallic_roughness = disable_mr_it->get<bool>();
      override.has_disable_metallic_roughness = true;
    }
    if (override.has_keep_normal_maps &&
        (!override.has_diffuse_only || !override.diffuse_only)) {
      return fail(diagnostic,
                  "gltf_scene material override keep_normal_maps requires diffuse_only true");
    }
    if (!override.has_normal_scale && !override.has_casts_shadows &&
        !override.has_diffuse_only && !override.has_disable_metallic_roughness) {
      return fail(diagnostic, "gltf_scene material override has no supported fields");
    }

    out.material_overrides.push_back(std::move(override));
  }
  return true;
}

bool readStringMap(const Json& object,
                   std::unordered_map<std::string, std::string>& out,
                   std::string_view section,
                   std::string* diagnostic) {
  if (!object.is_object()) {
    return fail(diagnostic, std::string(section) + " must be an object");
  }
  for (const auto& [name, value] : object.items()) {
    if (!value.is_string()) {
      return fail(diagnostic, std::string(section) + " values must be strings");
    }
    out[name] = value.get<std::string>();
  }
  return true;
}

bool parseColorValue(const Json& value, math::Color& out) {
  if (!value.is_array() || value.size() != 4u) {
    return false;
  }
  for (const Json& element : value) {
    if (!element.is_number()) {
      return false;
    }
  }
  out = math::Color{
      value[0].get<float>(),
      value[1].get<float>(),
      value[2].get<float>(),
      value[3].get<float>(),
  };
  return true;
}

bool parseMaterialParameterValue(const Json& value,
                                 rendering::MaterialParameterValue& out,
                                 std::string* diagnostic) {
  if (value.is_boolean()) {
    out = value.get<bool>();
    return true;
  }
  if (value.is_number_integer()) {
    out = value.get<int32_t>();
    return true;
  }
  if (value.is_number_unsigned()) {
    out = value.get<uint32_t>();
    return true;
  }
  if (value.is_number_float()) {
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
        return fail(diagnostic, "parameter arrays must contain only numbers");
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
      out = math::Color{
          value[0].get<float>(),
          value[1].get<float>(),
          value[2].get<float>(),
          value[3].get<float>(),
      };
      return true;
    }
  }
  return fail(diagnostic, "unsupported parameter value");
}

bool readParams(const Json& object,
                std::unordered_map<std::string, rendering::MaterialParameterValue>& out,
                std::string_view section,
                std::string* diagnostic) {
  if (!object.is_object()) {
    return fail(diagnostic, std::string(section) + " must be an object");
  }
  for (const auto& [name, value] : object.items()) {
    rendering::MaterialParameterValue parsed{};
    if (!parseMaterialParameterValue(value, parsed, diagnostic)) {
      return false;
    }
    out[name] = std::move(parsed);
  }
  return true;
}

Json materialParameterToJson(const rendering::MaterialParameterValue& value) {
  return std::visit(
      [](const auto& typed) -> Json {
        using Value = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Value, bool> ||
                      std::is_same_v<Value, int32_t> ||
                      std::is_same_v<Value, uint32_t> ||
                      std::is_same_v<Value, float> ||
                      std::is_same_v<Value, std::string>) {
          return typed;
        } else if constexpr (std::is_same_v<Value, rendering::Color>) {
          return Json::array({typed.r, typed.g, typed.b, typed.a});
        } else if constexpr (std::is_same_v<Value, glm::vec2>) {
          return Json::array({typed.x, typed.y});
        } else if constexpr (std::is_same_v<Value, glm::vec3>) {
          return Json::array({typed.x, typed.y, typed.z});
        } else {
          return Json::array({typed.x, typed.y, typed.z, typed.w});
        }
      },
      value);
}

Json paramsToJson(const std::unordered_map<std::string, rendering::MaterialParameterValue>& params) {
  Json out = Json::object();
  for (const auto& [name, value] : params) {
    out[name] = materialParameterToJson(value);
  }
  return out;
}

bool parseTextureFormat(std::string_view value, rendering::TextureFormat& out) {
  if (value == "rgba8") {
    out = rendering::TextureFormat::RGBA8;
  } else if (value == "rgba16f") {
    out = rendering::TextureFormat::RGBA16F;
  } else if (value == "rgb8") {
    out = rendering::TextureFormat::RGB8;
  } else if (value == "r8") {
    out = rendering::TextureFormat::R8;
  } else if (value == "bc7_rgba_unorm") {
    out = rendering::TextureFormat::BC7_RGBA_UNORM;
  } else if (value == "bc7_rgba_unorm_srgb") {
    out = rendering::TextureFormat::BC7_RGBA_UNORM_SRGB;
  } else if (value == "ktx2_basis_uastc") {
    out = rendering::TextureFormat::KTX2_BASIS_UASTC;
  } else {
    return false;
  }
  return true;
}

std::string textureFormatName(rendering::TextureFormat format) {
  switch (format) {
    case rendering::TextureFormat::RGBA8:
      return "rgba8";
    case rendering::TextureFormat::RGBA16F:
      return "rgba16f";
    case rendering::TextureFormat::RGB8:
      return "rgb8";
    case rendering::TextureFormat::R8:
      return "r8";
    case rendering::TextureFormat::BC7_RGBA_UNORM:
      return "bc7_rgba_unorm";
    case rendering::TextureFormat::BC7_RGBA_UNORM_SRGB:
      return "bc7_rgba_unorm_srgb";
    case rendering::TextureFormat::KTX2_BASIS_UASTC:
      return "ktx2_basis_uastc";
  }
  return "rgba8";
}

bool parseBlendMode(std::string_view value, rendering::MaterialDesc::BlendMode& out) {
  if (value == "alpha") {
    out = rendering::MaterialDesc::BlendMode::Alpha;
  } else if (value == "additive") {
    out = rendering::MaterialDesc::BlendMode::Additive;
  } else {
    return false;
  }
  return true;
}

std::string blendModeName(rendering::MaterialDesc::BlendMode mode) {
  switch (mode) {
    case rendering::MaterialDesc::BlendMode::Alpha:
      return "alpha";
    case rendering::MaterialDesc::BlendMode::Additive:
      return "additive";
  }
  return "alpha";
}

bool parseShaderPipeline(const Json& root,
                         const std::filesystem::path& base_dir,
                         rendering::MaterialPipelineDesc& out,
                         std::string* diagnostic) {
  const auto pipeline_it = root.find("pipeline");
  if (pipeline_it == root.end() || !pipeline_it->is_object()) {
    return fail(diagnostic, "shader_pass requires object field: pipeline");
  }
  const Json& pipeline = *pipeline_it;
  if (!fieldAllowed(pipeline,
                    {"name",
                     "vertex",
                     "fragment",
                     "vertex_entry",
                     "fragment_entry",
                     "defines"},
                    "shader_pass.pipeline",
                    diagnostic)) {
    return false;
  }

  out.name = pipeline.value("name", std::string("fullscreen"));
  auto read_path = [&](const char* field, std::filesystem::path& path) {
    const auto it = pipeline.find(field);
    if (it == pipeline.end() || !it->is_string() || it->get<std::string>().empty()) {
      return fail(diagnostic, std::string("shader_pass.pipeline requires string field: ") + field);
    }
    path = resolveEntryPath(base_dir, it->get<std::string>());
    std::ifstream shader(path);
    if (!shader) {
      return fail(diagnostic, "shader_pass pipeline shader is missing or unreadable: " +
                                  path.string());
    }
    return true;
  };
  if (!read_path("vertex", out.vertex_shader_path) ||
      !read_path("fragment", out.fragment_shader_path)) {
    return false;
  }
  if (const auto entry = pipeline.find("vertex_entry"); entry != pipeline.end()) {
    if (!entry->is_string()) {
      return fail(diagnostic, "shader_pass.pipeline.vertex_entry must be a string");
    }
    out.vertex_entry_point = entry->get<std::string>();
  }
  if (const auto entry = pipeline.find("fragment_entry"); entry != pipeline.end()) {
    if (!entry->is_string()) {
      return fail(diagnostic, "shader_pass.pipeline.fragment_entry must be a string");
    }
    out.fragment_entry_point = entry->get<std::string>();
  }
  if (const auto defines = pipeline.find("defines"); defines != pipeline.end()) {
    if (!defines->is_array()) {
      return fail(diagnostic, "shader_pass.pipeline.defines must be an array");
    }
    out.defines.clear();
    for (const Json& define : *defines) {
      if (!define.is_string()) {
        return fail(diagnostic, "shader_pass.pipeline.defines entries must be strings");
      }
      out.defines.push_back(define.get<std::string>());
    }
  }
  return true;
}

std::optional<rendering::ShaderPassAssetDesc> parseShaderPassAssetDesc(
    const Json& root,
    const std::filesystem::path& asset_path,
    const std::filesystem::path& base_dir,
    std::string* diagnostic) {
  const auto version_it = root.find("version");
  if (version_it == root.end() ||
      (!version_it->is_number_integer() && !version_it->is_number_unsigned()) ||
      version_it->get<int>() != 1) {
    fail(diagnostic, "shader_pass version must be integer 1");
    return std::nullopt;
  }
  if (!fieldAllowed(root,
                    {"version", "pipeline", "params", "textures", "render_state", "fullscreen"},
                    "shader_pass",
                    diagnostic)) {
    return std::nullopt;
  }

  rendering::ShaderPassAssetDesc desc{};
  desc.shader_pass_asset_path = asset_path;
  if (!parseShaderPipeline(root, base_dir, desc.pipeline, diagnostic)) {
    return std::nullopt;
  }
  if (const auto fullscreen = root.find("fullscreen"); fullscreen != root.end()) {
    if (!fullscreen->is_boolean()) {
      fail(diagnostic, "shader_pass.fullscreen must be a boolean");
      return std::nullopt;
    }
    desc.fullscreen = fullscreen->get<bool>();
  }
  if (const auto params = root.find("params"); params != root.end() &&
      !readParams(*params, desc.params, "shader_pass.params", diagnostic)) {
    return std::nullopt;
  }
  if (const auto textures = root.find("textures"); textures != root.end()) {
    if (!readStringMap(*textures, desc.textures, "shader_pass.textures", diagnostic)) {
      return std::nullopt;
    }
    for (const auto& [slot, texture_key] : desc.textures) {
      (void)slot;
      if (!AssetRegistry::isValidAssetKey(texture_key)) {
        fail(diagnostic,
             "invalid shader_pass texture key '" + texture_key + "': " +
                 AssetRegistry::assetKeyValidationError(texture_key));
        return std::nullopt;
      }
    }
  }
  if (const auto state = root.find("render_state"); state != root.end()) {
    if (!state->is_object() ||
        !fieldAllowed(*state,
                      {"depth_test", "depth_write", "blend", "blend_enabled", "blend_mode"},
                      "shader_pass.render_state",
                      diagnostic)) {
      return std::nullopt;
    }
    if (const auto it = state->find("depth_test"); it != state->end()) {
      if (!it->is_boolean()) {
        fail(diagnostic, "shader_pass.render_state.depth_test must be a boolean");
        return std::nullopt;
      }
      desc.depth_test = it->get<bool>();
    }
    if (const auto it = state->find("depth_write"); it != state->end()) {
      if (!it->is_boolean()) {
        fail(diagnostic, "shader_pass.render_state.depth_write must be a boolean");
        return std::nullopt;
      }
      desc.depth_write = it->get<bool>();
    }
    if (const auto it = state->find("blend"); it != state->end()) {
      if (!it->is_boolean()) {
        fail(diagnostic, "shader_pass.render_state.blend must be a boolean");
        return std::nullopt;
      }
      desc.blend_enabled = it->get<bool>();
    }
    if (const auto it = state->find("blend_enabled"); it != state->end()) {
      if (!it->is_boolean()) {
        fail(diagnostic, "shader_pass.render_state.blend_enabled must be a boolean");
        return std::nullopt;
      }
      desc.blend_enabled = it->get<bool>();
    }
    if (const auto it = state->find("blend_mode"); it != state->end()) {
      if (!it->is_string() || !parseBlendMode(it->get<std::string>(), desc.blend_mode)) {
        fail(diagnostic,
             "shader_pass.render_state.blend_mode must be 'alpha' or 'additive'");
        return std::nullopt;
      }
    }
  }
  return desc;
}

std::optional<rendering::ShaderPassAssetDesc> loadShaderPassAssetDesc(
    const std::filesystem::path& path,
    std::string* diagnostic) {
  Json root;
  if (!readJsonObjectFile(path, root, diagnostic, "shader_pass")) {
    return std::nullopt;
  }
  return parseShaderPassAssetDesc(root, path, packageDirectory(path), diagnostic);
}

bool parseResourceKind(std::string_view value, rendering::FrameGraphResourceKind& out) {
  if (value == "color_texture") {
    out = rendering::FrameGraphResourceKind::ColorTexture;
  } else if (value == "depth_texture") {
    out = rendering::FrameGraphResourceKind::DepthTexture;
  } else if (value == "external_color") {
    out = rendering::FrameGraphResourceKind::ExternalColor;
  } else if (value == "external_depth") {
    out = rendering::FrameGraphResourceKind::ExternalDepth;
  } else if (value == "backbuffer") {
    out = rendering::FrameGraphResourceKind::Backbuffer;
  } else {
    return false;
  }
  return true;
}

std::string resourceKindName(rendering::FrameGraphResourceKind kind) {
  switch (kind) {
    case rendering::FrameGraphResourceKind::ColorTexture:
      return "color_texture";
    case rendering::FrameGraphResourceKind::DepthTexture:
      return "depth_texture";
    case rendering::FrameGraphResourceKind::ExternalColor:
      return "external_color";
    case rendering::FrameGraphResourceKind::ExternalDepth:
      return "external_depth";
    case rendering::FrameGraphResourceKind::Backbuffer:
      return "backbuffer";
  }
  return "color_texture";
}

bool parsePassKind(std::string_view value, rendering::FrameGraphPassKind& out) {
  if (value == "scene") {
    out = rendering::FrameGraphPassKind::Scene;
  } else if (value == "builtin") {
    out = rendering::FrameGraphPassKind::Builtin;
  } else if (value == "shader") {
    out = rendering::FrameGraphPassKind::Shader;
  } else if (value == "copy" || value == "blit") {
    out = rendering::FrameGraphPassKind::Copy;
  } else if (value == "scene_mask") {
    out = rendering::FrameGraphPassKind::SceneMask;
  } else {
    return false;
  }
  return true;
}

std::string passKindName(rendering::FrameGraphPassKind kind) {
  switch (kind) {
    case rendering::FrameGraphPassKind::Scene:
      return "scene";
    case rendering::FrameGraphPassKind::Builtin:
      return "builtin";
    case rendering::FrameGraphPassKind::Shader:
      return "shader";
    case rendering::FrameGraphPassKind::Copy:
      return "copy";
    case rendering::FrameGraphPassKind::SceneMask:
      return "scene_mask";
  }
  return "shader";
}

bool readStringArray(const Json& value,
                     std::vector<std::string>& out,
                     std::string_view section,
                     std::string* diagnostic) {
  if (!value.is_array()) {
    return fail(diagnostic, std::string(section) + " must be an array");
  }
  for (const Json& entry : value) {
    if (!entry.is_string()) {
      return fail(diagnostic, std::string(section) + " entries must be strings");
    }
    std::string text = entry.get<std::string>();
    if (text.empty()) {
      return fail(diagnostic, std::string(section) + " entries must not be empty");
    }
    out.push_back(std::move(text));
  }
  return true;
}

bool parseHumanoidProfile(std::string_view value, world::HumanoidProfileKind& out) {
  if (value == "mixamo" || value == "Mixamo") {
    out = world::HumanoidProfileKind::Mixamo;
    return true;
  }
  return false;
}

bool readHumanoidImportOptions(const Json& entry,
                               detail::HumanoidImportOptions& out,
                               std::string* diagnostic) {
  const auto humanoid_it = entry.find("humanoid");
  if (humanoid_it == entry.end() || humanoid_it->is_null()) {
    return true;
  }
  if (!humanoid_it->is_object()) {
    return fail(diagnostic, "humanoid must be an object");
  }
  if (!fieldAllowed(*humanoid_it, {"profile", "rig_key"}, "humanoid", diagnostic)) {
    return false;
  }
  out.enabled = true;
  std::string profile = "mixamo";
  if (const auto profile_it = humanoid_it->find("profile");
      profile_it != humanoid_it->end()) {
    if (!profile_it->is_string()) {
      return fail(diagnostic, "humanoid.profile must be a string");
    }
    profile = profile_it->get<std::string>();
  }
  if (!parseHumanoidProfile(profile, out.profile)) {
    return fail(diagnostic, "unknown humanoid profile: " + profile);
  }
  if (const auto rig_key_it = humanoid_it->find("rig_key");
      rig_key_it != humanoid_it->end()) {
    if (!rig_key_it->is_string()) {
      return fail(diagnostic, "humanoid.rig_key must be a string");
    }
    out.rig_key = rig_key_it->get<std::string>();
  }
  if (!out.rig_key.empty() && !AssetRegistry::isValidAssetKey(out.rig_key)) {
    return fail(diagnostic,
                "invalid humanoid rig key '" + out.rig_key + "': " +
                    AssetRegistry::assetKeyValidationError(out.rig_key));
  }
  return true;
}

std::optional<rendering::FrameGraphDesc> parseFrameGraphDesc(
    const Json& root,
    std::string* diagnostic) {
  const auto version_it = root.find("version");
  if (version_it == root.end() ||
      (!version_it->is_number_integer() && !version_it->is_number_unsigned()) ||
      version_it->get<int>() != 1) {
    fail(diagnostic, "render_graph version must be integer 1");
    return std::nullopt;
  }
  if (!fieldAllowed(root,
                    {"version", "enabled", "resources", "passes", "output_resource"},
                    "render_graph",
                    diagnostic)) {
    return std::nullopt;
  }

  rendering::FrameGraphDesc desc{};
  desc.output_resource = root.value("output_resource",
                                    std::string(rendering::kFrameGraphCameraColor));
  desc.enabled = root.value("enabled", true);

  if (const auto resources = root.find("resources"); resources != root.end()) {
    if (!resources->is_array()) {
      fail(diagnostic, "render_graph.resources must be an array");
      return std::nullopt;
    }
    for (const Json& resource : *resources) {
      if (!resource.is_object() ||
          !fieldAllowed(resource,
                        {"name",
                         "kind",
                         "scale",
                         "size",
                         "width",
                         "height",
                         "format",
                         "history_count"},
                        "render_graph.resource",
                        diagnostic)) {
        return std::nullopt;
      }
      rendering::FrameGraphResourceDesc parsed{};
      if (!readRequiredString(resource, "name", parsed.name, diagnostic)) {
        return std::nullopt;
      }
      const std::string kind = resource.value("kind", std::string("color_texture"));
      if (!parseResourceKind(kind, parsed.kind)) {
        fail(diagnostic, "unknown render_graph resource kind: " + kind);
        return std::nullopt;
      }
      if (const auto format = resource.find("format"); format != resource.end()) {
        if (!format->is_string() ||
            !parseTextureFormat(format->get<std::string>(), parsed.format)) {
          fail(diagnostic, "unknown render_graph resource format");
          return std::nullopt;
        }
      }
      if (const auto scale = resource.find("scale"); scale != resource.end()) {
        if (!scale->is_array() || scale->size() != 2u ||
            !(*scale)[0].is_number() || !(*scale)[1].is_number()) {
          fail(diagnostic, "render_graph resource scale must be [width, height]");
          return std::nullopt;
        }
        parsed.size_mode = rendering::FrameGraphResourceSizeMode::CameraRelative;
        parsed.width_scale = (*scale)[0].get<float>();
        parsed.height_scale = (*scale)[1].get<float>();
      }
      if (const auto size = resource.find("size"); size != resource.end()) {
        if (!size->is_array() || size->size() != 2u ||
            !(*size)[0].is_number_unsigned() || !(*size)[1].is_number_unsigned()) {
          fail(diagnostic, "render_graph resource size must be [width, height]");
          return std::nullopt;
        }
        parsed.size_mode = rendering::FrameGraphResourceSizeMode::Absolute;
        parsed.width = (*size)[0].get<uint32_t>();
        parsed.height = (*size)[1].get<uint32_t>();
      }
      if (const auto width = resource.find("width"); width != resource.end()) {
        if (!width->is_number_unsigned()) {
          fail(diagnostic, "render_graph resource width must be unsigned");
          return std::nullopt;
        }
        parsed.size_mode = rendering::FrameGraphResourceSizeMode::Absolute;
        parsed.width = width->get<uint32_t>();
      }
      if (const auto height = resource.find("height"); height != resource.end()) {
        if (!height->is_number_unsigned()) {
          fail(diagnostic, "render_graph resource height must be unsigned");
          return std::nullopt;
        }
        parsed.size_mode = rendering::FrameGraphResourceSizeMode::Absolute;
        parsed.height = height->get<uint32_t>();
      }
      if (const auto history = resource.find("history_count"); history != resource.end()) {
        if (!history->is_number_unsigned()) {
          fail(diagnostic, "render_graph resource history_count must be unsigned");
          return std::nullopt;
        }
        parsed.history_count = history->get<uint32_t>();
      }
      desc.resources.push_back(std::move(parsed));
    }
  }

  const auto passes = root.find("passes");
  if (passes == root.end() || !passes->is_array()) {
    fail(diagnostic, "render_graph requires array field: passes");
    return std::nullopt;
  }
  for (const Json& pass : *passes) {
    if (!pass.is_object() ||
        !fieldAllowed(pass,
                      {"name",
                       "kind",
                       "builtin",
                       "builtin_pass",
                       "shader_pass",
                       "shader_pass_key",
                       "render_tags",
                       "inputs",
                       "outputs",
                       "params",
                       "enabled",
                       "clear",
                       "clear_depth",
                       "clear_color"},
                      "render_graph.pass",
                      diagnostic)) {
      return std::nullopt;
    }
    rendering::FrameGraphPassDesc parsed{};
    if (!readRequiredString(pass, "name", parsed.name, diagnostic)) {
      return std::nullopt;
    }
    const std::string kind = pass.value("kind", std::string("shader"));
    if (!parsePassKind(kind, parsed.kind)) {
      fail(diagnostic, "unknown render_graph pass kind: " + kind);
      return std::nullopt;
    }
    parsed.enabled = pass.value("enabled", true);
    parsed.clear = pass.value("clear", false);
    parsed.clear_depth = pass.value("clear_depth", false);
    if (const auto color = pass.find("clear_color"); color != pass.end() &&
        !parseColorValue(*color, parsed.clear_color)) {
      fail(diagnostic, "render_graph.pass.clear_color must be a four-number array");
      return std::nullopt;
    }
    if (const auto builtin = pass.find("builtin"); builtin != pass.end()) {
      if (!builtin->is_string()) {
        fail(diagnostic, "render_graph.pass.builtin must be a string");
        return std::nullopt;
      }
      parsed.builtin_pass = builtin->get<std::string>();
    }
    if (const auto builtin = pass.find("builtin_pass"); builtin != pass.end()) {
      if (!builtin->is_string()) {
        fail(diagnostic, "render_graph.pass.builtin_pass must be a string");
        return std::nullopt;
      }
      parsed.builtin_pass = builtin->get<std::string>();
    }
    if (const auto shader = pass.find("shader_pass"); shader != pass.end()) {
      if (!shader->is_string()) {
        fail(diagnostic, "render_graph.pass.shader_pass must be a string");
        return std::nullopt;
      }
      parsed.shader_pass_key = shader->get<std::string>();
    }
    if (const auto shader = pass.find("shader_pass_key"); shader != pass.end()) {
      if (!shader->is_string()) {
        fail(diagnostic, "render_graph.pass.shader_pass_key must be a string");
        return std::nullopt;
      }
      parsed.shader_pass_key = shader->get<std::string>();
    }
    if (!parsed.shader_pass_key.empty() &&
        !AssetRegistry::isValidAssetKey(parsed.shader_pass_key)) {
      fail(diagnostic,
           "invalid shader pass asset key '" + parsed.shader_pass_key + "': " +
               AssetRegistry::assetKeyValidationError(parsed.shader_pass_key));
      return std::nullopt;
    }
    if (const auto tags = pass.find("render_tags"); tags != pass.end() &&
        !readStringArray(*tags, parsed.render_tags, "render_graph.pass.render_tags", diagnostic)) {
      return std::nullopt;
    }
    if (const auto inputs = pass.find("inputs"); inputs != pass.end() &&
        !readStringMap(*inputs, parsed.inputs, "render_graph.pass.inputs", diagnostic)) {
      return std::nullopt;
    }
    if (const auto outputs = pass.find("outputs"); outputs != pass.end() &&
        !readStringMap(*outputs, parsed.outputs, "render_graph.pass.outputs", diagnostic)) {
      return std::nullopt;
    }
    if (const auto params = pass.find("params"); params != pass.end() &&
        !readParams(*params, parsed.params, "render_graph.pass.params", diagnostic)) {
      return std::nullopt;
    }
    desc.passes.push_back(std::move(parsed));
  }

  rendering::FrameGraphValidationResult validation = rendering::validateFrameGraphDesc(desc);
  if (!validation.valid()) {
    fail(diagnostic, "invalid render_graph: " + validation.diagnostics.front());
    return std::nullopt;
  }
  return desc;
}

std::optional<rendering::FrameGraphDesc> loadFrameGraphDesc(
    const std::filesystem::path& path,
    std::string* diagnostic) {
  Json root;
  if (!readJsonObjectFile(path, root, diagnostic, "render_graph")) {
    return std::nullopt;
  }
  return parseFrameGraphDesc(root, diagnostic);
}

Json shaderPassToJson(const rendering::ShaderPassAssetDesc& pass) {
  Json root{
      {"version", 1},
      {"fullscreen", pass.fullscreen},
      {"pipeline",
       Json{
           {"name", pass.pipeline.name},
           {"vertex", pass.pipeline.vertex_shader_path.lexically_normal().generic_string()},
           {"fragment", pass.pipeline.fragment_shader_path.lexically_normal().generic_string()},
           {"vertex_entry", pass.pipeline.vertex_entry_point},
           {"fragment_entry", pass.pipeline.fragment_entry_point},
           {"defines", pass.pipeline.defines},
       }},
      {"params", paramsToJson(pass.params)},
      {"textures", pass.textures},
      {"render_state",
       Json{
           {"depth_test", pass.depth_test},
           {"depth_write", pass.depth_write},
           {"blend_enabled", pass.blend_enabled},
           {"blend_mode", blendModeName(pass.blend_mode)},
       }},
  };
  return root;
}

Json frameGraphToJson(const rendering::FrameGraphDesc& graph) {
  Json root{
      {"version", 1},
      {"enabled", graph.enabled},
      {"output_resource", graph.output_resource},
      {"resources", Json::array()},
      {"passes", Json::array()},
  };
  for (const rendering::FrameGraphResourceDesc& resource : graph.resources) {
    Json item{
        {"name", resource.name},
        {"kind", resourceKindName(resource.kind)},
        {"format", textureFormatName(resource.format)},
        {"history_count", resource.history_count},
    };
    if (resource.size_mode == rendering::FrameGraphResourceSizeMode::Absolute) {
      item["size"] = Json::array({resource.width, resource.height});
    } else {
      item["scale"] = Json::array({resource.width_scale, resource.height_scale});
    }
    root["resources"].push_back(std::move(item));
  }
  for (const rendering::FrameGraphPassDesc& pass : graph.passes) {
    Json item{
        {"name", pass.name},
        {"kind", passKindName(pass.kind)},
        {"enabled", pass.enabled},
        {"inputs", pass.inputs},
        {"outputs", pass.outputs},
        {"params", paramsToJson(pass.params)},
        {"clear", pass.clear},
        {"clear_depth", pass.clear_depth},
        {"clear_color", Json::array({pass.clear_color.r,
                                      pass.clear_color.g,
                                      pass.clear_color.b,
                                      pass.clear_color.a})},
    };
    if (!pass.builtin_pass.empty()) {
      item["builtin"] = pass.builtin_pass;
    }
    if (!pass.shader_pass_key.empty()) {
      item["shader_pass"] = pass.shader_pass_key;
    }
    if (!pass.render_tags.empty()) {
      item["render_tags"] = pass.render_tags;
    }
    root["passes"].push_back(std::move(item));
  }
  return root;
}

bool readGltfSceneAlphaModePolicy(const Json& entry,
                                  world::GltfSceneLoadOptions& out,
                                  std::string* diagnostic) {
  const auto policy_it = entry.find("alpha_mode_policy");
  if (policy_it == entry.end()) {
    return true;
  }
  if (!policy_it->is_string()) {
    return fail(diagnostic, "gltf_scene alpha_mode_policy must be a string");
  }
  const std::string policy = policy_it->get<std::string>();
  if (policy == "authored") {
    out.alpha_mode_policy = world::GltfSceneLoadOptions::AlphaModePolicy::Authored;
    return true;
  }
  if (policy == "auto_cutout") {
    out.alpha_mode_policy = world::GltfSceneLoadOptions::AlphaModePolicy::AutoCutout;
    return true;
  }
  return fail(diagnostic, "unsupported gltf_scene alpha_mode_policy: " + policy);
}

bool readJson(const std::filesystem::path& path, Json& out, std::string* diagnostic) {
  std::ifstream stream(path);
  if (!stream) {
    return fail(diagnostic, "failed to open asset package: " + path.string());
  }
  try {
    stream >> out;
  } catch (const std::exception& e) {
    return fail(diagnostic, std::string("failed to parse asset package JSON: ") + e.what());
  }
  if (!out.is_object()) {
    return fail(diagnostic, "asset package root must be an object");
  }
  const auto version_it = out.find("version");
  if (version_it == out.end() ||
      (!version_it->is_number_integer() && !version_it->is_number_unsigned()) ||
      version_it->get<int>() != 1) {
    return fail(diagnostic, "asset package version must be integer 1");
  }
  const auto assets_it = out.find("assets");
  if (assets_it == out.end() || !assets_it->is_array()) {
    return fail(diagnostic, "asset package requires an assets array");
  }
  return true;
}

bool keyAlreadyExists(const AssetRegistry& assets,
                      const std::string& type,
                      const std::string& key) {
  if (type == "texture_rgba8" || type == "texture") {
    return assets.findTextureAsset(key) != nullptr;
  }
  if (type == "mesh") {
    return assets.findMeshAsset(key) != nullptr;
  }
  if (type == "material") {
    return assets.findMaterialAsset(key) != nullptr ||
           assets.findMaterialVariant(key) != nullptr;
  }
  if (type == "particle_effect") {
    return assets.findParticleEffect(key) != nullptr;
  }
  if (type == "environment_map") {
    return assets.findEnvironmentMap(key) != nullptr;
  }
  if (type == "gltf_scene") {
    return assets.findGltfSceneAsset(key) != nullptr;
  }
  if (type == "shader_pass") {
    return assets.findShaderPass(key) != nullptr;
  }
  if (type == "render_graph") {
    return assets.findFrameGraph(key) != nullptr;
  }
  if (type == "scene") {
    return assets.findSceneAsset(key) != nullptr;
  }
  if (type == "animation_clip") {
    return assets.findAnimationClip(key) != nullptr;
  }
  if (type == "skeleton") {
    return assets.findSkeleton(key) != nullptr;
  }
  if (type == "skin") {
    return assets.findSkin(key) != nullptr;
  }
  if (type == "humanoid_rig") {
    return assets.findHumanoidRig(key) != nullptr;
  }
  return false;
}

bool copyAssetTo(AssetRegistry& target,
                 const AssetRegistry& source,
                 const AssetPackageLoadedAsset& asset,
                 std::string* diagnostic) {
  if (asset.type == "texture_rgba8" || asset.type == "texture") {
    const TextureAsset* texture = source.findTextureAsset(asset.key);
    if (texture == nullptr || !target.registerTextureAsset(asset.key, *texture)) {
      return fail(diagnostic, "failed to commit texture asset: " + asset.key);
    }
    return true;
  }
  if (asset.type == "mesh") {
    const world::MeshData* mesh = source.findMeshAsset(asset.key);
    if (mesh == nullptr || !target.registerMeshAsset(asset.key, *mesh)) {
      return fail(diagnostic, "failed to commit mesh asset: " + asset.key);
    }
    return true;
  }
  if (asset.type == "material") {
    if (const rendering::MaterialAssetDesc* material = source.findMaterialAsset(asset.key)) {
      if (!target.registerMaterialAsset(asset.key, *material)) {
        return fail(diagnostic, "failed to commit material asset: " + asset.key);
      }
      return true;
    }
    if (const rendering::MaterialVariantDesc* variant = source.findMaterialVariant(asset.key)) {
      if (!target.registerMaterialVariant(asset.key, *variant)) {
        return fail(diagnostic, "failed to commit material variant: " + asset.key);
      }
      return true;
    }
    return fail(diagnostic, "missing staged material asset: " + asset.key);
  }
  if (asset.type == "particle_effect") {
    const visual::particles::ParticleEffectAsset* effect = source.findParticleEffect(asset.key);
    if (effect == nullptr || !target.registerParticleEffect(asset.key, *effect)) {
      return fail(diagnostic, "failed to commit particle effect: " + asset.key);
    }
    return true;
  }
  if (asset.type == "environment_map") {
    const EnvironmentMapAsset* environment = source.findEnvironmentMap(asset.key);
    if (environment == nullptr || !target.registerEnvironmentMap(asset.key, *environment)) {
      return fail(diagnostic, "failed to commit environment map: " + asset.key);
    }
    return true;
  }
  if (asset.type == "gltf_scene") {
    const GltfSceneAsset* scene = source.findGltfSceneAsset(asset.key);
    if (scene == nullptr || !target.registerGltfSceneAsset(asset.key, *scene)) {
      return fail(diagnostic, "failed to commit glTF scene: " + asset.key);
    }
    return true;
  }
  if (asset.type == "shader_pass") {
    const rendering::ShaderPassAssetDesc* pass = source.findShaderPass(asset.key);
    if (pass == nullptr || !target.registerShaderPass(asset.key, *pass)) {
      return fail(diagnostic, "failed to commit shader pass: " + asset.key);
    }
    return true;
  }
  if (asset.type == "render_graph") {
    const rendering::FrameGraphDesc* graph = source.findFrameGraph(asset.key);
    if (graph == nullptr || !target.registerFrameGraph(asset.key, *graph)) {
      return fail(diagnostic, "failed to commit render graph: " + asset.key);
    }
    return true;
  }
  if (asset.type == "scene") {
    const SceneAsset* scene = source.findSceneAsset(asset.key);
    if (scene == nullptr || !target.registerSceneAsset(asset.key, *scene)) {
      return fail(diagnostic, "failed to commit scene asset: " + asset.key);
    }
    return true;
  }
  if (asset.type == "animation_clip") {
    const world::AnimationClip* clip = source.findAnimationClip(asset.key);
    if (clip == nullptr || !target.registerAnimationClip(asset.key, *clip)) {
      return fail(diagnostic, "failed to commit animation clip: " + asset.key);
    }
    return true;
  }
  if (asset.type == "skeleton") {
    const world::Skeleton* skeleton = source.findSkeleton(asset.key);
    if (skeleton == nullptr || !target.registerSkeleton(asset.key, *skeleton)) {
      return fail(diagnostic, "failed to commit skeleton: " + asset.key);
    }
    return true;
  }
  if (asset.type == "skin") {
    const world::Skin* skin = source.findSkin(asset.key);
    if (skin == nullptr || !target.registerSkin(asset.key, *skin)) {
      return fail(diagnostic, "failed to commit skin: " + asset.key);
    }
    return true;
  }
  if (asset.type == "humanoid_rig") {
    const world::HumanoidRig* rig = source.findHumanoidRig(asset.key);
    if (rig == nullptr || !target.registerHumanoidRig(asset.key, *rig)) {
      return fail(diagnostic, "failed to commit humanoid rig: " + asset.key);
    }
    return true;
  }
  return fail(diagnostic, "unsupported staged asset type: " + asset.type);
}

void addLoaded(AssetPackageHandle& handle,
               std::string type,
               std::string key,
               std::string cache_blob_key = {}) {
  for (auto& asset : handle.assets) {
    if (asset.type == type && asset.key == key) {
      if (asset.cache_blob_key.empty() && !cache_blob_key.empty()) {
        asset.cache_blob_key = std::move(cache_blob_key);
      }
      return;
    }
  }
  handle.assets.push_back(AssetPackageLoadedAsset{
      .type = std::move(type),
      .key = std::move(key),
      .cache_blob_key = std::move(cache_blob_key),
  });
}

std::string sceneLoadDiagnostics(const scenes::SceneLoadResult& result) {
  std::string message;
  for (const std::string& diagnostic : result.diagnostics) {
    if (!message.empty()) {
      message += "; ";
    }
    message += diagnostic;
  }
  return message.empty() ? "unknown scene document load failure" : message;
}

bool importEntry(AssetRegistry& assets,
                 const Json& entry,
                 const std::filesystem::path& manifest_path,
                 const std::filesystem::path& base_dir,
                 AssetPackageHandle& handle,
                 std::string* diagnostic) {
  if (!entry.is_object()) {
    return fail(diagnostic, "asset package entries must be objects");
  }

  std::string type;
  std::string key;
  if (!readRequiredString(entry, "type", type, diagnostic) ||
      !readRequiredString(entry, "key", key, diagnostic)) {
    return false;
  }
  if (!AssetRegistry::isValidAssetKey(key)) {
    return fail(diagnostic, "invalid asset key '" + key + "': " +
                                AssetRegistry::assetKeyValidationError(key));
  }
  if (keyAlreadyExists(assets, type, key)) {
    return fail(diagnostic, "asset package would overwrite existing key: " + key);
  }

  std::filesystem::path source_path;
  const auto entry_start = core::SteadyClock::now();
  if (type == "texture_rgba8") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    TextureImportOptions options{};
    if (const auto it = entry.find("srgb"); it != entry.end()) {
      if (!it->is_boolean()) {
        return fail(diagnostic, "texture_rgba8.srgb must be a boolean");
      }
      options.srgb = it->get<bool>();
    }
    if (const auto it = entry.find("generate_mips"); it != entry.end()) {
      if (!it->is_boolean()) {
        return fail(diagnostic, "texture_rgba8.generate_mips must be a boolean");
      }
      options.generate_mips = it->get<bool>();
    }
    if (const auto it = entry.find("prefer_compressed"); it != entry.end()) {
      if (!it->is_boolean()) {
        return fail(diagnostic, "texture_rgba8.prefer_compressed must be a boolean");
      }
      options.prefer_compressed = it->get<bool>();
    }
    if (const auto it = entry.find("alpha_bleed"); it != entry.end()) {
      if (!it->is_boolean()) {
        return fail(diagnostic, "texture_rgba8.alpha_bleed must be a boolean");
      }
      options.alpha_bleed = it->get<bool>();
    }
    if (const auto it = entry.find("alpha_coverage_cutoff"); it != entry.end()) {
      if (!it->is_number()) {
        return fail(diagnostic, "texture_rgba8.alpha_coverage_cutoff must be a number");
      }
      options.alpha_coverage_cutoff = it->get<float>();
      if (options.alpha_coverage_cutoff < 0.0f || options.alpha_coverage_cutoff > 1.0f) {
        return fail(diagnostic,
                    "texture_rgba8.alpha_coverage_cutoff must be between 0 and 1");
      }
    }
    if (!detail::importTextureAsset(assets, key, source_path, options)) {
      return fail(diagnostic, "failed to import texture asset: " + source_path.string());
    }
    addLoaded(handle, type, key);
    logAssetPackageEntryDiag(manifest_path,
                             type,
                             key,
                             source_path,
                             entry_start,
                             core::SteadyClock::now());
    return true;
  }

  if (type == "mesh") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    if (!detail::importMeshAsset(assets, key, source_path)) {
      return fail(diagnostic, "failed to import mesh asset: " + source_path.string());
    }
    addLoaded(handle, type, key);
    logAssetPackageEntryDiag(manifest_path,
                             type,
                             key,
                             source_path,
                             entry_start,
                             core::SteadyClock::now());
    return true;
  }

  if (type == "material") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    MaterialLoadResult result = loadMaterialFile(assets, key, source_path);
    if (!result.success) {
      return fail(diagnostic, "failed to import material asset '" + source_path.string() +
                                  "': " + result.diagnostic);
    }
    addLoaded(handle, type, key);
    logAssetPackageEntryDiag(manifest_path,
                             type,
                             key,
                             source_path,
                             entry_start,
                             core::SteadyClock::now());
    return true;
  }

  if (type == "shader_pass") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    auto pass = loadShaderPassAssetDesc(source_path, diagnostic);
    if (!pass.has_value() || !assets.registerShaderPass(key, std::move(*pass))) {
      return fail(diagnostic, diagnostic != nullptr && !diagnostic->empty()
                                  ? *diagnostic
                                  : "failed to import shader pass: " + source_path.string());
    }
    addLoaded(handle, type, key);
    logAssetPackageEntryDiag(manifest_path,
                             type,
                             key,
                             source_path,
                             entry_start,
                             core::SteadyClock::now());
    return true;
  }

  if (type == "render_graph") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    auto graph = loadFrameGraphDesc(source_path, diagnostic);
    if (!graph.has_value() || !assets.registerFrameGraph(key, std::move(*graph))) {
      return fail(diagnostic, diagnostic != nullptr && !diagnostic->empty()
                                  ? *diagnostic
                                  : "failed to import render graph: " + source_path.string());
    }
    addLoaded(handle, type, key);
    logAssetPackageEntryDiag(manifest_path,
                             type,
                             key,
                             source_path,
                             entry_start,
                             core::SteadyClock::now());
    return true;
  }

  if (type == "particle_effect") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    if (!detail::importParticleEffect(assets, key, source_path)) {
      return fail(diagnostic, "failed to import particle effect: " + source_path.string());
    }
    addLoaded(handle, type, key);
    logAssetPackageEntryDiag(manifest_path,
                             type,
                             key,
                             source_path,
                             entry_start,
                             core::SteadyClock::now());
    return true;
  }

  if (type == "environment_map") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    if (!assets.registerEnvironmentMap(key, EnvironmentMapAsset{.path = source_path})) {
      return fail(diagnostic, "failed to register environment map: " + key);
    }
    addLoaded(handle, type, key);
    logAssetPackageEntryDiag(manifest_path,
                             type,
                             key,
                             source_path,
                             entry_start,
                             core::SteadyClock::now());
    return true;
  }

  if (type == "gltf_scene") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    world::GltfSceneLoadOptions load_options{};
    load_options.import_meshes = entry.value("import_meshes", true);
    load_options.import_lights = entry.value("import_lights", true);
    if (!readGltfSceneAlphaModePolicy(entry, load_options, diagnostic)) {
      return false;
    }
    if (!readGltfSceneMaterialOverrides(entry, load_options, diagnostic)) {
      return false;
    }
    detail::HumanoidImportOptions humanoid_options{};
    if (!readHumanoidImportOptions(entry, humanoid_options, diagnostic)) {
      return false;
    }
    GltfSceneAsset scene =
        detail::importGltfSceneAsset(assets, key, source_path, load_options, humanoid_options);
    if (scene.scene_key.empty()) {
      return fail(diagnostic, "failed to import glTF scene: " + source_path.string());
    }
    addLoaded(handle, "gltf_scene", key);
    for (const std::string& child_key : scene.mesh_asset_keys) {
      addLoaded(handle, "mesh", child_key);
    }
    for (const std::string& child_key : scene.texture_asset_keys) {
      addLoaded(handle, "texture", child_key);
    }
    for (const std::string& child_key : scene.material_keys) {
      addLoaded(handle, "material", child_key);
    }
    for (const std::string& child_key : scene.animation_clip_keys) {
      addLoaded(handle, "animation_clip", child_key);
    }
    for (const std::string& child_key : scene.skeleton_keys) {
      addLoaded(handle, "skeleton", child_key);
    }
    for (const std::string& child_key : scene.skin_keys) {
      addLoaded(handle, "skin", child_key);
    }
    for (const std::string& child_key : scene.humanoid_rig_keys) {
      addLoaded(handle, "humanoid_rig", child_key);
    }
    logAssetPackageEntryDiag(manifest_path,
                             type,
                             key,
                             source_path,
                             entry_start,
                             core::SteadyClock::now());
    return true;
  }

  if (type == "animation_clip") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    detail::HumanoidImportOptions humanoid_options{};
    if (!readHumanoidImportOptions(entry, humanoid_options, diagnostic)) {
      return false;
    }
    std::string source_clip;
    if (const auto clip_it = entry.find("clip"); clip_it != entry.end()) {
      if (!clip_it->is_string()) {
        return fail(diagnostic, "animation_clip.clip must be a string");
      }
      source_clip = clip_it->get<std::string>();
    }
    std::string display_name;
    if (const auto name_it = entry.find("name"); name_it != entry.end()) {
      if (!name_it->is_string()) {
        return fail(diagnostic, "animation_clip.name must be a string");
      }
      display_name = name_it->get<std::string>();
    }
    detail::AnimationClipImportResult imported =
        detail::importAnimationClipAsset(assets,
                                         key,
                                         source_path,
                                         source_clip,
                                         display_name,
                                         humanoid_options);
    if (imported.clip_key.empty()) {
      return fail(diagnostic, "failed to import animation clip: " + source_path.string());
    }
    addLoaded(handle, "animation_clip", imported.clip_key);
    for (const std::string& child_key : imported.skeleton_keys) {
      addLoaded(handle, "skeleton", child_key);
    }
    for (const std::string& child_key : imported.humanoid_rig_keys) {
      addLoaded(handle, "humanoid_rig", child_key);
    }
    logAssetPackageEntryDiag(manifest_path,
                             type,
                             key,
                             source_path,
                             entry_start,
                             core::SteadyClock::now());
    return true;
  }

  if (type == "scene") {
    if (!readRequiredPath(entry, base_dir, source_path, diagnostic)) {
      return false;
    }
    scenes::SceneLoadResult result = scenes::loadSceneDocument(source_path);
    if (!result.success()) {
      return fail(diagnostic, "failed to import scene asset '" + source_path.string() +
                                  "': " + sceneLoadDiagnostics(result));
    }
    SceneAsset scene{};
    scene.source_path = source_path;
    scene.document = std::move(*result.document);
    if (!assets.registerSceneAsset(key, std::move(scene))) {
      return fail(diagnostic, "failed to register scene asset: " + key);
    }
    addLoaded(handle, "scene", key);
    logAssetPackageEntryDiag(manifest_path,
                             type,
                             key,
                             source_path,
                             entry_start,
                             core::SteadyClock::now());
    return true;
  }

  return fail(diagnostic, "unsupported asset package type: " + type);
}

std::string assimpVersionString() {
  return std::to_string(aiGetVersionMajor()) + "." +
         std::to_string(aiGetVersionMinor()) + "." +
         std::to_string(aiGetVersionRevision());
}

std::string ktxDependencyString() {
#if defined(KARMA_ENABLE_KTX2)
#if !defined(KARMA_KTX_SOFTWARE_TAG)
#define KARMA_KTX_SOFTWARE_TAG "system"
#endif
  return std::string("libktx:") + KARMA_KTX_SOFTWARE_TAG;
#else
  return "no-ktx2";
#endif
}

std::string importerVersionForType(std::string_view type) {
  if (type == "texture_rgba8" || type == "texture") {
    return std::string(detail::textureImporterVersion());
  }
  if (type == "mesh") {
    return "assimp-mesh-v15-submesh-material-slots:" + assimpVersionString();
  }
  if (type == "gltf_scene") {
    return "assimp-gltf-scene-v15-specular-normal-convention:" + assimpVersionString();
  }
  if (type == "animation_clip") {
    return "assimp-animation-clip-v4-full-pose-hierarchy:" + assimpVersionString();
  }
  if (type == "material") {
    return "material-loader-v2";
  }
  if (type == "shader_pass") {
    return "shader-pass-v1";
  }
  if (type == "render_graph") {
    return "render-graph-v1";
  }
  if (type == "particle_effect") {
    return "particle-effect-v3";
  }
  if (type == "environment_map") {
    return "environment-path-v1";
  }
  if (type == "scene") {
    return "scene-document-v1";
  }
  return "unknown";
}

Json packageEntryCacheRecord(const Json& entry,
                             const std::filesystem::path& base_dir,
                             std::string* diagnostic) {
  Json record = Json::object();
  if (!entry.is_object()) {
    return record;
  }
  const std::string type = entry.value("type", std::string{});
  record["type"] = type;
  record["key"] = entry.value("key", std::string{});
  record["importer_version"] = importerVersionForType(type);
  record["entry"] = entry;
  if (const auto path_it = entry.find("path"); path_it != entry.end() && path_it->is_string()) {
    const std::filesystem::path source = resolveEntryPath(base_dir, path_it->get<std::string>());
    record["source"] = source.lexically_normal().generic_string();
    std::error_code ec;
    if (std::filesystem::exists(source, ec)) {
      record["source_size"] = static_cast<uint64_t>(std::filesystem::file_size(source, ec));
      const auto mtime = std::filesystem::last_write_time(source, ec);
      if (!ec) {
        record["source_mtime"] = mtime.time_since_epoch().count();
      }
      if (auto hash = hashFile(source); hash.has_value()) {
        record["source_hash"] = *hash;
      } else if (diagnostic != nullptr) {
        *diagnostic = "failed to hash package source: " + source.string();
      }
    }
    auto append_dependency =
        [&](const char* label, const std::filesystem::path& dependency_path) {
      if (dependency_path.empty()) {
        return;
      }
      Json dependency{{"label", label},
                      {"path", dependency_path.lexically_normal().generic_string()}};
      std::error_code dependency_ec;
      if (std::filesystem::exists(dependency_path, dependency_ec)) {
        dependency["size"] =
            static_cast<uint64_t>(
                std::filesystem::file_size(dependency_path, dependency_ec));
        const auto mtime =
            std::filesystem::last_write_time(dependency_path, dependency_ec);
        if (!dependency_ec) {
          dependency["mtime"] = mtime.time_since_epoch().count();
        }
        if (auto hash = hashFile(dependency_path); hash.has_value()) {
          dependency["hash"] = *hash;
        } else if (diagnostic != nullptr) {
          *diagnostic = "failed to hash asset dependency: " +
                        dependency_path.string();
        }
      } else if (diagnostic != nullptr) {
        *diagnostic = "failed to stat asset dependency: " +
                      dependency_path.string();
      }
      record["dependencies"].push_back(std::move(dependency));
    };
    if (const auto dependencies_it = entry.find("dependencies");
        dependencies_it != entry.end()) {
      if (!dependencies_it->is_array()) {
        if (diagnostic != nullptr) {
          *diagnostic = "asset dependencies must be an array";
        }
        return record;
      }
      for (const Json& dependency : *dependencies_it) {
        if (!dependency.is_string() || dependency.get_ref<const std::string&>().empty()) {
          if (diagnostic != nullptr) {
            *diagnostic = "asset dependency paths must be non-empty strings";
          }
          return record;
        }
        append_dependency(
            "source",
            resolveEntryPath(base_dir, dependency.get_ref<const std::string&>()));
      }
    }
    if (type == "material") {
      std::string material_diagnostic;
      if (auto material = loadMaterialAssetDesc(source, &material_diagnostic);
          material.has_value()) {
        append_dependency("vertex", material->pipeline.vertex_shader_path);
        append_dependency("fragment", material->pipeline.fragment_shader_path);
      } else if (diagnostic != nullptr && !material_diagnostic.empty()) {
        *diagnostic = material_diagnostic;
      }
    } else if (type == "shader_pass") {
      std::string pass_diagnostic;
      if (auto pass = loadShaderPassAssetDesc(source, &pass_diagnostic);
          pass.has_value()) {
        append_dependency("vertex", pass->pipeline.vertex_shader_path);
        append_dependency("fragment", pass->pipeline.fragment_shader_path);
      } else if (diagnostic != nullptr && !pass_diagnostic.empty()) {
        *diagnostic = pass_diagnostic;
      }
    }
  }
  return record;
}

std::string packageCacheKey(const std::filesystem::path& manifest_path,
                            const Json& package_json,
                            std::string_view manifest_hash,
                            std::string* diagnostic) {
  const std::filesystem::path base_dir = packageDirectory(manifest_path);
  Json key{
      {"asset_cache_version", std::string(AssetCache::kAssetCacheVersion)},
      {"package_cache_content_version", std::string(kPackageCacheContentVersion)},
      {"manifest_path", manifest_path.lexically_normal().generic_string()},
      {"manifest_hash", std::string(manifest_hash)},
      {"assimp_version", assimpVersionString()},
      {"ktx_dependency", ktxDependencyString()},
      {"texture_profile", detail::textureProfileVersion()},
      {"package_options", Json::object()},
      {"assets", Json::array()},
  };
  for (const Json& entry : package_json["assets"]) {
    key["assets"].push_back(packageEntryCacheRecord(entry, base_dir, diagnostic));
  }
  return hashString(key.dump());
}

std::string packageAssetBlobKey(std::string_view package_key,
                                std::string_view type,
                                std::string_view key) {
  return hashString(Json{
      {"asset_cache_version", std::string(AssetCache::kAssetCacheVersion)},
      {"package_cache_content_version", std::string(kPackageCacheContentVersion)},
      {"package", std::string(package_key)},
      {"type", std::string(type)},
      {"key", std::string(key)},
  }.dump());
}

void assignPackageBlobKeys(AssetPackageHandle& handle, std::string_view package_key) {
  for (auto& asset : handle.assets) {
    if (asset.type == "environment_map" ||
        asset.type == "shader_pass" ||
        asset.type == "render_graph" ||
        asset.type == "scene") {
      asset.cache_blob_key.clear();
      continue;
    }
    asset.cache_blob_key = packageAssetBlobKey(package_key, asset.type, asset.key);
  }
}

std::string blobTypeForAsset(const AssetRegistry& assets,
                             const AssetPackageLoadedAsset& asset) {
  if (asset.type == "material") {
    if (assets.findMaterialVariant(asset.key) != nullptr) {
      return "material_variant";
    }
    return "material_asset";
  }
  return asset.type;
}

std::filesystem::path cacheBlobPath(const std::filesystem::path& root,
                                    std::string_view cache_key) {
  return root / "blobs" / (std::string(cache_key) + ".kasset");
}

bool writeBytesDirect(const std::filesystem::path& path,
                      const std::vector<uint8_t>& bytes,
                      std::string* diagnostic) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    return fail(diagnostic, "failed to create baked asset blob directory: " + ec.message());
  }

  const std::filesystem::path temp = path.string() + ".tmp";
  {
    std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
    if (!stream) {
      return fail(diagnostic, "failed to open baked asset blob: " + temp.string());
    }
    if (!bytes.empty()) {
      stream.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    stream.flush();
    if (!stream) {
      std::filesystem::remove(temp, ec);
      return fail(diagnostic, "failed to write baked asset blob: " + temp.string());
    }
  }

  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temp, path, ec);
  }
  if (ec) {
    std::filesystem::remove(temp, ec);
    return fail(diagnostic, "failed to commit baked asset blob: " + ec.message());
  }
  return true;
}

std::optional<std::vector<uint8_t>> serializePackageAssetBlob(
    const AssetRegistry& assets,
    const AssetPackageLoadedAsset& asset,
    std::string* diagnostic) {
  if (asset.type == "environment_map" || asset.type == "scene") {
    return std::vector<uint8_t>{};
  }
  if (asset.type == "texture_rgba8" || asset.type == "texture") {
    const TextureAsset* texture = assets.findTextureAsset(asset.key);
    if (texture == nullptr) {
      fail(diagnostic, "missing texture asset for baked blob: " + asset.key);
      return std::nullopt;
    }
    return detail::serializeTexture(*texture);
  }
  if (asset.type == "mesh") {
    const world::MeshData* mesh = assets.findMeshAsset(asset.key);
    if (mesh == nullptr) {
      fail(diagnostic, "missing mesh asset for baked blob: " + asset.key);
      return std::nullopt;
    }
    return detail::serializeMesh(*mesh);
  }
  if (asset.type == "material") {
    if (const rendering::MaterialVariantDesc* variant = assets.findMaterialVariant(asset.key)) {
      return detail::serializeMaterialVariant(*variant);
    }
    const rendering::MaterialAssetDesc* material = assets.findMaterialAsset(asset.key);
    if (material == nullptr) {
      fail(diagnostic, "missing material asset for baked blob: " + asset.key);
      return std::nullopt;
    }
    return detail::serializeMaterialAsset(*material);
  }
  if (asset.type == "particle_effect") {
    const visual::particles::ParticleEffectAsset* effect = assets.findParticleEffect(asset.key);
    if (effect == nullptr) {
      fail(diagnostic, "missing particle effect for baked blob: " + asset.key);
      return std::nullopt;
    }
    return detail::serializeParticleEffect(*effect);
  }
  if (asset.type == "gltf_scene") {
    const GltfSceneAsset* scene = assets.findGltfSceneAsset(asset.key);
    if (scene == nullptr) {
      fail(diagnostic, "missing glTF scene for baked blob: " + asset.key);
      return std::nullopt;
    }
    return detail::serializeGltfScene(*scene);
  }
  if (asset.type == "animation_clip") {
    const world::AnimationClip* clip = assets.findAnimationClip(asset.key);
    if (clip == nullptr) {
      fail(diagnostic, "missing animation clip for baked blob: " + asset.key);
      return std::nullopt;
    }
    return detail::serializeAnimationClip(*clip);
  }
  if (asset.type == "skeleton") {
    const world::Skeleton* skeleton = assets.findSkeleton(asset.key);
    if (skeleton == nullptr) {
      fail(diagnostic, "missing skeleton for baked blob: " + asset.key);
      return std::nullopt;
    }
    return detail::serializeSkeleton(*skeleton);
  }
  if (asset.type == "skin") {
    const world::Skin* skin = assets.findSkin(asset.key);
    if (skin == nullptr) {
      fail(diagnostic, "missing skin for baked blob: " + asset.key);
      return std::nullopt;
    }
    return detail::serializeSkin(*skin);
  }
  fail(diagnostic, "unsupported baked asset type: " + asset.type);
  return std::nullopt;
}

bool writePackageAssetBlobNoIndex(const std::filesystem::path& root,
                                  const AssetRegistry& assets,
                                  const AssetPackageLoadedAsset& asset,
                                  std::string* diagnostic) {
  if (asset.type == "environment_map" || asset.type == "scene") {
    return true;
  }
  if (asset.cache_blob_key.empty()) {
    return fail(diagnostic, "missing baked blob key for package asset: " + asset.key);
  }
  std::optional<std::vector<uint8_t>> bytes =
      serializePackageAssetBlob(assets, asset, diagnostic);
  if (!bytes.has_value()) {
    return false;
  }
  return writeBytesDirect(cacheBlobPath(root, asset.cache_blob_key), *bytes, diagnostic);
}

bool writePackageAssetBlob(AssetCache& cache,
                           const AssetRegistry& assets,
                           const AssetPackageLoadedAsset& asset,
                           std::string* diagnostic) {
  if (asset.type == "environment_map") {
    return true;
  }
  if (asset.type == "shader_pass" ||
      asset.type == "render_graph" ||
      asset.type == "scene") {
    return true;
  }
  if (asset.cache_blob_key.empty()) {
    return fail(diagnostic, "missing cache blob key for package asset: " + asset.key);
  }
  if (asset.type == "texture_rgba8" || asset.type == "texture") {
    const TextureAsset* texture = assets.findTextureAsset(asset.key);
    return texture != nullptr &&
           cache.writeTexture(asset.cache_blob_key, *texture, diagnostic);
  }
  if (asset.type == "mesh") {
    const world::MeshData* mesh = assets.findMeshAsset(asset.key);
    return mesh != nullptr && cache.writeMesh(asset.cache_blob_key, *mesh, diagnostic);
  }
  if (asset.type == "material") {
    if (const rendering::MaterialVariantDesc* variant = assets.findMaterialVariant(asset.key)) {
      return cache.writeMaterialVariant(asset.cache_blob_key, *variant, diagnostic);
    }
    const rendering::MaterialAssetDesc* material = assets.findMaterialAsset(asset.key);
    return material != nullptr &&
           cache.writeMaterialAsset(asset.cache_blob_key, *material, diagnostic);
  }
  if (asset.type == "particle_effect") {
    const visual::particles::ParticleEffectAsset* effect = assets.findParticleEffect(asset.key);
    return effect != nullptr &&
           cache.writeParticleEffect(asset.cache_blob_key, *effect, diagnostic);
  }
  if (asset.type == "gltf_scene") {
    const GltfSceneAsset* scene = assets.findGltfSceneAsset(asset.key);
    return scene != nullptr && cache.writeGltfScene(asset.cache_blob_key, *scene, diagnostic);
  }
  if (asset.type == "animation_clip") {
    const world::AnimationClip* clip = assets.findAnimationClip(asset.key);
    return clip != nullptr && cache.writeAnimationClip(asset.cache_blob_key, *clip, diagnostic);
  }
  if (asset.type == "skeleton") {
    const world::Skeleton* skeleton = assets.findSkeleton(asset.key);
    return skeleton != nullptr &&
           cache.writeSkeleton(asset.cache_blob_key, *skeleton, diagnostic);
  }
  if (asset.type == "skin") {
    const world::Skin* skin = assets.findSkin(asset.key);
    return skin != nullptr && cache.writeSkin(asset.cache_blob_key, *skin, diagnostic);
  }
  if (asset.type == "humanoid_rig") {
    const world::HumanoidRig* rig = assets.findHumanoidRig(asset.key);
    return rig != nullptr && cache.writeHumanoidRig(asset.cache_blob_key, *rig, diagnostic);
  }
  return fail(diagnostic, "unsupported cache asset type: " + asset.type);
}

Json packageCacheManifest(const AssetPackageHandle& handle,
                          const AssetRegistry& assets,
                          std::string_view package_key) {
  Json root{
      {"version", 2},
      {"package_key", std::string(package_key)},
      {"manifest_path", handle.manifest_path.lexically_normal().generic_string()},
      {"assets", Json::array()},
  };
  for (const auto& asset : handle.assets) {
    Json entry{{"type", asset.type},
               {"key", asset.key},
               {"blob_key", asset.cache_blob_key},
               {"blob_type", blobTypeForAsset(assets, asset)}};
    if (asset.type == "environment_map") {
      if (const EnvironmentMapAsset* environment = assets.findEnvironmentMap(asset.key)) {
        entry["path"] = environment->path.lexically_normal().generic_string();
      }
    }
    if (asset.type == "shader_pass") {
      if (const rendering::ShaderPassAssetDesc* pass = assets.findShaderPass(asset.key)) {
        entry["asset"] = shaderPassToJson(*pass);
      }
    }
    if (asset.type == "render_graph") {
      if (const rendering::FrameGraphDesc* graph = assets.findFrameGraph(asset.key)) {
        entry["asset"] = frameGraphToJson(*graph);
      }
    }
    if (asset.type == "scene") {
      if (const SceneAsset* scene = assets.findSceneAsset(asset.key)) {
        entry["path"] = scene->source_path.lexically_normal().generic_string();
      }
    }
    if (asset.type == "gltf_scene") {
      if (const GltfSceneAsset* scene = assets.findGltfSceneAsset(asset.key)) {
        entry["generated"] = Json{
            {"mesh", scene->mesh_asset_keys},
            {"texture", scene->texture_asset_keys},
            {"material", scene->material_keys},
            {"animation_clip", scene->animation_clip_keys},
            {"skeleton", scene->skeleton_keys},
            {"skin", scene->skin_keys},
            {"humanoid_rig", scene->humanoid_rig_keys},
        };
      }
    }
    root["assets"].push_back(std::move(entry));
  }
  return root;
}

bool writePackageCache(AssetCache& cache,
                       const AssetRegistry& staging,
                       const AssetPackageHandle& handle,
                       std::string_view package_key,
                       std::string* diagnostic) {
  if (!cache.enabled()) {
    return false;
  }
  for (const auto& asset : handle.assets) {
    if (!writePackageAssetBlob(cache, staging, asset, diagnostic)) {
      return false;
    }
  }
  return cache.writePackageManifest(package_key,
                                    packageCacheManifest(handle, staging, package_key),
                                    diagnostic);
}

bool restoreCachedTextureAsset(AssetRegistry& staging,
                               const std::filesystem::path& manifest_path,
                               const CachedAssetRecord& record,
                               TextureAsset texture,
                               AssetPackageHandle& handle,
                               std::string* diagnostic,
                               core::SteadyClock::time_point restore_start) {
  if (keyAlreadyExists(staging, record.type, record.key)) {
    addLoaded(handle, record.type, record.key, record.blob_key);
    logAssetPackageCacheAssetDiag(manifest_path,
                                  record.type,
                                  record.key,
                                  record.blob_type,
                                  restore_start,
                                  core::SteadyClock::now());
    return true;
  }
  if (!staging.registerTextureAsset(record.key, std::move(texture))) {
    return fail(diagnostic, "failed to restore cached texture: " + record.key);
  }
  addLoaded(handle, record.type, record.key, record.blob_key);
  logAssetPackageCacheAssetDiag(manifest_path,
                                record.type,
                                record.key,
                                record.blob_type,
                                restore_start,
                                core::SteadyClock::now());
  return true;
}

bool restoreCachedAsset(AssetCache& cache,
                        AssetRegistry& staging,
                        const std::filesystem::path& manifest_path,
                        const Json& entry,
                        AssetPackageHandle& handle,
                        std::string* diagnostic) {
  CachedAssetRecord record;
  if (!readCachedAssetRecord(entry, record, diagnostic)) {
    return false;
  }
  const auto restore_start = core::SteadyClock::now();

  if (keyAlreadyExists(staging, record.type, record.key)) {
    addLoaded(handle, record.type, record.key, record.blob_key);
    logAssetPackageCacheAssetDiag(manifest_path,
                                  record.type,
                                  record.key,
                                  record.blob_type,
                                  restore_start,
                                  core::SteadyClock::now());
    return true;
  }

  if (record.type == "environment_map") {
    const std::filesystem::path path = entry.value("path", std::string{});
    if (!staging.registerEnvironmentMap(record.key, EnvironmentMapAsset{.path = path})) {
      return fail(diagnostic, "failed to restore cached environment map: " + record.key);
    }
    addLoaded(handle, record.type, record.key);
    logAssetPackageCacheAssetDiag(manifest_path,
                                  record.type,
                                  record.key,
                                  record.blob_type,
                                  restore_start,
                                  core::SteadyClock::now());
    return true;
  }
  if (record.type == "shader_pass") {
    const auto asset_it = entry.find("asset");
    if (asset_it == entry.end() || !asset_it->is_object()) {
      return fail(diagnostic, "cached shader pass is missing inline asset: " + record.key);
    }
    auto pass = parseShaderPassAssetDesc(
        *asset_it,
        manifest_path,
        packageDirectory(manifest_path),
        diagnostic);
    if (!pass.has_value() || !staging.registerShaderPass(record.key, std::move(*pass))) {
      return fail(diagnostic, "failed to restore cached shader pass: " + record.key);
    }
    addLoaded(handle, record.type, record.key);
    logAssetPackageCacheAssetDiag(manifest_path,
                                  record.type,
                                  record.key,
                                  record.blob_type,
                                  restore_start,
                                  core::SteadyClock::now());
    return true;
  }
  if (record.type == "render_graph") {
    const auto asset_it = entry.find("asset");
    if (asset_it == entry.end() || !asset_it->is_object()) {
      return fail(diagnostic, "cached render graph is missing inline asset: " + record.key);
    }
    auto graph = parseFrameGraphDesc(*asset_it, diagnostic);
    if (!graph.has_value() || !staging.registerFrameGraph(record.key, std::move(*graph))) {
      return fail(diagnostic, "failed to restore cached render graph: " + record.key);
    }
    addLoaded(handle, record.type, record.key);
    logAssetPackageCacheAssetDiag(manifest_path,
                                  record.type,
                                  record.key,
                                  record.blob_type,
                                  restore_start,
                                  core::SteadyClock::now());
    return true;
  }
  if (record.type == "scene") {
    const std::filesystem::path path = entry.value("path", std::string{});
    scenes::SceneLoadResult result = scenes::loadSceneDocument(path);
    if (!result.success()) {
      return fail(diagnostic, "failed to restore cached scene asset '" + path.string() +
                                  "': " + sceneLoadDiagnostics(result));
    }
    SceneAsset scene{};
    scene.source_path = path;
    scene.document = std::move(*result.document);
    if (!staging.registerSceneAsset(record.key, std::move(scene))) {
      return fail(diagnostic, "failed to restore cached scene asset: " + record.key);
    }
    addLoaded(handle, record.type, record.key);
    logAssetPackageCacheAssetDiag(manifest_path,
                                  record.type,
                                  record.key,
                                  record.blob_type,
                                  restore_start,
                                  core::SteadyClock::now());
    return true;
  }
  if (record.blob_key.empty()) {
    return fail(diagnostic, "package cache asset record is missing blob key: " + record.key);
  }
  if (isTextureBlobType(record.blob_type)) {
    auto texture = cache.readTexture(record.blob_key, diagnostic);
    if (!texture.has_value()) {
      return fail(diagnostic, "failed to restore cached texture: " + record.key);
    }
    return restoreCachedTextureAsset(staging,
                                     manifest_path,
                                     record,
                                     std::move(*texture),
                                     handle,
                                     diagnostic,
                                     restore_start);
  } else if (record.blob_type == "mesh") {
    auto mesh = cache.readMesh(record.blob_key, diagnostic);
    if (!mesh.has_value() || !staging.registerMeshAsset(record.key, std::move(*mesh))) {
      return fail(diagnostic, "failed to restore cached mesh: " + record.key);
    }
  } else if (record.blob_type == "material_asset") {
    auto material = cache.readMaterialAsset(record.blob_key, diagnostic);
    if (!material.has_value() || !staging.registerMaterialAsset(record.key, std::move(*material))) {
      return fail(diagnostic, "failed to restore cached material asset: " + record.key);
    }
  } else if (record.blob_type == "material_variant") {
    auto material = cache.readMaterialVariant(record.blob_key, diagnostic);
    if (!material.has_value() || !staging.registerMaterialVariant(record.key, std::move(*material))) {
      return fail(diagnostic, "failed to restore cached material variant: " + record.key);
    }
  } else if (record.blob_type == "particle_effect") {
    auto effect = cache.readParticleEffect(record.blob_key, diagnostic);
    if (!effect.has_value() || !staging.registerParticleEffect(record.key, std::move(*effect))) {
      return fail(diagnostic, "failed to restore cached particle effect: " + record.key);
    }
  } else if (record.blob_type == "gltf_scene") {
    auto scene = cache.readGltfScene(record.blob_key, diagnostic);
    if (!scene.has_value() || !staging.registerGltfSceneAsset(record.key, std::move(*scene))) {
      return fail(diagnostic, "failed to restore cached glTF scene: " + record.key);
    }
  } else if (record.blob_type == "animation_clip") {
    auto clip = cache.readAnimationClip(record.blob_key, diagnostic);
    if (!clip.has_value() || !staging.registerAnimationClip(record.key, std::move(*clip))) {
      return fail(diagnostic, "failed to restore cached animation clip: " + record.key);
    }
  } else if (record.blob_type == "skeleton") {
    auto skeleton = cache.readSkeleton(record.blob_key, diagnostic);
    if (!skeleton.has_value() || !staging.registerSkeleton(record.key, std::move(*skeleton))) {
      return fail(diagnostic, "failed to restore cached skeleton: " + record.key);
    }
  } else if (record.blob_type == "skin") {
    auto skin = cache.readSkin(record.blob_key, diagnostic);
    if (!skin.has_value() || !staging.registerSkin(record.key, std::move(*skin))) {
      return fail(diagnostic, "failed to restore cached skin: " + record.key);
    }
  } else if (record.blob_type == "humanoid_rig") {
    auto rig = cache.readHumanoidRig(record.blob_key, diagnostic);
    if (!rig.has_value() || !staging.registerHumanoidRig(record.key, std::move(*rig))) {
      return fail(diagnostic, "failed to restore cached humanoid rig: " + record.key);
    }
  } else {
    return fail(diagnostic, "unsupported cached blob type: " + record.blob_type);
  }

  addLoaded(handle, record.type, record.key, record.blob_key);
  logAssetPackageCacheAssetDiag(manifest_path,
                                record.type,
                                record.key,
                                record.blob_type,
                                restore_start,
                                core::SteadyClock::now());
  return true;
}

std::optional<AssetPackageHandle> loadPackageFromCache(AssetCache& cache,
                                                       const std::filesystem::path& manifest_path,
                                                       std::string_view package_key,
                                                       AssetRegistry& staging,
                                                       std::string* diagnostic) {
  auto manifest = cache.readPackageManifest(package_key, diagnostic);
  if (!manifest.has_value()) {
    return std::nullopt;
  }
  if (!manifest->is_object() ||
      manifest->value("version", 0u) != 2u ||
      !manifest->contains("assets") ||
      !(*manifest)["assets"].is_array()) {
    return std::nullopt;
  }

  struct TextureRestoreResult {
    std::optional<TextureAsset> texture;
    std::string diagnostic;
  };
  struct TextureRestoreRecord {
    std::size_t entry_index = 0u;
    CachedAssetRecord record;
  };
  struct TextureRestoreJob {
    std::size_t entry_index = 0u;
    CachedAssetRecord record;
    core::SteadyClock::time_point start;
    std::future<TextureRestoreResult> future;
  };

  const Json& entries = (*manifest)["assets"];
  std::vector<TextureRestoreRecord> texture_records;
  texture_records.reserve(entries.size());
  std::vector<TextureRestoreJob> texture_jobs;
  texture_jobs.reserve(entries.size());
  for (std::size_t index = 0u; index < entries.size(); ++index) {
    CachedAssetRecord record;
    std::string record_diagnostic;
    if (!readCachedAssetRecord(entries[index], record, &record_diagnostic) ||
        record.blob_key.empty() ||
        !isTextureBlobType(record.blob_type)) {
      continue;
    }
    texture_records.push_back(TextureRestoreRecord{
        .entry_index = index,
        .record = std::move(record),
    });
  }

  const uint32_t max_texture_jobs = textureRestoreJobLimit();
  if (startupDiagnosticsEnabled() && !texture_records.empty()) {
    spdlog::info(
        "Engine startup diag: area=asset_package package='{}' stage=cache texture restore schedule ms=0.00 count={} jobs={}",
        manifest_path.string(),
        texture_records.size(),
        max_texture_jobs);
  }

  auto start_texture_job = [&](std::size_t texture_index) {
    const TextureRestoreRecord& texture_record = texture_records[texture_index];
    const auto start = core::SteadyClock::now();
    texture_jobs.push_back(TextureRestoreJob{
        .entry_index = texture_record.entry_index,
        .record = texture_record.record,
        .start = start,
        .future = std::async(std::launch::async,
                             [&cache, blob_key = texture_record.record.blob_key]() {
          TextureRestoreResult result;
          result.texture = cache.readTexture(blob_key, &result.diagnostic);
          return result;
        }),
    });
  };

  std::size_t next_texture_to_start = 0u;
  while (next_texture_to_start < texture_records.size() &&
         next_texture_to_start < max_texture_jobs) {
    start_texture_job(next_texture_to_start++);
  }

  AssetPackageHandle handle{};
  handle.manifest_path = manifest_path;
  handle.restored_from_cache = true;
  std::size_t texture_job_index = 0u;
  for (std::size_t entry_index = 0u; entry_index < entries.size(); ++entry_index) {
    if (texture_job_index < texture_jobs.size() &&
        texture_jobs[texture_job_index].entry_index == entry_index) {
      TextureRestoreJob& job = texture_jobs[texture_job_index];
      TextureRestoreResult result = job.future.get();
      const CachedAssetRecord record = job.record;
      const core::SteadyClock::time_point start = job.start;
      if (next_texture_to_start < texture_records.size()) {
        start_texture_job(next_texture_to_start++);
      }
      if (!result.texture.has_value() ||
          !restoreCachedTextureAsset(staging,
                                     manifest_path,
                                     record,
                                     std::move(*result.texture),
                                     handle,
                                     diagnostic,
                                     start)) {
        if (diagnostic != nullptr && !result.diagnostic.empty()) {
          *diagnostic = result.diagnostic;
        }
        staging.clear();
        return std::nullopt;
      }
      ++texture_job_index;
      continue;
    }
    const Json& entry = entries[entry_index];
    if (!restoreCachedAsset(cache, staging, manifest_path, entry, handle, diagnostic)) {
      staging.clear();
      return std::nullopt;
    }
  }
  return handle;
}

std::optional<AssetPackageHandle> commitStagedPackage(AssetRegistry& target,
                                                      AssetRegistry& staging,
                                                      const AssetPackageHandle& staged,
                                                      std::string* diagnostic,
                                                      bool move_assets = false) {
  for (const auto& asset : staged.assets) {
    if (keyAlreadyExists(target, asset.type, asset.key)) {
      fail(diagnostic, "asset package would overwrite existing key: " + asset.key);
      return std::nullopt;
    }
  }

  AssetPackageHandle committed{};
  committed.manifest_path = staged.manifest_path;
  committed.restored_from_cache = staged.restored_from_cache;
  for (const auto& asset : staged.assets) {
    if (move_assets && target.moveAssetFrom(staging, asset.type, asset.key)) {
      committed.assets.push_back(asset);
      continue;
    }
    if (!copyAssetTo(target, staging, asset, diagnostic)) {
      unloadAssetPackage(target, committed);
      return std::nullopt;
    }
    committed.assets.push_back(asset);
  }
  return committed;
}

std::string normalizedPackageKey(const std::filesystem::path& manifest_path) {
  std::error_code ec;
  std::filesystem::path absolute = std::filesystem::absolute(manifest_path, ec);
  if (ec) {
    absolute = manifest_path;
  }
  return absolute.lexically_normal().generic_string();
}

std::filesystem::path bakedDescriptorPath(const std::filesystem::path& baked_cache_path) {
  if (baked_cache_path.filename() == "baked.package.json") {
    return baked_cache_path;
  }
  return baked_cache_path / "baked.package.json";
}

std::filesystem::path bakedRootPath(const std::filesystem::path& baked_cache_path) {
  const std::filesystem::path descriptor = bakedDescriptorPath(baked_cache_path);
  return descriptor.parent_path().empty() ? std::filesystem::path(".")
                                          : descriptor.parent_path();
}

std::string bakedPackageId(const std::filesystem::path& output_dir,
                           const AssetPackageBakeOptions& options) {
  if (!options.package_id.empty()) {
    return options.package_id;
  }
  const std::string filename = output_dir.filename().generic_string();
  return filename.empty() ? "asset_package" : filename;
}

std::string bakedPackageKey(std::string_view package_id) {
  return hashString(Json{
      {"schema", "karma.baked_asset_package.blobs"},
      {"version", 1},
      {"package_id", std::string(package_id)},
  }.dump());
}

bool readJsonObjectFile(const std::filesystem::path& path,
                        Json& out,
                        std::string* diagnostic) {
  std::ifstream stream(path);
  if (!stream) {
    return fail(diagnostic, "failed to open JSON file: " + path.string());
  }
  try {
    stream >> out;
  } catch (const std::exception& e) {
    return fail(diagnostic, std::string("failed to parse JSON file: ") + e.what());
  }
  if (!out.is_object()) {
    return fail(diagnostic, "JSON file root must be an object: " + path.string());
  }
  return true;
}

bool writeJsonObjectFile(const std::filesystem::path& path,
                         const Json& json,
                         std::string* diagnostic) {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return fail(diagnostic, "failed to create baked package directory: " + ec.message());
    }
  }
  std::ofstream stream(path, std::ios::trunc);
  if (!stream) {
    return fail(diagnostic, "failed to open baked package descriptor: " + path.string());
  }
  stream << json.dump(2) << '\n';
  return static_cast<bool>(stream);
}

std::optional<std::string> packageSourceFingerprint(
    const std::filesystem::path& source_package_path,
    const Json& package_json,
    std::string* diagnostic) {
  const std::filesystem::path manifest_path = resolveAssetPackagePath(source_package_path);
  const std::string manifest_hash =
      hashFile(manifest_path).value_or(hashString(manifest_path.lexically_normal().generic_string()));
  std::string fingerprint_diagnostic;
  const std::string fingerprint =
      packageCacheKey(manifest_path, package_json, manifest_hash, &fingerprint_diagnostic);
  if (!fingerprint_diagnostic.empty()) {
    fail(diagnostic, fingerprint_diagnostic);
    return std::nullopt;
  }
  return fingerprint;
}

std::optional<AssetPackageHandle> restorePackageManifest(
    AssetCache& cache,
    const std::filesystem::path& manifest_path,
    const Json& manifest,
    AssetRegistry& staging,
    std::string* diagnostic) {
  if (!manifest.is_object() ||
      manifest.value("version", 0u) != 2u ||
      !manifest.contains("assets") ||
      !manifest["assets"].is_array()) {
    fail(diagnostic, "baked package manifest is malformed");
    return std::nullopt;
  }

  AssetPackageHandle handle{};
  handle.manifest_path = manifest_path;
  for (const Json& entry : manifest["assets"]) {
    if (!restoreCachedAsset(cache, staging, manifest_path, entry, handle, diagnostic)) {
      staging.clear();
      return std::nullopt;
    }
  }
  return handle;
}

}  // namespace

std::filesystem::path resolveAssetPackagePath(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::is_directory(path, ec)) {
    return path / "assets.package.json";
  }
  if (path.extension().empty()) {
    return path / "assets.package.json";
  }
  return path;
}

std::optional<AssetPackageHandle> importAssetPackage(AssetRegistry& assets,
                                                     const std::filesystem::path& path,
                                                     std::string* diagnostic) {
  return importAssetPackage(assets, path, AssetPackageOptions{}, diagnostic);
}

std::optional<AssetPackageHandle> importAssetPackage(AssetRegistry& assets,
                                                     const std::filesystem::path& path,
                                                     const AssetPackageOptions& options,
                                                     std::string* diagnostic) {
  const std::filesystem::path manifest_path = resolveAssetPackagePath(path);
  const auto package_start = core::SteadyClock::now();
  auto stage_start = package_start;
  Json root;
  if (!readJson(manifest_path, root, diagnostic)) {
    return std::nullopt;
  }
  logAssetPackageDiag(manifest_path,
                      "manifest read",
                      stage_start,
                      core::SteadyClock::now(),
                      root["assets"].size());
  stage_start = core::SteadyClock::now();
  AssetCache cache(options.cache);
  const std::string manifest_hash =
      hashFile(manifest_path).value_or(hashString(manifest_path.lexically_normal().generic_string()));
  logAssetPackageDiag(manifest_path, "manifest hash", stage_start, core::SteadyClock::now());
  stage_start = core::SteadyClock::now();
  std::string package_cache_diagnostic;
  const std::string package_cache_key =
      packageCacheKey(manifest_path,
                      root,
                      manifest_hash,
                      &package_cache_diagnostic);
  if (!package_cache_diagnostic.empty()) {
    fail(diagnostic, package_cache_diagnostic);
    return std::nullopt;
  }
  logAssetPackageDiag(manifest_path, "cache key build", stage_start, core::SteadyClock::now());

  if (cache.enabled()) {
    AssetRegistry cached_staging;
    std::string cache_diagnostic;
    stage_start = core::SteadyClock::now();
    if (auto cached = loadPackageFromCache(cache,
                                           manifest_path,
                                           package_cache_key,
                                           cached_staging,
                                           &cache_diagnostic)) {
      logAssetPackageDiag(manifest_path,
                          "cache restore",
                          stage_start,
                          core::SteadyClock::now(),
                          cached->assets.size());
      stage_start = core::SteadyClock::now();
      if (auto committed = commitStagedPackage(assets,
                                               cached_staging,
                                               *cached,
                                               diagnostic,
                                               true)) {
        logAssetPackageDiag(manifest_path,
                            "cache commit",
                            stage_start,
                            core::SteadyClock::now(),
                            committed->assets.size());
        logAssetPackageDiag(manifest_path,
                            "total cache hit",
                            package_start,
                            core::SteadyClock::now(),
                            committed->assets.size());
        return committed;
      }
      return std::nullopt;
    }
    logAssetPackageDiag(manifest_path, "cache miss lookup", stage_start, core::SteadyClock::now());
  }

  AssetRegistry staging;
  AssetPackageHandle staged{};
  staged.manifest_path = manifest_path;
  const std::filesystem::path base_dir = packageDirectory(manifest_path);
  stage_start = core::SteadyClock::now();
  for (const Json& entry : root["assets"]) {
    if (!importEntry(staging, entry, manifest_path, base_dir, staged, diagnostic)) {
      return std::nullopt;
    }
  }
  logAssetPackageDiag(manifest_path,
                      "source import",
                      stage_start,
                      core::SteadyClock::now(),
                      staged.assets.size());
  stage_start = core::SteadyClock::now();
  assignPackageBlobKeys(staged, package_cache_key);
  logAssetPackageDiag(manifest_path, "assign cache blob keys", stage_start, core::SteadyClock::now());

  stage_start = core::SteadyClock::now();
  auto committed = commitStagedPackage(assets, staging, staged, diagnostic);
  if (!committed.has_value()) {
    return std::nullopt;
  }
  logAssetPackageDiag(manifest_path,
                      "commit staged",
                      stage_start,
                      core::SteadyClock::now(),
                      committed->assets.size());

  std::string cache_write_diagnostic;
  stage_start = core::SteadyClock::now();
  (void)writePackageCache(cache, staging, staged, package_cache_key, &cache_write_diagnostic);
  logAssetPackageDiag(manifest_path,
                      "cache write",
                      stage_start,
                      core::SteadyClock::now(),
                      staged.assets.size());
  logAssetPackageDiag(manifest_path,
                      "total source import",
                      package_start,
                      core::SteadyClock::now(),
                      committed->assets.size());
  return committed;
}

bool bakeAssetPackage(const std::filesystem::path& source_package_path,
                      const std::filesystem::path& output_dir,
                      const AssetPackageBakeOptions& options,
                      std::string* diagnostic) {
  const std::filesystem::path manifest_path = resolveAssetPackagePath(source_package_path);
  Json root;
  if (!readJson(manifest_path, root, diagnostic)) {
    return false;
  }
  std::optional<std::string> source_fingerprint =
      packageSourceFingerprint(manifest_path, root, diagnostic);
  if (!source_fingerprint.has_value()) {
    return false;
  }

  AssetRegistry staging;
  std::optional<AssetPackageHandle> imported =
      importAssetPackage(staging, manifest_path, options.import_options, diagnostic);
  if (!imported.has_value()) {
    return false;
  }

  const std::string package_id = bakedPackageId(output_dir, options);
  AssetPackageHandle baked = *imported;
  baked.manifest_path = manifest_path;
  assignPackageBlobKeys(baked, bakedPackageKey(package_id));

  const std::filesystem::path root_path = bakedRootPath(output_dir);
  for (const AssetPackageLoadedAsset& asset : baked.assets) {
    if (!writePackageAssetBlobNoIndex(root_path, staging, asset, diagnostic)) {
      return false;
    }
  }

  const Json package_manifest = packageCacheManifest(baked,
                                                     staging,
                                                     bakedPackageKey(package_id));
  Json blob_keys = Json::array();
  for (const AssetPackageLoadedAsset& asset : baked.assets) {
    if (!asset.cache_blob_key.empty()) {
      blob_keys.push_back(asset.cache_blob_key);
    }
  }
  std::sort(blob_keys.begin(), blob_keys.end(), [](const Json& a, const Json& b) {
    return a.get<std::string>() < b.get<std::string>();
  });

  Json descriptor{
      {"schema", "karma.baked_asset_package"},
      {"version", 1},
      {"asset_cache_version", std::string(AssetCache::kAssetCacheVersion)},
      {"package_cache_content_version", std::string(kPackageCacheContentVersion)},
      {"package_id", package_id},
      {"source_package_path", manifest_path.lexically_normal().generic_string()},
      {"scene_fingerprint", options.scene_fingerprint},
      {"source_fingerprint", *source_fingerprint},
      {"manifest", root},
      {"package_manifest", package_manifest},
      {"blob_keys", blob_keys},
  };
  return writeJsonObjectFile(bakedDescriptorPath(output_dir), descriptor, diagnostic);
}

std::optional<AssetPackageHandle> importBakedAssetPackage(
    AssetRegistry& assets,
    const std::filesystem::path& baked_cache_path,
    std::string* diagnostic) {
  const std::filesystem::path descriptor_path = bakedDescriptorPath(baked_cache_path);
  Json descriptor;
  if (!readJsonObjectFile(descriptor_path, descriptor, diagnostic)) {
    return std::nullopt;
  }
  if (descriptor.value("schema", std::string{}) != "karma.baked_asset_package" ||
      descriptor.value("version", 0u) != 1u ||
      !descriptor.contains("package_manifest") ||
      !descriptor["package_manifest"].is_object()) {
    fail(diagnostic, "baked asset package descriptor is malformed: " +
                         descriptor_path.string());
    return std::nullopt;
  }

  AssetCacheConfig cache_config{};
  cache_config.root = bakedRootPath(baked_cache_path);
  cache_config.enabled = true;
  cache_config.flush = false;
  cache_config.ensure_layout = false;
  AssetCache cache(cache_config);
  AssetRegistry staging;
  std::optional<AssetPackageHandle> staged =
      restorePackageManifest(cache,
                             descriptor_path,
                             descriptor["package_manifest"],
                             staging,
                             diagnostic);
  if (!staged.has_value()) {
    return std::nullopt;
  }
  std::optional<AssetPackageHandle> committed =
      commitStagedPackage(assets, staging, *staged, diagnostic, true);
  if (!committed.has_value()) {
    return std::nullopt;
  }
  committed->manifest_path = descriptor_path;
  return committed;
}

bool checkBakedAssetPackage(const std::filesystem::path& source_package_path,
                            const std::filesystem::path& baked_cache_path,
                            const AssetPackageBakeOptions& options,
                            std::string* diagnostic) {
  const std::filesystem::path descriptor_path = bakedDescriptorPath(baked_cache_path);
  Json descriptor;
  if (!readJsonObjectFile(descriptor_path, descriptor, diagnostic)) {
    return false;
  }
  if (descriptor.value("schema", std::string{}) != "karma.baked_asset_package" ||
      descriptor.value("version", 0u) != 1u ||
      !descriptor.contains("package_manifest") ||
      !descriptor["package_manifest"].is_object()) {
    return fail(diagnostic, "baked asset package descriptor is malformed: " +
                            descriptor_path.string());
  }
  if (!options.scene_fingerprint.empty() &&
      descriptor.value("scene_fingerprint", std::string{}) != options.scene_fingerprint) {
    return fail(diagnostic, "baked asset package scene fingerprint is stale: " +
                            descriptor_path.string());
  }
  const std::string expected_package_id = bakedPackageId(baked_cache_path, options);
  if (descriptor.value("package_id", std::string{}) != expected_package_id) {
    return fail(diagnostic, "baked asset package id is stale: " + descriptor_path.string());
  }

  Json root;
  if (!readJson(resolveAssetPackagePath(source_package_path), root, diagnostic)) {
    return false;
  }
  std::optional<std::string> source_fingerprint =
      packageSourceFingerprint(source_package_path, root, diagnostic);
  if (!source_fingerprint.has_value()) {
    return false;
  }
  if (descriptor.value("source_fingerprint", std::string{}) != *source_fingerprint) {
    return fail(diagnostic, "baked asset package source fingerprint is stale: " +
                            descriptor_path.string());
  }

  const Json& assets = descriptor["package_manifest"]["assets"];
  if (!assets.is_array()) {
    return fail(diagnostic, "baked asset package manifest assets are malformed: " +
                            descriptor_path.string());
  }
  const std::filesystem::path root_path = bakedRootPath(baked_cache_path);
  for (const Json& asset : assets) {
    CachedAssetRecord record;
    if (!readCachedAssetRecord(asset, record, diagnostic)) {
      return false;
    }
    if (record.blob_key.empty()) {
      continue;
    }
    std::error_code ec;
    if (!std::filesystem::exists(cacheBlobPath(root_path, record.blob_key), ec)) {
      return fail(diagnostic, "baked asset blob is missing: " + record.blob_key);
    }
  }
  return true;
}

struct AssetPackageJob::State {
  std::filesystem::path path;
  AssetPackageOptions options;
  AssetRegistry staging;
  std::optional<AssetPackageHandle> handle;
  std::string diagnostic;
  std::future<void> future;
  std::atomic_bool complete{false};
  std::atomic_bool success{false};
};

AssetPackageJob::AssetPackageJob() = default;
AssetPackageJob::~AssetPackageJob() = default;
AssetPackageJob::AssetPackageJob(std::shared_ptr<State> state) : state_(std::move(state)) {}
AssetPackageJob::AssetPackageJob(AssetPackageJob&&) noexcept = default;
AssetPackageJob& AssetPackageJob::operator=(AssetPackageJob&&) noexcept = default;

bool AssetPackageJob::valid() const {
  return static_cast<bool>(state_);
}

bool AssetPackageJob::ready() const {
  if (!state_) {
    return false;
  }
  if (state_->complete.load(std::memory_order_acquire)) {
    return true;
  }
  return state_->future.valid() &&
         state_->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void AssetPackageJob::wait() {
  if (!state_) {
    return;
  }
  if (state_->future.valid()) {
    state_->future.wait();
  }
}

bool AssetPackageJob::success() const {
  return state_ != nullptr &&
         state_->complete.load(std::memory_order_acquire) &&
         state_->success.load(std::memory_order_acquire);
}

const std::string& AssetPackageJob::diagnostic() const {
  static const std::string empty;
  return state_ != nullptr && state_->complete.load(std::memory_order_acquire)
             ? state_->diagnostic
             : empty;
}

const AssetPackageHandle* AssetPackageJob::handle() const {
  return state_ != nullptr &&
                 state_->complete.load(std::memory_order_acquire) &&
                 state_->handle.has_value()
             ? &*state_->handle
             : nullptr;
}

AssetPackageJob loadAssetPackageAsync(const std::filesystem::path& path,
                                      const AssetPackageOptions& options) {
  auto state = std::make_shared<AssetPackageJob::State>();
  state->path = path;
  state->options = options;
  state->future = std::async(std::launch::async, [state]() {
    try {
      state->handle =
          importAssetPackage(state->staging, state->path, state->options, &state->diagnostic);
      state->success.store(state->handle.has_value(), std::memory_order_release);
      if (!state->success.load(std::memory_order_acquire) && state->diagnostic.empty()) {
        state->diagnostic = "asset package import failed";
      }
    } catch (const std::exception& e) {
      state->diagnostic = e.what();
      state->success.store(false, std::memory_order_release);
    } catch (...) {
      state->diagnostic = "unknown asset package import failure";
      state->success.store(false, std::memory_order_release);
    }
    state->complete.store(true, std::memory_order_release);
  });
  return AssetPackageJob(std::move(state));
}

bool commitAssetPackageJob(AssetRegistry& assets,
                           AssetPackageJob& job,
                           AssetPackageHandle* out_handle) {
  if (!job.state_) {
    return false;
  }
  job.wait();
  if (job.state_->future.valid()) {
    job.state_->future.get();
  }
  if (!job.state_->success.load(std::memory_order_acquire) ||
      !job.state_->handle.has_value()) {
    return false;
  }

  auto committed = commitStagedPackage(assets,
                                       job.state_->staging,
                                       *job.state_->handle,
                                       &job.state_->diagnostic,
                                       true);
  if (!committed.has_value()) {
    return false;
  }

  if (out_handle != nullptr) {
    *out_handle = *committed;
  }
  return true;
}

bool unloadAssetPackage(AssetRegistry& assets, const AssetPackageHandle& package) {
  bool removed_any = false;
  for (auto it = package.assets.rbegin(); it != package.assets.rend(); ++it) {
    if (it->type == "texture_rgba8" || it->type == "texture") {
      removed_any = assets.unregisterTextureAsset(it->key) || removed_any;
    } else if (it->type == "mesh") {
      removed_any = assets.unregisterMeshAsset(it->key) || removed_any;
    } else if (it->type == "material") {
      removed_any = assets.unregisterMaterial(it->key) || removed_any;
    } else if (it->type == "particle_effect") {
      removed_any = assets.unregisterParticleEffect(it->key) || removed_any;
    } else if (it->type == "environment_map") {
      removed_any = assets.unregisterEnvironmentMap(it->key) || removed_any;
    } else if (it->type == "gltf_scene") {
      removed_any = assets.unregisterGltfSceneAsset(it->key) || removed_any;
    } else if (it->type == "shader_pass") {
      removed_any = assets.unregisterShaderPass(it->key) || removed_any;
    } else if (it->type == "render_graph") {
      removed_any = assets.unregisterFrameGraph(it->key) || removed_any;
    } else if (it->type == "scene") {
      removed_any = assets.unregisterSceneAsset(it->key) || removed_any;
    } else if (it->type == "animation_clip") {
      removed_any = assets.unregisterAnimationClip(it->key) || removed_any;
    } else if (it->type == "skeleton") {
      removed_any = assets.unregisterSkeleton(it->key) || removed_any;
    } else if (it->type == "skin") {
      removed_any = assets.unregisterSkin(it->key) || removed_any;
    } else if (it->type == "humanoid_rig") {
      removed_any = assets.unregisterHumanoidRig(it->key) || removed_any;
    }
  }
  return removed_any;
}

AssetPackageStore::AssetPackageStore(AssetRegistry& assets,
                                     AssetPackageOptions options)
    : assets_(&assets), options_(std::move(options)) {}

static_assert(std::is_nothrow_move_constructible_v<AssetPackageHandle>);

AssetPackageStore::~AssetPackageStore() {
  clear();
}

std::string AssetPackageStore::packageKey(const std::filesystem::path& manifest_path) const {
  return normalizedPackageKey(manifest_path);
}

std::optional<AssetPackageHandle> AssetPackageStore::acquirePackage(
    const std::filesystem::path& path,
    std::string* diagnostic) {
  const std::lock_guard lock(mutex_);
  if (assets_ == nullptr) {
    return std::nullopt;
  }
  const std::filesystem::path manifest_path = resolveAssetPackagePath(path);
  const std::string key = packageKey(manifest_path);
  auto existing = records_.find(key);
  if (existing != records_.end()) {
    if (existing->second.ref_count == std::numeric_limits<uint32_t>::max()) {
      fail(diagnostic, "asset package reference count overflow: " + key);
      return std::nullopt;
    }
    // Copy first: a handle allocation failure must not increment the store's
    // ownership count without returning a releasable token to the caller.
    AssetPackageHandle acquired = existing->second.handle;
    existing->second.ref_count += 1u;
    return std::optional<AssetPackageHandle>(std::move(acquired));
  }

  auto imported = importAssetPackage(*assets_, manifest_path, options_, diagnostic);
  if (!imported.has_value()) {
    return std::nullopt;
  }
  uint64_t instance_id = next_instance_id_;
  while (instance_id == 0u || keys_by_instance_id_.contains(instance_id)) {
    ++instance_id;
  }
  imported->instance_id = instance_id;

  AssetPackageHandle returned;
  bool record_inserted = false;
  bool id_inserted = false;
  try {
    returned = *imported;
    Record record{};
    record.handle = std::move(*imported);
    record.ref_count = 1u;
    const auto [record_it, inserted] =
        records_.emplace(key, std::move(record));
    (void)record_it;
    if (!inserted) {
      throw std::logic_error("duplicate asset package store key");
    }
    record_inserted = true;
    const auto [id_it, inserted_id] =
        keys_by_instance_id_.emplace(instance_id, key);
    (void)id_it;
    if (!inserted_id) {
      throw std::logic_error("duplicate asset package instance id");
    }
    id_inserted = true;
    next_instance_id_ = instance_id + 1u;
    if (next_instance_id_ == 0u) next_instance_id_ = 1u;
    return std::optional<AssetPackageHandle>(std::move(returned));
  } catch (...) {
    if (id_inserted) keys_by_instance_id_.erase(instance_id);
    if (record_inserted) {
      const auto record_it = records_.find(key);
      if (record_it != records_.end()) {
        unloadAssetPackage(*assets_, record_it->second.handle);
        records_.erase(record_it);
      }
    } else if (imported->valid()) {
      unloadAssetPackage(*assets_, *imported);
    } else if (returned.valid()) {
      unloadAssetPackage(*assets_, returned);
    }
    throw;
  }
}

std::optional<AssetPackageHandle> AssetPackageStore::acquireBakedPackage(
    const std::filesystem::path& baked_cache_path,
    std::string* diagnostic) {
  return acquireBakedPackage(baked_cache_path, {}, diagnostic);
}

std::optional<AssetPackageHandle> AssetPackageStore::acquireBakedPackage(
    const std::filesystem::path& baked_cache_path,
    const std::filesystem::path& source_package_path,
    std::string* diagnostic) {
  const std::lock_guard lock(mutex_);
  if (assets_ == nullptr) {
    return std::nullopt;
  }
  const std::filesystem::path descriptor_path = bakedDescriptorPath(baked_cache_path);
  std::filesystem::path source_manifest_path;
  if (!source_package_path.empty()) {
    source_manifest_path = resolveAssetPackagePath(source_package_path);
  } else {
    // Baked descriptors retain the source manifest locator specifically so a
    // source request and a baked request can participate in one ownership
    // record. Failure to inspect it is non-fatal here: the normal baked import
    // below owns descriptor validation and its user-facing diagnostic.
    Json descriptor;
    std::string ignored_diagnostic;
    if (readJsonObjectFile(descriptor_path, descriptor, &ignored_diagnostic)) {
      const auto source_it = descriptor.find("source_package_path");
      if (source_it != descriptor.end() && source_it->is_string()) {
        const std::filesystem::path embedded_source =
            source_it->get<std::string>();
        if (!embedded_source.empty()) {
          source_manifest_path = resolveAssetPackagePath(embedded_source);
        }
      }
    }
  }
  const std::string key = packageKey(source_manifest_path.empty()
                                         ? descriptor_path
                                         : source_manifest_path);
  auto existing = records_.find(key);
  if (existing != records_.end()) {
    if (existing->second.ref_count == std::numeric_limits<uint32_t>::max()) {
      fail(diagnostic, "baked asset package reference count overflow: " + key);
      return std::nullopt;
    }
    AssetPackageHandle acquired = existing->second.handle;
    existing->second.ref_count += 1u;
    return std::optional<AssetPackageHandle>(std::move(acquired));
  }

  auto imported = importBakedAssetPackage(*assets_, descriptor_path, diagnostic);
  if (!imported.has_value()) {
    return std::nullopt;
  }
  uint64_t instance_id = next_instance_id_;
  while (instance_id == 0u || keys_by_instance_id_.contains(instance_id)) {
    ++instance_id;
  }
  imported->instance_id = instance_id;

  AssetPackageHandle returned;
  bool record_inserted = false;
  bool id_inserted = false;
  try {
    returned = *imported;
    Record record{};
    record.handle = std::move(*imported);
    record.ref_count = 1u;
    const auto [record_it, inserted] =
        records_.emplace(key, std::move(record));
    (void)record_it;
    if (!inserted) {
      throw std::logic_error("duplicate baked asset package store key");
    }
    record_inserted = true;
    const auto [id_it, inserted_id] =
        keys_by_instance_id_.emplace(instance_id, key);
    (void)id_it;
    if (!inserted_id) {
      throw std::logic_error("duplicate baked asset package instance id");
    }
    id_inserted = true;
    next_instance_id_ = instance_id + 1u;
    if (next_instance_id_ == 0u) next_instance_id_ = 1u;
    return std::optional<AssetPackageHandle>(std::move(returned));
  } catch (...) {
    if (id_inserted) keys_by_instance_id_.erase(instance_id);
    if (record_inserted) {
      const auto record_it = records_.find(key);
      if (record_it != records_.end()) {
        unloadAssetPackage(*assets_, record_it->second.handle);
        records_.erase(record_it);
      }
    } else if (imported->valid()) {
      unloadAssetPackage(*assets_, *imported);
    } else if (returned.valid()) {
      unloadAssetPackage(*assets_, returned);
    }
    throw;
  }
}

bool AssetPackageStore::releasePackage(const AssetPackageHandle& package) {
  const std::lock_guard lock(mutex_);
  std::string key;
  if (package.instance_id != 0u) {
    const auto id_it = keys_by_instance_id_.find(package.instance_id);
    if (id_it == keys_by_instance_id_.end()) {
      // A nonzero id is an exact ownership generation. Falling back to its
      // path would let a stale token release a newly reacquired package.
      return false;
    }
    key = id_it->second;
  } else {
    key = packageKey(resolveAssetPackagePath(package.manifest_path));
  }
  auto it = records_.find(key);
  if (it == records_.end()) {
    return false;
  }
  if (it->second.ref_count > 0u) {
    it->second.ref_count -= 1u;
  }
  if (it->second.ref_count == 0u) {
    if (assets_ != nullptr) {
      unloadAssetPackage(*assets_, it->second.handle);
    }
    keys_by_instance_id_.erase(it->second.handle.instance_id);
    records_.erase(it);
  }
  return true;
}

void AssetPackageStore::clear() {
  const std::lock_guard lock(mutex_);
  if (assets_ != nullptr) {
    for (auto& [key, record] : records_) {
      (void)key;
      unloadAssetPackage(*assets_, record.handle);
    }
  }
  records_.clear();
  keys_by_instance_id_.clear();
}

}  // namespace karma::assets
