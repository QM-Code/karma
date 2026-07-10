#include "../backend.hpp"

#include "../backend_internal.h"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>

#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <future>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

namespace karma::rendering::backend {

LoadedImage decodeEmbeddedAssimpTexture(const aiTexture& texture) {
  if (texture.mHeight == 0) {
    if (texture.mWidth > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
      return {};
    }
    return loadImageFromMemory(reinterpret_cast<const unsigned char*>(texture.pcData),
                               texture.mWidth);
  }

  LoadedImage image{};
  image.width = static_cast<int>(texture.mWidth);
  image.height = static_cast<int>(texture.mHeight);
  std::size_t byte_count = 0u;
  if (!rendering::tryTextureDataSize(image.width, image.height, 4u, byte_count)) {
    return {};
  }
  image.pixels.resize(byte_count);
  for (int output_y = 0; output_y < image.height; ++output_y) {
    const int source_y = image.height - output_y - 1;
    const aiTexel* source_row =
        texture.pcData + static_cast<std::size_t>(source_y) * image.width;
    unsigned char* output_row =
        image.pixels.data() + static_cast<std::size_t>(output_y) * image.width * 4u;
    for (int x = 0; x < image.width; ++x) {
      const aiTexel& source = source_row[x];
      const std::size_t output = static_cast<std::size_t>(x) * 4u;
      output_row[output + 0u] = source.r;
      output_row[output + 1u] = source.g;
      output_row[output + 2u] = source.b;
      output_row[output + 3u] = source.a;
    }
  }
  return image;
}

std::string makeMaterialTextureCacheKey(std::string_view source_key,
                                        bool srgb,
                                        bool generate_mips) {
  std::string key(source_key);
  key.append("|karma-texture:");
  key.push_back(srgb ? 's' : 'l');
  key.push_back(generate_mips ? 'm' : '1');
  return key;
}

namespace {
enum TextureCoordSlot : size_t {
  kTexCoordBaseColor = 0,
  kTexCoordNormal = 1,
  kTexCoordMetallicRoughness = 2,
  kTexCoordOcclusion = 3,
  kTexCoordEmissive = 4,
  kTexCoordClearcoat = 5,
  kTexCoordClearcoatRoughness = 6,
  kTexCoordClearcoatNormal = 7,
  kTexCoordSheenColor = 8,
  kTexCoordSheenRoughness = 9,
  kTexCoordTransmission = 10,
  kTexCoordThickness = 11,
};

rendering::MaterialDesc buildImportedMaterialDesc(const aiMaterial& material) {
  rendering::MaterialDesc desc{};
  desc.base_color = {1.0f, 1.0f, 1.0f, 1.0f};

  aiColor4D base_factor(1.0f, 1.0f, 1.0f, 1.0f);
  if (material.Get(AI_MATKEY_BASE_COLOR, base_factor) == AI_SUCCESS) {
    desc.base_color = {base_factor.r, base_factor.g, base_factor.b, base_factor.a};
  } else {
    aiColor3D diffuse(1.0f, 1.0f, 1.0f);
    if (material.Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
      desc.base_color = {diffuse.r, diffuse.g, diffuse.b, 1.0f};
    }
  }

  float opacity = desc.base_color.a;
  if (material.Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
    desc.base_color.a = opacity;
  }

  int two_sided = 0;
  if (material.Get(AI_MATKEY_TWOSIDED, two_sided) == AI_SUCCESS) {
    desc.double_sided = two_sided != 0;
  }

  if (float alpha_cutoff = desc.alpha_cutoff;
      material.Get(AI_MATKEY_GLTF_ALPHACUTOFF, alpha_cutoff) == AI_SUCCESS) {
    desc.alpha_cutoff = alpha_cutoff;
  }

  aiString alpha_mode;
  const bool has_gltf_alpha_mode =
      material.Get(AI_MATKEY_GLTF_ALPHAMODE, alpha_mode) == AI_SUCCESS;
  const std::string alpha_mode_value = has_gltf_alpha_mode ? alpha_mode.C_Str() : "";
  if (alpha_mode_value == "MASK" || alpha_mode_value == "mask") {
    desc.alpha_mode = rendering::MaterialDesc::AlphaMode::Masked;
    desc.transparent = false;
  } else if (alpha_mode_value == "BLEND" || alpha_mode_value == "blend") {
    desc.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
    desc.transparent = true;
    desc.depth_write = false;
  } else {
    desc.transparent = desc.base_color.a < 0.999f;
    if (desc.transparent) {
      desc.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
      desc.depth_write = false;
    }
  }
  return desc;
}

struct AssimpTextureImportRef {
  std::string key;
  std::string raw_key;
  std::filesystem::path resolved_path;
  std::string label;
  bool srgb = false;
  bool embedded = false;
};

int embeddedTextureIndex(const std::string& raw_key) {
  if (raw_key.size() < 2 || raw_key[0] != '*') {
    return -1;
  }
  char* end = nullptr;
  const long parsed = std::strtol(raw_key.c_str() + 1, &end, 10);
  if (end == nullptr || *end != '\0' || parsed < 0 ||
      parsed > static_cast<long>(std::numeric_limits<int>::max())) {
    return -1;
  }
  return static_cast<int>(parsed);
}

LoadedImage decodeImportedTextureBytes(const rendering::ImportedMaterialTexture& texture) {
  LoadedImage image{};
  if (texture.source_bytes.empty()) {
    return image;
  }
  if (texture.compressed) {
    return loadImageFromMemory(texture.source_bytes.data(), texture.source_bytes.size());
  }

  if (texture.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      texture.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return image;
  }
  image.width = static_cast<int>(texture.width);
  image.height = static_cast<int>(texture.height);
  std::size_t expected_size = 0u;
  if (!rendering::tryTextureDataSize(image.width,
                                     image.height,
                                     4u,
                                     expected_size)) {
    return {};
  }
  if (texture.source_bytes.size() < expected_size) {
    return {};
  }
  image.pixels.resize(expected_size);
  std::memcpy(image.pixels.data(), texture.source_bytes.data(), expected_size);
  return image;
}

void appendAssimpTextureRef(std::vector<AssimpTextureImportRef>& refs,
                            std::unordered_set<std::string>& seen_keys,
                            const std::string& model_key,
                            const std::filesystem::path& base_dir,
                            const aiString& tex_path,
                            bool srgb,
                            bool generate_mips,
                            const char* label) {
  if (tex_path.length == 0) {
    return;
  }
  const std::string raw_key = tex_path.C_Str();
  const bool embedded = !raw_key.empty() && raw_key[0] == '*';
  const std::filesystem::path resolved_path = embedded ? std::filesystem::path{} : (base_dir / raw_key);
  const std::string source_key =
      embedded ? model_key + ":" + raw_key : resolved_path.string();
  std::string key = makeMaterialTextureCacheKey(source_key, srgb, generate_mips);
  if (!seen_keys.insert(key).second) {
    return;
  }
  refs.push_back(AssimpTextureImportRef{
      .key = std::move(key),
      .raw_key = raw_key,
      .resolved_path = resolved_path,
      .label = label ? label : "assimpTexture",
      .srgb = srgb,
      .embedded = embedded,
  });
}

void collectAssimpMaterialTextureRefs(const aiMaterial& material,
                                      const std::string& model_key,
                                      const std::filesystem::path& base_dir,
                                      bool generate_mips,
                                      std::vector<AssimpTextureImportRef>& refs,
                                      std::unordered_set<std::string>& seen_keys) {
  aiString tex_path;
  aiTextureMapping mapping = aiTextureMapping_UV;
  unsigned int uv_index = 0;
  float blend = 1.0f;
  aiTextureOp op = aiTextureOp_Multiply;
  aiTextureMapMode mapmode[2] = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};

  auto reset_query = [&]() {
    tex_path.Clear();
    mapping = aiTextureMapping_UV;
    uv_index = 0;
    blend = 1.0f;
    op = aiTextureOp_Multiply;
    mapmode[0] = aiTextureMapMode_Wrap;
    mapmode[1] = aiTextureMapMode_Wrap;
  };
  auto collect_texture = [&](aiTextureType type,
                             unsigned int texture_index,
                             bool srgb,
                             const char* label) {
    reset_query();
    if (material.GetTexture(type, texture_index, &tex_path,
                            &mapping, &uv_index, &blend, &op, mapmode) != AI_SUCCESS) {
      return false;
    }
    appendAssimpTextureRef(refs,
                          seen_keys,
                          model_key,
                          base_dir,
                          tex_path,
                          srgb,
                          generate_mips,
                          label);
    return true;
  };

  if (!collect_texture(aiTextureType_BASE_COLOR, 0, true, "baseColor")) {
    collect_texture(aiTextureType_DIFFUSE, 0, true, "baseColor");
  }
  collect_texture(aiTextureType_NORMALS, 0, false, "normal");
  if (!collect_texture(aiTextureType_METALNESS, 0, false, "metallicRoughness")) {
    collect_texture(aiTextureType_DIFFUSE_ROUGHNESS, 0, false, "metallicRoughness");
  }
  if (!collect_texture(aiTextureType_AMBIENT_OCCLUSION, 0, false, "occlusion")) {
    collect_texture(aiTextureType_LIGHTMAP, 0, false, "occlusion");
  }
  collect_texture(aiTextureType_EMISSIVE, 0, true, "emissive");
  collect_texture(aiTextureType_CLEARCOAT, 0, false, "clearcoat");
  collect_texture(aiTextureType_CLEARCOAT, 1, false, "clearcoatRoughness");
  collect_texture(aiTextureType_CLEARCOAT, 2, false, "clearcoatNormal");
  collect_texture(aiTextureType_SHEEN, 0, true, "sheenColor");
  collect_texture(aiTextureType_SHEEN, 1, false, "sheenRoughness");
  collect_texture(aiTextureType_TRANSMISSION, 0, false, "transmission");
  collect_texture(aiTextureType_TRANSMISSION, 1, false, "thickness");
}

MaterialPipelineKind pipelineKind(std::string_view name) {
  if (name == "energy_shell") {
    return MaterialPipelineKind::EnergyShell;
  }
  if (name == "wave_volume") {
    return MaterialPipelineKind::WaveVolume;
  }
  if (name == "sphere_halo") {
    return MaterialPipelineKind::SphereHalo;
  }
  if (name == "screen_wave") {
    return MaterialPipelineKind::ScreenWave;
  }
  if (name == "sphere_glow_volume") {
    return MaterialPipelineKind::SphereGlowVolume;
  }
  if (name == "volumetric_solid") {
    return MaterialPipelineKind::VolumetricSolid;
  }
  if (name == "foliage") {
    return MaterialPipelineKind::Foliage;
  }
  return MaterialPipelineKind::Standard;
}

const float* parameterFloat(
    const std::unordered_map<std::string, rendering::MaterialParameterValue>& params,
    std::string_view name) {
  const auto it = params.find(std::string(name));
  if (it == params.end()) {
    return nullptr;
  }
  return std::get_if<float>(&it->second);
}

const bool* parameterBool(
    const std::unordered_map<std::string, rendering::MaterialParameterValue>& params,
    std::string_view name) {
  const auto it = params.find(std::string(name));
  if (it == params.end()) {
    return nullptr;
  }
  return std::get_if<bool>(&it->second);
}

const glm::vec3* parameterVec3(
    const std::unordered_map<std::string, rendering::MaterialParameterValue>& params,
    std::string_view name) {
  const auto it = params.find(std::string(name));
  if (it == params.end()) {
    return nullptr;
  }
  return std::get_if<glm::vec3>(&it->second);
}

uint32_t parameterUint(
    const std::unordered_map<std::string, rendering::MaterialParameterValue>& params,
    std::string_view name,
    uint32_t fallback) {
  const auto it = params.find(std::string(name));
  if (it == params.end()) {
    return fallback;
  }
  if (const auto* value = std::get_if<uint32_t>(&it->second)) {
    return *value;
  }
  if (const auto* value = std::get_if<int32_t>(&it->second); value != nullptr && *value >= 0) {
    return static_cast<uint32_t>(*value);
  }
  if (const auto* value = std::get_if<float>(&it->second); value != nullptr && *value >= 0.0f) {
    return static_cast<uint32_t>(*value);
  }
  return fallback;
}

}  // namespace

void DiligentBackend::initializeTextureCoordTransforms(MaterialRecord& record) const {
  for (size_t i = 0; i < MaterialRecord::kTextureCoordSlotCount; ++i) {
    record.texcoord_row0[i] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    record.texcoord_row1[i] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
  }
}

void DiligentBackend::setTextureCoordTransform(MaterialRecord& record,
                                               const aiMaterial& material,
                                               unsigned int texture_type,
                                               unsigned int texture_index,
                                               unsigned int uv_index,
                                               size_t slot) const {
  if (slot >= MaterialRecord::kTextureCoordSlotCount) {
    return;
  }

  const auto type = static_cast<aiTextureType>(texture_type);
  aiUVTransform transform;
  transform.mTranslation = aiVector2D(0.0f, 0.0f);
  transform.mScaling = aiVector2D(1.0f, 1.0f);
  transform.mRotation = 0.0f;
  material.Get(AI_MATKEY_UVTRANSFORM(type, texture_index), transform);

  const float sx = transform.mScaling.x;
  const float sy = transform.mScaling.y;
  const float c = std::cos(transform.mRotation);
  const float s = std::sin(transform.mRotation);
  const float tx = transform.mTranslation.x;
  const float ty = transform.mTranslation.y;

  record.texcoord_row0[slot] =
      glm::vec4(c * sx, -s * sy, -0.5f * c + 0.5f * s + 0.5f + tx,
                uv_index > 0u ? 1.0f : 0.0f);
  record.texcoord_row1[slot] =
      glm::vec4(s * sx, c * sy, -0.5f * s - 0.5f * c + 0.5f + ty, 0.0f);
}

void DiligentBackend::bindShadowResourcesToSrb(Diligent::IShaderResourceBinding* srb) const {
  if (!srb) {
    return;
  }

  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ShadowMap")) {
    if (shadow_map_srv_) {
      var->Set(shadow_map_srv_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PointShadowMap")) {
    if (point_shadow_map_srv_ || shadow_map_srv_) {
      var->Set(point_shadow_map_srv_ ? point_shadow_map_srv_ : shadow_map_srv_,
               Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ShadowSampler")) {
    if (shadow_sampler_) {
      var->Set(shadow_sampler_, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
  }
}

void DiligentBackend::bindForwardPlusResourcesToSrb(Diligent::IShaderResourceBinding* srb) const {
  Diligent::IBufferView* light_srv = active_forward_plus_light_srv_
                                         ? active_forward_plus_light_srv_
                                         : forward_plus_light_srv_.RawPtr();
  Diligent::IBufferView* tile_count_srv = active_forward_plus_tile_count_srv_
                                              ? active_forward_plus_tile_count_srv_
                                              : forward_plus_tile_count_srv_.RawPtr();
  Diligent::IBufferView* tile_index_srv = active_forward_plus_tile_index_srv_
                                              ? active_forward_plus_tile_index_srv_
                                              : forward_plus_tile_index_srv_.RawPtr();
  if (!srb || !light_srv || !tile_count_srv || !tile_index_srv) {
    return;
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusLights")) {
    var->Set(light_srv);
  }
  if (auto* var =
          srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusTileLightCounts")) {
    var->Set(tile_count_srv);
  }
  if (auto* var =
          srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ForwardPlusTileLightIndices")) {
    var->Set(tile_index_srv);
  }
}

void DiligentBackend::initializeMaterialBindingForPipeline(
    MaterialRecord& record,
    Diligent::IPipelineState* pso,
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb) {
  srb.Release();
  if (!pso) {
    return;
  }

  const auto srb_start = core::SteadyClock::now();
  pso->CreateShaderResourceBinding(&srb, true);
  recordResourceCreation("material_bindings",
                         "material forward SRB",
                         srb_start,
                         core::SteadyClock::now());
  if (!srb) {
    return;
  }

  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
    var->Set(sampler_color_);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerClamp")) {
    var->Set(sampler_color_clamp_);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
    var->Set(sampler_data_);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BaseColorTex")) {
    var->Set(record.base_color_srv);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_NormalTex")) {
    var->Set(record.normal_srv);
  }
  if (auto* var =
          srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_MetallicRoughnessTex")) {
    var->Set(record.metallic_roughness_srv);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_OcclusionTex")) {
    var->Set(record.occlusion_srv);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_EmissiveTex")) {
    var->Set(record.emissive_srv);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ClearcoatTex")) {
    var->Set(record.clearcoat_srv);
  }
  if (auto* var =
          srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ClearcoatRoughnessTex")) {
    var->Set(record.clearcoat_roughness_srv);
  }
  if (auto* var =
          srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ClearcoatNormalTex")) {
    var->Set(record.clearcoat_normal_srv);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SheenColorTex")) {
    var->Set(record.sheen_color_srv);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SheenRoughnessTex")) {
    var->Set(record.sheen_roughness_srv);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_TransmissionTex")) {
    var->Set(record.transmission_srv);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ThicknessTex")) {
    var->Set(record.thickness_srv);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex")) {
    var->Set(env_irradiance_srv_ ? env_irradiance_srv_ : default_env_);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex")) {
    var->Set(env_prefilter_srv_ ? env_prefilter_srv_ : default_env_);
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT")) {
    if (Diligent::ITextureView* srv = brdfLutSrv()) {
      var->Set(srv);
    }
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneColor")) {
    var->Set(default_base_color_);
  }
  ensureParticleFallbackDepthResource();
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth")) {
    var->Set(particle_fallback_depth_srv_);
  }
  bindForwardPlusResourcesToSrb(srb);
  bindShadowResourcesToSrb(srb);
}

void DiligentBackend::initializeMaterialBindings(MaterialRecord& record) {
  const auto total_start = core::SteadyClock::now();
  auto stage_start = total_start;
  auto mark_stage = [&](const char* stage) {
    const auto stage_end = core::SteadyClock::now();
    logRenderResourceDiag("material_bindings", stage, stage_start, stage_end);
    stage_start = stage_end;
  };

  if (!record.base_color_srv) {
    record.base_color_srv = default_base_color_;
  }
  if (!record.normal_srv) {
    record.normal_srv = default_normal_;
  }
  if (!record.metallic_roughness_srv) {
    record.metallic_roughness_srv = default_metallic_roughness_;
  }
  if (!record.occlusion_srv) {
    record.occlusion_srv = default_occlusion_;
  }
  if (!record.emissive_srv) {
    record.emissive_srv = default_emissive_;
  }
  if (!record.clearcoat_srv) {
    record.clearcoat_srv = default_base_color_;
  }
  if (!record.clearcoat_roughness_srv) {
    record.clearcoat_roughness_srv = default_base_color_;
  }
  if (!record.clearcoat_normal_srv) {
    record.clearcoat_normal_srv = default_normal_;
  }
  if (!record.sheen_color_srv) {
    record.sheen_color_srv = default_base_color_;
  }
  if (!record.sheen_roughness_srv) {
    record.sheen_roughness_srv = default_base_color_;
  }
  if (!record.transmission_srv) {
    record.transmission_srv = default_base_color_;
  }
  if (!record.thickness_srv) {
    record.thickness_srv = default_base_color_;
  }
  mark_stage("default texture assignment");

  record.srb.Release();
  record.transparent_srb.Release();
  record.transparent_double_sided_srb.Release();
  record.additive_srb.Release();
  record.additive_double_sided_srb.Release();
  record.custom_srb.Release();
  record.custom_transparent_srb.Release();
  record.custom_transparent_double_sided_srb.Release();
  record.custom_additive_srb.Release();
  record.custom_additive_double_sided_srb.Release();
  record.shadow_alpha_srb.Release();
  for (auto& srb : record.layout_srbs) {
    srb.Release();
  }
  for (auto& srb : record.layout_custom_srbs) {
    srb.Release();
  }
  mark_stage("binding invalidation");
  logRenderResourceDiag("material_bindings", "total", total_start, core::SteadyClock::now());
}

void DiligentBackend::replaceMaterialTextureView(Diligent::ITextureView* previous,
                                                 Diligent::ITextureView* replacement) {
  if (previous == nullptr || previous == replacement) {
    return;
  }

  auto replace_in_record = [&](MaterialRecord& record) {
    bool changed = false;
    auto replace = [&](Diligent::RefCntAutoPtr<Diligent::ITextureView>& view) {
      if (view.RawPtr() == previous) {
        view = replacement;
        changed = true;
      }
    };
    replace(record.base_color_srv);
    replace(record.normal_srv);
    replace(record.metallic_roughness_srv);
    replace(record.occlusion_srv);
    replace(record.emissive_srv);
    replace(record.clearcoat_srv);
    replace(record.clearcoat_roughness_srv);
    replace(record.clearcoat_normal_srv);
    replace(record.sheen_color_srv);
    replace(record.sheen_roughness_srv);
    replace(record.transmission_srv);
    replace(record.thickness_srv);
    if (changed) {
      initializeMaterialBindings(record);
    }
  };

  for (auto& [id, record] : materials_) {
    (void)id;
    replace_in_record(record);
  }
  for (auto& [key, entry] : imported_material_templates_) {
    (void)key;
    for (MaterialRecord& record : entry.materials) {
      replace_in_record(record);
    }
  }
  for (auto& [key, record] : imported_payload_material_templates_) {
    (void)key;
    replace_in_record(record);
  }
}

bool DiligentBackend::materialUsesCustomForwardPipeline(const MaterialRecord& material) const {
  return material.pipeline.name == "custom" &&
         !material.pipeline.vertex_shader_path.empty() &&
         !material.pipeline.fragment_shader_path.empty();
}

Diligent::IShaderResourceBinding* DiligentBackend::ensureMaterialForwardSrb(
    MaterialRecord& material,
    ForwardPipelineVariant variant,
    bool custom_pipeline,
    rendering::InstanceGpuLayout layout) {
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>* target = nullptr;
  Diligent::IPipelineState* pso = nullptr;
  const size_t layout_slot = forwardPipelineVariantIndex(variant) * kInstanceGpuLayoutCount +
                             instanceGpuLayoutIndex(layout);

  if (custom_pipeline) {
    pso = ensureCustomForwardPipeline(material, variant, layout);
    if (layout != rendering::InstanceGpuLayout::Matrix4x4Params ||
        variant == ForwardPipelineVariant::OpaqueDoubleSided) {
      target = std::addressof(material.layout_custom_srbs[layout_slot]);
    } else {
      switch (variant) {
        case ForwardPipelineVariant::Opaque:
          target = std::addressof(material.custom_srb);
          break;
        case ForwardPipelineVariant::OpaqueDoubleSided:
          target = std::addressof(material.layout_custom_srbs[layout_slot]);
          break;
        case ForwardPipelineVariant::Transparent:
          target = std::addressof(material.custom_transparent_srb);
          break;
        case ForwardPipelineVariant::TransparentDoubleSided:
          target = std::addressof(material.custom_transparent_double_sided_srb);
          break;
        case ForwardPipelineVariant::Additive:
          target = std::addressof(material.custom_additive_srb);
          break;
        case ForwardPipelineVariant::AdditiveDoubleSided:
          target = std::addressof(material.custom_additive_double_sided_srb);
          break;
        case ForwardPipelineVariant::DepthPrepass:
          return nullptr;
      }
    }
  } else {
    pso = ensureForwardPipeline(variant, layout);
    if (layout != rendering::InstanceGpuLayout::Matrix4x4Params ||
        variant == ForwardPipelineVariant::OpaqueDoubleSided) {
      target = std::addressof(material.layout_srbs[layout_slot]);
    } else {
      switch (variant) {
        case ForwardPipelineVariant::Opaque:
          target = std::addressof(material.srb);
          break;
        case ForwardPipelineVariant::OpaqueDoubleSided:
          target = std::addressof(material.layout_srbs[layout_slot]);
          break;
        case ForwardPipelineVariant::Transparent:
          target = std::addressof(material.transparent_srb);
          break;
        case ForwardPipelineVariant::TransparentDoubleSided:
          target = std::addressof(material.transparent_double_sided_srb);
          break;
        case ForwardPipelineVariant::Additive:
          target = std::addressof(material.additive_srb);
          break;
        case ForwardPipelineVariant::AdditiveDoubleSided:
          target = std::addressof(material.additive_double_sided_srb);
          break;
        case ForwardPipelineVariant::DepthPrepass:
          return nullptr;
      }
    }
  }

  if (target == nullptr || pso == nullptr) {
    return nullptr;
  }
  if (!*target) {
    initializeMaterialBindingForPipeline(material, pso, *target);
  }
  return target->RawPtr();
}

Diligent::IShaderResourceBinding* DiligentBackend::ensureMaterialShadowAlphaSrb(
    MaterialRecord& material) {
  if (!shadow_alpha_pipeline_state_) {
    return nullptr;
  }
  if (!material.shadow_alpha_srb) {
    initializeMaterialBindingForPipeline(material,
                                         shadow_alpha_pipeline_state_.RawPtr(),
                                         material.shadow_alpha_srb);
  }
  return material.shadow_alpha_srb.RawPtr();
}

void DiligentBackend::initializeDefaultMaterialBinding(
    Diligent::IPipelineState* pso,
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& out_srb) {
  out_srb.Release();
  if (!pso) {
    return;
  }

  const auto srb_start = core::SteadyClock::now();
  pso->CreateShaderResourceBinding(&out_srb, true);
  recordResourceCreation("material_bindings",
                         "default material SRB",
                         srb_start,
                         core::SteadyClock::now());
  if (!out_srb) {
    return;
  }

  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
    var->Set(sampler_color_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerClamp")) {
    var->Set(sampler_color_clamp_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
    var->Set(sampler_data_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BaseColorTex")) {
    var->Set(default_base_color_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_NormalTex")) {
    var->Set(default_normal_);
  }
  if (auto* var =
          out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_MetallicRoughnessTex")) {
    var->Set(default_metallic_roughness_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_OcclusionTex")) {
    var->Set(default_occlusion_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_EmissiveTex")) {
    var->Set(default_emissive_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ClearcoatTex")) {
    var->Set(default_base_color_);
  }
  if (auto* var =
          out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ClearcoatRoughnessTex")) {
    var->Set(default_base_color_);
  }
  if (auto* var =
          out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ClearcoatNormalTex")) {
    var->Set(default_normal_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SheenColorTex")) {
    var->Set(default_base_color_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SheenRoughnessTex")) {
    var->Set(default_base_color_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_TransmissionTex")) {
    var->Set(default_base_color_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ThicknessTex")) {
    var->Set(default_base_color_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex")) {
    var->Set(env_irradiance_srv_ ? env_irradiance_srv_ : default_env_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex")) {
    var->Set(env_prefilter_srv_ ? env_prefilter_srv_ : default_env_);
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT")) {
    if (Diligent::ITextureView* srv = brdfLutSrv()) {
      var->Set(srv);
    }
  }
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneColor")) {
    var->Set(default_base_color_);
  }
  ensureParticleFallbackDepthResource();
  if (auto* var = out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth")) {
    var->Set(particle_fallback_depth_srv_);
  }
  bindForwardPlusResourcesToSrb(out_srb);
  bindShadowResourcesToSrb(out_srb);
}

void DiligentBackend::preloadAssimpTextures(const aiScene& scene,
                                            const std::filesystem::path& asset_path) {
  const auto total_start = core::SteadyClock::now();
  auto stage_start = total_start;
  const std::filesystem::path base_dir = asset_path.parent_path();
  const std::string model_key = asset_path.string();
  std::vector<AssimpTextureImportRef> refs;
  std::unordered_set<std::string> seen_keys;
  refs.reserve(scene.mNumMaterials * 4u);
  seen_keys.reserve(scene.mNumMaterials * 4u);

  for (unsigned int material_index = 0; material_index < scene.mNumMaterials; ++material_index) {
    if (scene.mMaterials[material_index] == nullptr) {
      continue;
    }
    collectAssimpMaterialTextureRefs(*scene.mMaterials[material_index],
                                     model_key,
                                     base_dir,
                                     generate_mips_enabled_,
                                     refs,
                                     seen_keys);
  }
  logRenderTextureImportDiag("assimp_texture_preload",
                             "collect refs",
                             stage_start,
                             core::SteadyClock::now());

  auto is_cached = [&](const std::string& key) {
    auto cache_it = texture_cache_.find(key);
    return cache_it != texture_cache_.end() &&
           textures_.find(cache_it->second) != textures_.end();
  };

  struct DecodeJob {
    size_t ref_index = 0;
    std::future<LoadedImage> image;
  };
  std::vector<LoadedImage> decoded_images(refs.size());
  std::vector<bool> decoded(refs.size(), false);
  std::vector<DecodeJob> decode_jobs;
  decode_jobs.reserve(refs.size());

  stage_start = core::SteadyClock::now();
  for (size_t ref_index = 0; ref_index < refs.size(); ++ref_index) {
    const auto& ref = refs[ref_index];
    if (!ref.embedded || is_cached(ref.key)) {
      continue;
    }
    const int texture_index = embeddedTextureIndex(ref.raw_key);
    if (texture_index < 0 || texture_index >= static_cast<int>(scene.mNumTextures)) {
      continue;
    }
    const aiTexture* embedded = scene.mTextures[texture_index];
    if (embedded == nullptr) {
      continue;
    }
    decode_jobs.push_back(DecodeJob{
        .ref_index = ref_index,
        .image = std::async(std::launch::async, [embedded]() {
          return decodeEmbeddedAssimpTexture(*embedded);
        }),
    });
  }
  logRenderTextureImportDiag("assimp_texture_preload",
                             "dispatch embedded decode",
                             stage_start,
                             core::SteadyClock::now());

  stage_start = core::SteadyClock::now();
  for (auto& job : decode_jobs) {
    decoded_images[job.ref_index] = job.image.get();
    decoded[job.ref_index] = true;
  }
  logRenderTextureImportDiag("assimp_texture_preload",
                             "embedded decode",
                             stage_start,
                             core::SteadyClock::now());

  stage_start = core::SteadyClock::now();
  size_t uploaded_count = 0;
  for (size_t ref_index = 0; ref_index < refs.size(); ++ref_index) {
    const auto& ref = refs[ref_index];
    if (is_cached(ref.key)) {
      continue;
    }

    LoadedImage image;
    if (ref.embedded) {
      if (!decoded[ref_index]) {
        const int texture_index = embeddedTextureIndex(ref.raw_key);
        if (texture_index >= 0 && texture_index < static_cast<int>(scene.mNumTextures) &&
            scene.mTextures[texture_index] != nullptr) {
          image = decodeEmbeddedAssimpTexture(*scene.mTextures[texture_index]);
        }
      } else {
        image = std::move(decoded_images[ref_index]);
      }
    } else {
      const auto file_decode_start = core::SteadyClock::now();
      image = loadImageFromFile(ref.resolved_path);
      logRenderTextureImportDiag("assimp_texture_preload",
                                 "file decode",
                                 file_decode_start,
                                 core::SteadyClock::now());
    }

    if (image.pixels.empty()) {
      continue;
    }

    TextureRecord record{};
    record.srv = createTextureSRV(image.pixels.data(),
                                  image.width,
                                  image.height,
                                  ref.srgb,
                                  generate_mips_enabled_,
                                  ref.label.c_str(),
                                  record.texture);
    if (!record.srv) {
      continue;
    }
    record.desc = rendering::TextureDesc{
        .width = image.width,
        .height = image.height,
        .format = rendering::TextureFormat::RGBA8,
        .srgb = ref.srgb,
        .generate_mips = generate_mips_enabled_,
        .mip_levels = 1u,
    };
    const rendering::TextureId id = allocateTextureId();
    if (id == rendering::kInvalidTexture) {
      continue;
    }
    textures_[id] = std::move(record);
    texture_cache_[ref.key] = id;
    uploaded_count += 1u;
  }
  logRenderTextureImportDiag("assimp_texture_preload",
                             "upload",
                             stage_start,
                             core::SteadyClock::now());
  if (renderTextureImportDiagnosticsEnabled()) {
    spdlog::info("Render texture import diag: area=assimp_texture_preload stage=summary refs={} decoded={} uploaded={}",
                 refs.size(),
                 decode_jobs.size(),
                 uploaded_count);
  }
  logRenderTextureImportDiag("assimp_texture_preload",
                             "total",
                             total_start,
                             core::SteadyClock::now());
}

Diligent::RefCntAutoPtr<Diligent::ITextureView> DiligentBackend::loadImportedMaterialTexture(
    const rendering::ImportedMaterialTexture& texture) {
  const auto total_start = core::SteadyClock::now();
  if (texture.source_key.empty()) {
    return {};
  }

  const std::string cache_key = makeMaterialTextureCacheKey(
      texture.source_key, texture.srgb, generate_mips_enabled_);
  auto cache_it = texture_cache_.find(cache_key);
  if (cache_it != texture_cache_.end()) {
    auto tex_it = textures_.find(cache_it->second);
    if (tex_it != textures_.end()) {
      logRenderResourceDiag("imported_material_texture",
                            "cache hit",
                            total_start,
                            core::SteadyClock::now());
      return tex_it->second.srv;
    }
  }

  LoadedImage image{};
  auto stage_start = core::SteadyClock::now();
  if (texture.embedded) {
    image = decodeImportedTextureBytes(texture);
    logRenderResourceDiag("imported_material_texture",
                          "embedded decode",
                          stage_start,
                          core::SteadyClock::now());
  } else {
    image = loadImageFromFile(texture.resolved_path);
    logRenderResourceDiag("imported_material_texture",
                          "file decode",
                          stage_start,
                          core::SteadyClock::now());
  }

  if (image.pixels.empty()) {
    logRenderResourceDiag("imported_material_texture",
                          "total",
                          total_start,
                          core::SteadyClock::now());
    return {};
  }

  stage_start = core::SteadyClock::now();
  TextureRecord record{};
  record.srv = createTextureSRV(image.pixels.data(),
                                image.width,
                                image.height,
                                texture.srgb,
                                generate_mips_enabled_,
                                texture.label.c_str(),
                                record.texture);
  logRenderResourceDiag("imported_material_texture",
                        "gpu upload",
                        stage_start,
                        core::SteadyClock::now());
  if (!record.srv) {
    logRenderResourceDiag("imported_material_texture",
                          "total",
                          total_start,
                          core::SteadyClock::now());
    return {};
  }
  record.desc = rendering::TextureDesc{
      .width = image.width,
      .height = image.height,
      .format = rendering::TextureFormat::RGBA8,
      .srgb = texture.srgb,
      .generate_mips = generate_mips_enabled_,
      .mip_levels = 1u,
  };
  const rendering::TextureId id = allocateTextureId();
  if (id == rendering::kInvalidTexture) {
    return {};
  }
  Diligent::RefCntAutoPtr<Diligent::ITextureView> srv = record.srv;
  textures_[id] = std::move(record);
  texture_cache_[cache_key] = id;
  logRenderResourceDiag("imported_material_texture",
                        "total",
                        total_start,
                        core::SteadyClock::now());
  return srv;
}

void DiligentBackend::preloadImportedMaterialTextures(
    const rendering::ImportedMaterialData& material) {
  const auto total_start = core::SteadyClock::now();
  auto stage_start = total_start;

  std::vector<const rendering::ImportedMaterialTexture*> refs;
  std::unordered_set<std::string> seen_keys;
  refs.reserve(material.textures.size());
  seen_keys.reserve(material.textures.size());
  for (const auto& texture : material.textures) {
    const std::string cache_key = makeMaterialTextureCacheKey(
        texture.source_key, texture.srgb, generate_mips_enabled_);
    if (texture.source_key.empty() || !seen_keys.insert(cache_key).second) {
      continue;
    }
    refs.push_back(&texture);
  }
  logRenderTextureImportDiag("imported_payload_texture_preload",
                             "collect refs",
                             stage_start,
                             core::SteadyClock::now());

  auto is_cached = [&](const rendering::ImportedMaterialTexture& texture) {
    const std::string key = makeMaterialTextureCacheKey(
        texture.source_key, texture.srgb, generate_mips_enabled_);
    auto cache_it = texture_cache_.find(key);
    return cache_it != texture_cache_.end() &&
           textures_.find(cache_it->second) != textures_.end();
  };

  struct DecodeJob {
    size_t ref_index = 0;
    std::future<LoadedImage> image;
  };
  std::vector<LoadedImage> decoded_images(refs.size());
  std::vector<bool> decoded(refs.size(), false);
  std::vector<DecodeJob> decode_jobs;
  decode_jobs.reserve(refs.size());

  stage_start = core::SteadyClock::now();
  for (size_t ref_index = 0; ref_index < refs.size(); ++ref_index) {
    const rendering::ImportedMaterialTexture* texture = refs[ref_index];
    if (texture == nullptr || !texture->embedded || texture->source_bytes.empty() ||
        is_cached(*texture)) {
      continue;
    }
    decode_jobs.push_back(DecodeJob{
        .ref_index = ref_index,
        .image = std::async(std::launch::async, [texture]() {
          return decodeImportedTextureBytes(*texture);
        }),
    });
  }
  logRenderTextureImportDiag("imported_payload_texture_preload",
                             "dispatch embedded decode",
                             stage_start,
                             core::SteadyClock::now());

  stage_start = core::SteadyClock::now();
  for (auto& job : decode_jobs) {
    decoded_images[job.ref_index] = job.image.get();
    decoded[job.ref_index] = true;
  }
  logRenderTextureImportDiag("imported_payload_texture_preload",
                             "embedded decode",
                             stage_start,
                             core::SteadyClock::now());

  stage_start = core::SteadyClock::now();
  size_t uploaded_count = 0u;
  for (size_t ref_index = 0; ref_index < refs.size(); ++ref_index) {
    const auto& texture = *refs[ref_index];
    if (is_cached(texture)) {
      continue;
    }

    LoadedImage image{};
    if (decoded[ref_index]) {
      image = std::move(decoded_images[ref_index]);
    } else if (texture.embedded) {
      image = decodeImportedTextureBytes(texture);
    } else {
      const auto file_decode_start = core::SteadyClock::now();
      image = loadImageFromFile(texture.resolved_path);
      logRenderTextureImportDiag("imported_payload_texture_preload",
                                 "file decode",
                                 file_decode_start,
                                 core::SteadyClock::now());
    }

    if (image.pixels.empty()) {
      continue;
    }

    TextureRecord record{};
    record.srv = createTextureSRV(image.pixels.data(),
                                  image.width,
                                  image.height,
                                  texture.srgb,
                                  generate_mips_enabled_,
                                  texture.label.c_str(),
                                  record.texture);
    if (!record.srv) {
      continue;
    }
    record.desc = rendering::TextureDesc{
        .width = image.width,
        .height = image.height,
        .format = rendering::TextureFormat::RGBA8,
        .srgb = texture.srgb,
        .generate_mips = generate_mips_enabled_,
        .mip_levels = 1u,
    };
    const rendering::TextureId id = allocateTextureId();
    if (id == rendering::kInvalidTexture) {
      continue;
    }
    textures_[id] = std::move(record);
    texture_cache_[makeMaterialTextureCacheKey(
        texture.source_key, texture.srgb, generate_mips_enabled_)] = id;
    uploaded_count += 1u;
  }
  logRenderTextureImportDiag("imported_payload_texture_preload",
                             "upload",
                             stage_start,
                             core::SteadyClock::now());
  if (renderTextureImportDiagnosticsEnabled()) {
    spdlog::info(
        "Render texture import diag: area=imported_payload_texture_preload "
        "stage=summary refs={} decoded={} uploaded={}",
        refs.size(),
        decode_jobs.size(),
        uploaded_count);
  }
  logRenderTextureImportDiag("imported_payload_texture_preload",
                             "total",
                             total_start,
                             core::SteadyClock::now());
}

DiligentBackend::MaterialRecord DiligentBackend::buildImportedMaterialRecord(
    const rendering::ImportedMaterialData& material) {
  const auto total_start = core::SteadyClock::now();
  MaterialRecord record{};
  initializeTextureCoordTransforms(record);
  record.desc = material.material;
  record.base_color_factor = glm::vec4(record.desc.base_color.r,
                                       record.desc.base_color.g,
                                       record.desc.base_color.b,
                                       record.desc.base_color.a);
  record.emissive_factor = glm::vec3(record.desc.emissive_color.r,
                                     record.desc.emissive_color.g,
                                     record.desc.emissive_color.b);
  record.metallic_factor = record.desc.metallic;
  record.roughness_factor = record.desc.roughness;
  record.normal_scale = record.desc.normal_scale;
  record.occlusion_strength = record.desc.occlusion_strength;
  record.emissive_strength = record.desc.emissive_strength;
  record.emissive_factor *= record.emissive_strength;
  record.clearcoat_factor = record.desc.clearcoat;
  record.clearcoat_roughness_factor = record.desc.clearcoat_roughness;
  record.sheen_color_factor = glm::vec3(record.desc.sheen_color.r,
                                        record.desc.sheen_color.g,
                                        record.desc.sheen_color.b);
  record.sheen_roughness_factor = record.desc.sheen_roughness;
  record.anisotropy_factor = record.desc.anisotropy;
  record.transmission_factor = record.desc.transmission;
  record.ior = record.desc.ior;
  record.thickness_factor = record.desc.thickness;
  record.attenuation_distance = record.desc.attenuation_distance;
  record.attenuation_color = glm::vec3(record.desc.attenuation_color.r,
                                       record.desc.attenuation_color.g,
                                       record.desc.attenuation_color.b);
  record.analytic_sphere_normals = record.desc.analytic_sphere_normals;
  record.blend_mode = record.desc.blend_mode;

  const size_t texcoord_count =
      std::min<size_t>(MaterialRecord::kTextureCoordSlotCount,
                       rendering::kImportedMaterialTextureCoordSlotCount);
  for (size_t i = 0; i < texcoord_count; ++i) {
    record.texcoord_row0[i] = material.texcoord_row0[i];
    record.texcoord_row1[i] = material.texcoord_row1[i];
  }

  for (const auto& texture : material.textures) {
    Diligent::RefCntAutoPtr<Diligent::ITextureView> srv = loadImportedMaterialTexture(texture);
    if (!srv) {
      continue;
    }
    switch (texture.semantic) {
      case rendering::ImportedMaterialTextureSemantic::BaseColor:
        record.base_color_srv = srv;
        break;
      case rendering::ImportedMaterialTextureSemantic::Normal:
        record.normal_srv = srv;
        break;
      case rendering::ImportedMaterialTextureSemantic::MetallicRoughness:
        record.metallic_roughness_srv = srv;
        break;
      case rendering::ImportedMaterialTextureSemantic::Occlusion:
        record.occlusion_srv = srv;
        break;
      case rendering::ImportedMaterialTextureSemantic::Emissive:
        record.emissive_srv = srv;
        break;
      case rendering::ImportedMaterialTextureSemantic::Clearcoat:
        record.clearcoat_srv = srv;
        break;
      case rendering::ImportedMaterialTextureSemantic::ClearcoatRoughness:
        record.clearcoat_roughness_srv = srv;
        break;
      case rendering::ImportedMaterialTextureSemantic::ClearcoatNormal:
        record.clearcoat_normal_srv = srv;
        break;
      case rendering::ImportedMaterialTextureSemantic::SheenColor:
        record.sheen_color_srv = srv;
        break;
      case rendering::ImportedMaterialTextureSemantic::SheenRoughness:
        record.sheen_roughness_srv = srv;
        break;
      case rendering::ImportedMaterialTextureSemantic::Transmission:
        record.transmission_srv = srv;
        break;
      case rendering::ImportedMaterialTextureSemantic::Thickness:
        record.thickness_srv = srv;
        break;
    }
  }

  logRenderResourceDiag("imported_material_payload",
                        "build record total",
                        total_start,
                        core::SteadyClock::now());
  return record;
}

DiligentBackend::MaterialRecord DiligentBackend::buildImportedMaterialRecord(
    const aiScene& scene,
    const aiMaterial& material,
    const std::filesystem::path& asset_path) {
  const auto total_start = core::SteadyClock::now();
  MaterialRecord record{};
  initializeTextureCoordTransforms(record);
  record.desc = buildImportedMaterialDesc(material);
  record.base_color_factor = glm::vec4(record.desc.base_color.r,
                                       record.desc.base_color.g,
                                       record.desc.base_color.b,
                                       record.desc.base_color.a);

  aiColor3D emissive(0.0f, 0.0f, 0.0f);
  if (material.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
    record.emissive_factor = glm::vec3(emissive.r, emissive.g, emissive.b);
    record.desc.emissive_color = {emissive.r, emissive.g, emissive.b, 1.0f};
  }
  float emissive_strength = 1.0f;
  if (material.Get(AI_MATKEY_EMISSIVE_INTENSITY, emissive_strength) == AI_SUCCESS) {
    record.emissive_strength = emissive_strength;
    record.desc.emissive_strength = emissive_strength;
  }
  record.emissive_factor *= record.emissive_strength;

  float metallic = 1.0f;
  if (material.Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
    record.metallic_factor = metallic;
    record.desc.metallic = metallic;
  }
  float roughness = 1.0f;
  if (material.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
    record.roughness_factor = roughness;
    record.desc.roughness = roughness;
  }
  float normal_scale = 1.0f;
  if (material.Get(AI_MATKEY_TEXBLEND_NORMALS(0), normal_scale) == AI_SUCCESS) {
    record.normal_scale = normal_scale;
    record.desc.normal_scale = normal_scale;
  }
  float occlusion_strength = 1.0f;
  if (material.Get(AI_MATKEY_TEXBLEND(aiTextureType_AMBIENT_OCCLUSION, 0), occlusion_strength) ==
          AI_SUCCESS ||
      material.Get(AI_MATKEY_TEXBLEND_LIGHTMAP(0), occlusion_strength) == AI_SUCCESS) {
    record.occlusion_strength = occlusion_strength;
    record.desc.occlusion_strength = occlusion_strength;
  }

  float clearcoat = 0.0f;
  if (material.Get(AI_MATKEY_CLEARCOAT_FACTOR, clearcoat) == AI_SUCCESS) {
    record.clearcoat_factor = clearcoat;
    record.desc.clearcoat = clearcoat;
  }
  float clearcoat_roughness = 0.0f;
  if (material.Get(AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, clearcoat_roughness) == AI_SUCCESS) {
    record.clearcoat_roughness_factor = clearcoat_roughness;
    record.desc.clearcoat_roughness = clearcoat_roughness;
  }
  aiColor3D sheen_color(0.0f, 0.0f, 0.0f);
  if (material.Get(AI_MATKEY_SHEEN_COLOR_FACTOR, sheen_color) == AI_SUCCESS) {
    record.sheen_color_factor = glm::vec3(sheen_color.r, sheen_color.g, sheen_color.b);
    record.desc.sheen_color = {sheen_color.r, sheen_color.g, sheen_color.b, 1.0f};
  }
  float sheen_roughness = 0.0f;
  if (material.Get(AI_MATKEY_SHEEN_ROUGHNESS_FACTOR, sheen_roughness) == AI_SUCCESS) {
    record.sheen_roughness_factor = sheen_roughness;
    record.desc.sheen_roughness = sheen_roughness;
  }
  float anisotropy = 0.0f;
  if (material.Get(AI_MATKEY_ANISOTROPY_FACTOR, anisotropy) == AI_SUCCESS) {
    record.anisotropy_factor = anisotropy;
    record.desc.anisotropy = anisotropy;
  }
  float transmission = 0.0f;
  if (material.Get(AI_MATKEY_TRANSMISSION_FACTOR, transmission) == AI_SUCCESS) {
    record.transmission_factor = transmission;
    record.desc.transmission = transmission;
    if (transmission > 0.001f) {
      record.desc.transparent = true;
      record.desc.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
    }
  }
  float ior = 1.5f;
  if (material.Get(AI_MATKEY_REFRACTI, ior) == AI_SUCCESS) {
    record.ior = ior;
    record.desc.ior = ior;
  }
  float thickness = 0.0f;
  if (material.Get(AI_MATKEY_VOLUME_THICKNESS_FACTOR, thickness) == AI_SUCCESS) {
    record.thickness_factor = thickness;
    record.desc.thickness = thickness;
  }
  float attenuation_distance = std::numeric_limits<float>::infinity();
  if (material.Get(AI_MATKEY_VOLUME_ATTENUATION_DISTANCE, attenuation_distance) == AI_SUCCESS) {
    record.attenuation_distance = attenuation_distance;
    record.desc.attenuation_distance = attenuation_distance;
  }
  aiColor3D attenuation_color(1.0f, 1.0f, 1.0f);
  if (material.Get(AI_MATKEY_VOLUME_ATTENUATION_COLOR, attenuation_color) == AI_SUCCESS) {
    record.attenuation_color =
        glm::vec3(attenuation_color.r, attenuation_color.g, attenuation_color.b);
    record.desc.attenuation_color =
        {attenuation_color.r, attenuation_color.g, attenuation_color.b, 1.0f};
  }

  const std::filesystem::path base_dir = asset_path.parent_path();
  const std::string model_key = asset_path.string();
  aiString tex_path;
  aiTextureMapping mapping = aiTextureMapping_UV;
  unsigned int uv_index = 0;
  float blend = 1.0f;
  aiTextureOp op = aiTextureOp_Multiply;
  aiTextureMapMode mapmode[2] = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};

  if (material.GetTexture(aiTextureType_BASE_COLOR, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.base_color_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, true, "baseColor");
    setTextureCoordTransform(record, material, aiTextureType_BASE_COLOR, 0, uv_index,
                             kTexCoordBaseColor);
  } else if (material.GetTexture(aiTextureType_DIFFUSE, 0, &tex_path,
                                 &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.base_color_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, true, "baseColor");
    setTextureCoordTransform(record, material, aiTextureType_DIFFUSE, 0, uv_index,
                             kTexCoordBaseColor);
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_NORMALS, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.normal_srv = loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "normal");
    setTextureCoordTransform(record, material, aiTextureType_NORMALS, 0, uv_index,
                             kTexCoordNormal);
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_METALNESS, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.metallic_roughness_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "metallicRoughness");
    setTextureCoordTransform(record, material, aiTextureType_METALNESS, 0, uv_index,
                             kTexCoordMetallicRoughness);
  } else if (material.GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &tex_path,
                                 &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.metallic_roughness_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "metallicRoughness");
    setTextureCoordTransform(record, material, aiTextureType_DIFFUSE_ROUGHNESS, 0, uv_index,
                             kTexCoordMetallicRoughness);
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.occlusion_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "occlusion");
    setTextureCoordTransform(record, material, aiTextureType_AMBIENT_OCCLUSION, 0, uv_index,
                             kTexCoordOcclusion);
  } else if (material.GetTexture(aiTextureType_LIGHTMAP, 0, &tex_path,
                                 &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.occlusion_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "occlusion");
    setTextureCoordTransform(record, material, aiTextureType_LIGHTMAP, 0, uv_index,
                             kTexCoordOcclusion);
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_EMISSIVE, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.emissive_srv = loadTextureFromAssimp(scene, model_key, base_dir, tex_path, true, "emissive");
    setTextureCoordTransform(record, material, aiTextureType_EMISSIVE, 0, uv_index,
                             kTexCoordEmissive);
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_CLEARCOAT, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.clearcoat_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "clearcoat");
    setTextureCoordTransform(record, material, aiTextureType_CLEARCOAT, 0, uv_index,
                             kTexCoordClearcoat);
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_CLEARCOAT, 1, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.clearcoat_roughness_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "clearcoatRoughness");
    setTextureCoordTransform(record, material, aiTextureType_CLEARCOAT, 1, uv_index,
                             kTexCoordClearcoatRoughness);
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_CLEARCOAT, 2, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.clearcoat_normal_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "clearcoatNormal");
    setTextureCoordTransform(record, material, aiTextureType_CLEARCOAT, 2, uv_index,
                             kTexCoordClearcoatNormal);
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_SHEEN, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.sheen_color_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, true, "sheenColor");
    setTextureCoordTransform(record, material, aiTextureType_SHEEN, 0, uv_index,
                             kTexCoordSheenColor);
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_SHEEN, 1, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.sheen_roughness_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "sheenRoughness");
    setTextureCoordTransform(record, material, aiTextureType_SHEEN, 1, uv_index,
                             kTexCoordSheenRoughness);
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_TRANSMISSION, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.transmission_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "transmission");
    setTextureCoordTransform(record, material, aiTextureType_TRANSMISSION, 0, uv_index,
                             kTexCoordTransmission);
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_TRANSMISSION, 1, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.thickness_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "thickness");
    setTextureCoordTransform(record, material, aiTextureType_TRANSMISSION, 1, uv_index,
                             kTexCoordThickness);
  }

  logRenderResourceDiag("imported_material", "build record total", total_start, core::SteadyClock::now());
  return record;
}

const DiligentBackend::ImportedMaterialTemplateCacheEntry* DiligentBackend::getImportedMaterialTemplates(
    const std::filesystem::path& path) {
  const auto total_start = core::SteadyClock::now();
  const std::string cache_key = path.string();
  auto cache_it = imported_material_templates_.find(cache_key);
  if (cache_it != imported_material_templates_.end()) {
    logRenderResourceDiag("imported_material_templates", "cache hit", total_start, core::SteadyClock::now());
    return &cache_it->second;
  }

  ImportedMaterialTemplateCacheEntry entry{};
  Assimp::Importer importer;
  auto stage_start = total_start;
  const aiScene* scene = importer.ReadFile(path.string(), 0);
  auto stage_end = core::SteadyClock::now();
  logRenderResourceDiag("imported_material_templates", "assimp import", stage_start, stage_end);
  if (!scene) {
    auto [it, _] = imported_material_templates_.emplace(cache_key, std::move(entry));
    logRenderResourceDiag("imported_material_templates", "total", total_start, core::SteadyClock::now());
    return &it->second;
  }

  stage_start = stage_end;
  preloadAssimpTextures(*scene, path);
  stage_end = core::SteadyClock::now();
  logRenderResourceDiag("imported_material_templates", "preload textures", stage_start, stage_end);

  stage_start = stage_end;
  entry.materials.reserve(scene->mNumMaterials);
  for (unsigned int material_index = 0; material_index < scene->mNumMaterials; ++material_index) {
    if (scene->mMaterials[material_index] != nullptr) {
      MaterialRecord record =
          buildImportedMaterialRecord(*scene, *scene->mMaterials[material_index], path);
      initializeMaterialBindings(record);
      entry.materials.push_back(std::move(record));
    } else {
      MaterialRecord fallback{};
      initializeTextureCoordTransforms(fallback);
      fallback.desc = rendering::MaterialDesc{};
      initializeMaterialBindings(fallback);
      entry.materials.push_back(std::move(fallback));
    }
  }
  stage_end = core::SteadyClock::now();
  logRenderResourceDiag("imported_material_templates", "build records", stage_start, stage_end);

  auto [it, _] = imported_material_templates_.emplace(cache_key, std::move(entry));
  logRenderResourceDiag("imported_material_templates", "total", total_start, core::SteadyClock::now());
  return &it->second;
}

void DiligentBackend::applyResolvedMaterial(
    MaterialRecord& record,
    const rendering::ResolvedMaterialDesc& resolved) {
  const rendering::MaterialDesc& material = resolved.surface;
  record.pipeline = resolved.pipeline;
  record.desc = material;
  if (record.desc.transparent &&
      record.desc.alpha_mode == rendering::MaterialDesc::AlphaMode::Opaque) {
    record.desc.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
  }
  record.base_color_factor = glm::vec4(material.base_color.r,
                                       material.base_color.g,
                                       material.base_color.b,
                                       material.base_color.a);
  record.emissive_factor = glm::vec3(material.emissive_color.r,
                                     material.emissive_color.g,
                                     material.emissive_color.b);
  record.metallic_factor = material.metallic;
  record.roughness_factor = material.roughness;
  record.normal_scale = material.normal_scale;
  record.occlusion_strength = material.occlusion_strength;
  record.emissive_strength = material.emissive_strength;
  record.emissive_factor *= record.emissive_strength;
  record.clearcoat_factor = material.clearcoat;
  record.clearcoat_roughness_factor = material.clearcoat_roughness;
  record.sheen_color_factor =
      glm::vec3(material.sheen_color.r, material.sheen_color.g, material.sheen_color.b);
  record.sheen_roughness_factor = material.sheen_roughness;
  record.anisotropy_factor = material.anisotropy;
  record.transmission_factor = material.transmission;
  record.ior = material.ior;
  record.thickness_factor = material.thickness;
  record.attenuation_distance = material.attenuation_distance;
  record.attenuation_color = glm::vec3(material.attenuation_color.r,
                                       material.attenuation_color.g,
                                       material.attenuation_color.b);
  record.shading_model = pipelineKind(resolved.pipeline.name);
  record.analytic_sphere_normals = material.analytic_sphere_normals;
  if (const float* value = parameterFloat(resolved.params, "shell_fresnel_power")) {
    record.shell_fresnel_power = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_fresnel_strength")) {
    record.shell_fresnel_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_refraction_strength")) {
    record.shell_refraction_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_interior_strength")) {
    record.shell_interior_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_highlight_strength")) {
    record.shell_highlight_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_alpha_boost")) {
    record.shell_alpha_boost = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_swirl_strength")) {
    record.shell_swirl_strength = *value;
  }
  if (const bool* value = parameterBool(resolved.params, "analytic_sphere_normals")) {
    record.analytic_sphere_normals = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_body_strength")) {
    record.shell_body_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "screen_center_x")) {
    record.screen_center_x = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "screen_center_y")) {
    record.screen_center_y = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "screen_radius_x")) {
    record.screen_radius_x = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "screen_radius_y")) {
    record.screen_radius_y = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "wave_tint_strength")) {
    record.wave_tint_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "wave_distortion_strength")) {
    record.wave_distortion_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "wave_edge_strength")) {
    record.wave_edge_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "wave_noise_strength")) {
    record.wave_noise_strength = *value;
  }
  if (const glm::vec3* value = parameterVec3(resolved.params, "volume_center")) {
    record.volume_center = *value;
  }
  if (const glm::vec3* value = parameterVec3(resolved.params, "volume_axis_x")) {
    record.volume_axis_x = *value;
  }
  if (const glm::vec3* value = parameterVec3(resolved.params, "volume_axis_y")) {
    record.volume_axis_y = *value;
  }
  if (const glm::vec3* value = parameterVec3(resolved.params, "volume_axis_z")) {
    record.volume_axis_z = *value;
  }
  record.volume_shape = parameterUint(resolved.params, "volume_shape", record.volume_shape);
  if (const float* value = parameterFloat(resolved.params, "volume_radius")) {
    record.volume_radius = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_capsule_half_length")) {
    record.volume_capsule_half_length = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_density")) {
    record.volume_density = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_scattering")) {
    record.volume_scattering = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_anisotropy")) {
    record.volume_anisotropy = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_absorption")) {
    record.volume_absorption = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_distortion_strength")) {
    record.volume_distortion_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_noise_strength")) {
    record.volume_noise_strength = *value;
  }
  auto assign_custom_material_param =
      [&](size_t index, const rendering::MaterialParameterValue& value) {
    glm::vec4 parsed{0.0f};
    if (const auto* f = std::get_if<float>(&value)) {
      parsed.x = *f;
    } else if (const auto* i = std::get_if<int32_t>(&value)) {
      parsed.x = static_cast<float>(*i);
    } else if (const auto* u = std::get_if<uint32_t>(&value)) {
      parsed.x = static_cast<float>(*u);
    } else if (const auto* b = std::get_if<bool>(&value)) {
      parsed.x = *b ? 1.0f : 0.0f;
    } else if (const auto* color = std::get_if<rendering::Color>(&value)) {
      parsed = glm::vec4(color->r, color->g, color->b, color->a);
    } else if (const auto* v = std::get_if<glm::vec2>(&value)) {
      parsed = glm::vec4(*v, 0.0f, 0.0f);
    } else if (const auto* v = std::get_if<glm::vec3>(&value)) {
      parsed = glm::vec4(*v, 0.0f);
    } else if (const auto* v = std::get_if<glm::vec4>(&value)) {
      parsed = *v;
    } else {
      return;
    }
    if (index < record.custom_material_params.size()) {
      record.custom_material_params[index] = parsed;
      record.custom_material_param_enabled[index] = true;
    }
  };
  for (size_t index = 0; index < record.custom_material_params.size(); ++index) {
    const std::string key = "material_params" + std::to_string(index);
    if (const auto param_it = resolved.params.find(key); param_it != resolved.params.end()) {
      assign_custom_material_param(index, param_it->second);
    }
  }
  record.blend_mode = record.desc.blend_mode;
  auto assign_texture_handle =
      [&](std::initializer_list<const char*> keys,
          Diligent::RefCntAutoPtr<Diligent::ITextureView>& target) {
    for (const char* key : keys) {
      const auto texture_it = resolved.texture_handles.find(key);
      if (texture_it == resolved.texture_handles.end()) {
        continue;
      }
      const auto renderer_texture_it = textures_.find(texture_it->second);
      if (renderer_texture_it != textures_.end() && renderer_texture_it->second.srv) {
        target = renderer_texture_it->second.srv;
      }
      return;
    }
  };
  assign_texture_handle({"base_color", "baseColor", "albedo", "diffuse"},
                        record.base_color_srv);
  assign_texture_handle({"normal", "normal_map", "normalMap"}, record.normal_srv);
  assign_texture_handle({"metallic_roughness", "metallicRoughness"},
                        record.metallic_roughness_srv);
  assign_texture_handle({"occlusion", "ao"}, record.occlusion_srv);
  assign_texture_handle({"emissive"}, record.emissive_srv);
  assign_texture_handle({"clearcoat"}, record.clearcoat_srv);
  assign_texture_handle({"clearcoat_roughness", "clearcoatRoughness"},
                        record.clearcoat_roughness_srv);
  assign_texture_handle({"clearcoat_normal", "clearcoatNormal"},
                        record.clearcoat_normal_srv);
  assign_texture_handle({"sheen_color", "sheenColor"}, record.sheen_color_srv);
  assign_texture_handle({"sheen_roughness", "sheenRoughness"},
                        record.sheen_roughness_srv);
  assign_texture_handle({"transmission"}, record.transmission_srv);
  assign_texture_handle({"thickness"}, record.thickness_srv);
  initializeMaterialBindings(record);
}

rendering::MaterialId DiligentBackend::createMaterial(const rendering::ResolvedMaterialDesc& resolved) {
  if (resolved.imported_material &&
      resolved.material_asset_index != std::numeric_limits<uint32_t>::max()) {
    const rendering::MaterialId imported =
        createMaterialFromImportedPayload(resolved.material_asset_path,
                                          resolved.material_asset_index,
                                          *resolved.imported_material);
    if (imported != rendering::kInvalidMaterial) {
      if (auto it = materials_.find(imported); it != materials_.end()) {
        applyResolvedMaterial(it->second, resolved);
      }
      return imported;
    }
  }
  if (!resolved.material_asset_path.empty() &&
      resolved.material_asset_index != std::numeric_limits<uint32_t>::max()) {
    const rendering::MaterialId imported =
        createMaterialFromAsset(resolved.material_asset_path, resolved.material_asset_index);
    if (imported != rendering::kInvalidMaterial) {
      if (auto it = materials_.find(imported); it != materials_.end()) {
        applyResolvedMaterial(it->second, resolved);
      }
      return imported;
    }
  }

  rendering::MaterialDesc material = resolved.surface;

  const rendering::MaterialId id = allocateMaterialId();
  if (id == rendering::kInvalidMaterial) {
    return rendering::kInvalidMaterial;
  }
  MaterialRecord record{};
  initializeTextureCoordTransforms(record);
  record.pipeline = resolved.pipeline;
  record.desc = material;
  if (record.desc.transparent &&
      record.desc.alpha_mode == rendering::MaterialDesc::AlphaMode::Opaque) {
    record.desc.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
  }
  record.base_color_factor = glm::vec4(material.base_color.r,
                                       material.base_color.g,
                                       material.base_color.b,
                                       material.base_color.a);
  record.emissive_factor = glm::vec3(material.emissive_color.r,
                                     material.emissive_color.g,
                                     material.emissive_color.b);
  record.metallic_factor = material.metallic;
  record.roughness_factor = material.roughness;
  record.normal_scale = material.normal_scale;
  record.occlusion_strength = material.occlusion_strength;
  record.emissive_strength = material.emissive_strength;
  record.emissive_factor *= record.emissive_strength;
  record.clearcoat_factor = material.clearcoat;
  record.clearcoat_roughness_factor = material.clearcoat_roughness;
  record.sheen_color_factor =
      glm::vec3(material.sheen_color.r, material.sheen_color.g, material.sheen_color.b);
  record.sheen_roughness_factor = material.sheen_roughness;
  record.anisotropy_factor = material.anisotropy;
  record.transmission_factor = material.transmission;
  record.ior = material.ior;
  record.thickness_factor = material.thickness;
  record.attenuation_distance = material.attenuation_distance;
  record.attenuation_color = glm::vec3(material.attenuation_color.r,
                                       material.attenuation_color.g,
                                       material.attenuation_color.b);
  record.shading_model = pipelineKind(resolved.pipeline.name);
  record.analytic_sphere_normals = material.analytic_sphere_normals;
  if (const float* value = parameterFloat(resolved.params, "shell_fresnel_power")) {
    record.shell_fresnel_power = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_fresnel_strength")) {
    record.shell_fresnel_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_refraction_strength")) {
    record.shell_refraction_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_interior_strength")) {
    record.shell_interior_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_highlight_strength")) {
    record.shell_highlight_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_alpha_boost")) {
    record.shell_alpha_boost = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_swirl_strength")) {
    record.shell_swirl_strength = *value;
  }
  if (const bool* value = parameterBool(resolved.params, "analytic_sphere_normals")) {
    record.analytic_sphere_normals = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "shell_body_strength")) {
    record.shell_body_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "screen_center_x")) {
    record.screen_center_x = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "screen_center_y")) {
    record.screen_center_y = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "screen_radius_x")) {
    record.screen_radius_x = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "screen_radius_y")) {
    record.screen_radius_y = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "wave_tint_strength")) {
    record.wave_tint_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "wave_distortion_strength")) {
    record.wave_distortion_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "wave_edge_strength")) {
    record.wave_edge_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "wave_noise_strength")) {
    record.wave_noise_strength = *value;
  }
  if (const glm::vec3* value = parameterVec3(resolved.params, "volume_center")) {
    record.volume_center = *value;
  }
  if (const glm::vec3* value = parameterVec3(resolved.params, "volume_axis_x")) {
    record.volume_axis_x = *value;
  }
  if (const glm::vec3* value = parameterVec3(resolved.params, "volume_axis_y")) {
    record.volume_axis_y = *value;
  }
  if (const glm::vec3* value = parameterVec3(resolved.params, "volume_axis_z")) {
    record.volume_axis_z = *value;
  }
  record.volume_shape = parameterUint(resolved.params, "volume_shape", record.volume_shape);
  if (const float* value = parameterFloat(resolved.params, "volume_radius")) {
    record.volume_radius = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_capsule_half_length")) {
    record.volume_capsule_half_length = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_density")) {
    record.volume_density = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_scattering")) {
    record.volume_scattering = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_anisotropy")) {
    record.volume_anisotropy = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_absorption")) {
    record.volume_absorption = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_distortion_strength")) {
    record.volume_distortion_strength = *value;
  }
  if (const float* value = parameterFloat(resolved.params, "volume_noise_strength")) {
    record.volume_noise_strength = *value;
  }
  auto assign_custom_material_param =
      [&](size_t index, const rendering::MaterialParameterValue& value) {
    glm::vec4 parsed{0.0f};
    if (const auto* f = std::get_if<float>(&value)) {
      parsed.x = *f;
    } else if (const auto* i = std::get_if<int32_t>(&value)) {
      parsed.x = static_cast<float>(*i);
    } else if (const auto* u = std::get_if<uint32_t>(&value)) {
      parsed.x = static_cast<float>(*u);
    } else if (const auto* b = std::get_if<bool>(&value)) {
      parsed.x = *b ? 1.0f : 0.0f;
    } else if (const auto* color = std::get_if<rendering::Color>(&value)) {
      parsed = glm::vec4(color->r, color->g, color->b, color->a);
    } else if (const auto* v = std::get_if<glm::vec2>(&value)) {
      parsed = glm::vec4(*v, 0.0f, 0.0f);
    } else if (const auto* v = std::get_if<glm::vec3>(&value)) {
      parsed = glm::vec4(*v, 0.0f);
    } else if (const auto* v = std::get_if<glm::vec4>(&value)) {
      parsed = *v;
    } else {
      return;
    }
    if (index < record.custom_material_params.size()) {
      record.custom_material_params[index] = parsed;
      record.custom_material_param_enabled[index] = true;
    }
  };
  for (size_t index = 0; index < record.custom_material_params.size(); ++index) {
    const std::string key = "material_params" + std::to_string(index);
    if (const auto param_it = resolved.params.find(key); param_it != resolved.params.end()) {
      assign_custom_material_param(index, param_it->second);
    }
  }
  record.blend_mode = record.desc.blend_mode;
  auto assign_texture_handle =
      [&](std::initializer_list<const char*> keys,
          Diligent::RefCntAutoPtr<Diligent::ITextureView>& target) {
    for (const char* key : keys) {
      const auto texture_it = resolved.texture_handles.find(key);
      if (texture_it == resolved.texture_handles.end()) {
        continue;
      }
      const auto renderer_texture_it = textures_.find(texture_it->second);
      if (renderer_texture_it != textures_.end() && renderer_texture_it->second.srv) {
        target = renderer_texture_it->second.srv;
      }
      return;
    }
  };
  assign_texture_handle({"base_color", "baseColor", "albedo", "diffuse"},
                        record.base_color_srv);
  assign_texture_handle({"normal", "normal_map", "normalMap"}, record.normal_srv);
  assign_texture_handle({"metallic_roughness", "metallicRoughness"},
                        record.metallic_roughness_srv);
  assign_texture_handle({"occlusion", "ao"}, record.occlusion_srv);
  assign_texture_handle({"emissive"}, record.emissive_srv);
  assign_texture_handle({"clearcoat"}, record.clearcoat_srv);
  assign_texture_handle({"clearcoat_roughness", "clearcoatRoughness"},
                        record.clearcoat_roughness_srv);
  assign_texture_handle({"clearcoat_normal", "clearcoatNormal"},
                        record.clearcoat_normal_srv);
  assign_texture_handle({"sheen_color", "sheenColor"}, record.sheen_color_srv);
  assign_texture_handle({"sheen_roughness", "sheenRoughness"},
                        record.sheen_roughness_srv);
  assign_texture_handle({"transmission"}, record.transmission_srv);
  assign_texture_handle({"thickness"}, record.thickness_srv);
  if (materialUsesCustomForwardPipeline(record)) {
    const bool transparent =
        record.desc.transparent ||
        record.desc.alpha_mode == rendering::MaterialDesc::AlphaMode::Blend;
    const bool additive =
        transparent && record.desc.blend_mode == rendering::MaterialDesc::BlendMode::Additive;
    const bool double_sided = record.desc.double_sided;
    ForwardPipelineVariant variant = ForwardPipelineVariant::Opaque;
    if (transparent) {
      if (additive) {
        variant = double_sided ? ForwardPipelineVariant::AdditiveDoubleSided
                               : ForwardPipelineVariant::Additive;
      } else {
        variant = double_sided ? ForwardPipelineVariant::TransparentDoubleSided
                               : ForwardPipelineVariant::Transparent;
      }
    } else if (double_sided) {
      variant = ForwardPipelineVariant::OpaqueDoubleSided;
    }
    if (ensureCustomForwardPipeline(record,
                                    variant,
                                    rendering::InstanceGpuLayout::Matrix4x4Params) == nullptr) {
      return rendering::kInvalidMaterial;
    }
  }
  initializeMaterialBindings(record);

  materials_[id] = std::move(record);
  return id;
}

rendering::MaterialId DiligentBackend::createMaterialFromAsset(const std::filesystem::path& path,
                                                              uint32_t material_index) {
  const auto total_start = core::SteadyClock::now();
  auto stage_start = total_start;
  const auto* templates = getImportedMaterialTemplates(path);
  auto stage_end = core::SteadyClock::now();
  logRenderResourceDiag("material_from_asset", "template lookup", stage_start, stage_end);
  if (!templates || material_index >= templates->materials.size()) {
    logRenderResourceDiag("material_from_asset", "total", total_start, core::SteadyClock::now());
    return rendering::kInvalidMaterial;
  }

  stage_start = stage_end;
  const rendering::MaterialId id = allocateMaterialId();
  if (id == rendering::kInvalidMaterial) {
    return rendering::kInvalidMaterial;
  }
  MaterialRecord record = templates->materials[material_index];
  initializeMaterialBindings(record);
  stage_end = core::SteadyClock::now();
  logRenderResourceDiag("material_from_asset", "clone", stage_start, stage_end);
  materials_[id] = std::move(record);
  logRenderResourceDiag("material_from_asset", "total", total_start, core::SteadyClock::now());
  return id;
}

rendering::MaterialId DiligentBackend::createMaterialFromImportedPayload(
    const std::filesystem::path& path,
    uint32_t material_index,
    const rendering::ImportedMaterialData& imported) {
  const auto total_start = core::SteadyClock::now();
  auto stage_start = total_start;
  std::string cache_key = path.string();
  cache_key.append("#material=");
  cache_key.append(std::to_string(material_index));

  auto template_it = imported_payload_material_templates_.find(cache_key);
  auto stage_end = core::SteadyClock::now();
  logRenderResourceDiag("material_from_imported_payload",
                        "template lookup",
                        stage_start,
                        stage_end);

  if (template_it == imported_payload_material_templates_.end()) {
    stage_start = stage_end;
    preloadImportedMaterialTextures(imported);
    stage_end = core::SteadyClock::now();
    logRenderResourceDiag("material_from_imported_payload",
                          "preload textures",
                          stage_start,
                          stage_end);

    stage_start = stage_end;
    MaterialRecord template_record = buildImportedMaterialRecord(imported);
    initializeMaterialBindings(template_record);
    stage_end = core::SteadyClock::now();
    logRenderResourceDiag("material_from_imported_payload",
                          "build template",
                          stage_start,
                          stage_end);

    auto [inserted_it, _] =
        imported_payload_material_templates_.emplace(std::move(cache_key), std::move(template_record));
    template_it = inserted_it;
  }

  if (template_it == imported_payload_material_templates_.end()) {
    logRenderResourceDiag("material_from_imported_payload",
                          "total",
                          total_start,
                          core::SteadyClock::now());
    return rendering::kInvalidMaterial;
  }

  stage_start = core::SteadyClock::now();
  const rendering::MaterialId id = allocateMaterialId();
  if (id == rendering::kInvalidMaterial) {
    return rendering::kInvalidMaterial;
  }
  MaterialRecord record = template_it->second;
  initializeMaterialBindings(record);
  materials_[id] = std::move(record);
  stage_end = core::SteadyClock::now();
  logRenderResourceDiag("material_from_imported_payload", "clone", stage_start, stage_end);
  logRenderResourceDiag("material_from_imported_payload",
                        "total",
                        total_start,
                        core::SteadyClock::now());
  return id;
}

void DiligentBackend::updateMaterial(rendering::MaterialId material,
                                     const rendering::MaterialDesc& desc) {
  auto it = materials_.find(material);
  if (it == materials_.end()) {
    return;
  }

  it->second.desc = desc;
  if (it->second.desc.transparent &&
      it->second.desc.alpha_mode == rendering::MaterialDesc::AlphaMode::Opaque) {
    it->second.desc.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
  }
  it->second.base_color_factor = glm::vec4(desc.base_color.r,
                                           desc.base_color.g,
                                           desc.base_color.b,
                                           desc.base_color.a);
  it->second.emissive_factor = glm::vec3(desc.emissive_color.r,
                                         desc.emissive_color.g,
                                         desc.emissive_color.b);
  it->second.metallic_factor = desc.metallic;
  it->second.roughness_factor = desc.roughness;
  it->second.normal_scale = desc.normal_scale;
  it->second.occlusion_strength = desc.occlusion_strength;
  it->second.emissive_strength = desc.emissive_strength;
  it->second.emissive_factor *= it->second.emissive_strength;
  it->second.clearcoat_factor = desc.clearcoat;
  it->second.clearcoat_roughness_factor = desc.clearcoat_roughness;
  it->second.sheen_color_factor =
      glm::vec3(desc.sheen_color.r, desc.sheen_color.g, desc.sheen_color.b);
  it->second.sheen_roughness_factor = desc.sheen_roughness;
  it->second.anisotropy_factor = desc.anisotropy;
  it->second.transmission_factor = desc.transmission;
  it->second.ior = desc.ior;
  it->second.thickness_factor = desc.thickness;
  it->second.attenuation_distance = desc.attenuation_distance;
  it->second.attenuation_color = glm::vec3(desc.attenuation_color.r,
                                           desc.attenuation_color.g,
                                           desc.attenuation_color.b);
  it->second.analytic_sphere_normals = desc.analytic_sphere_normals;
  it->second.blend_mode = it->second.desc.blend_mode;
  initializeMaterialBindings(it->second);
  directional_shadow_scene_dirty_ = true;
  point_shadow_scene_dirty_ = true;
}

rendering::MaterialId DiligentBackend::allocateMaterialId() noexcept {
  if (nextMaterialId_ == rendering::kInvalidMaterial) {
    return rendering::kInvalidMaterial;
  }
  const rendering::MaterialId id = nextMaterialId_;
  nextMaterialId_ = id == std::numeric_limits<rendering::MaterialId>::max()
                        ? rendering::kInvalidMaterial
                        : id + 1u;
  return id;
}

void DiligentBackend::destroyMaterial(rendering::MaterialId material) {
  if (materials_.erase(material) == 0u) {
    return;
  }
  directional_shadow_scene_dirty_ = true;
  point_shadow_scene_dirty_ = true;
}

void DiligentBackend::setMaterialFloat(rendering::MaterialId material,
                                       std::string_view /*name*/,
                                       float /*value*/) {
  if (materials_.find(material) == materials_.end()) {
    return;
  }
}

}  // namespace karma::rendering::backend
