#include "../backend.hpp"

#include "../backend_internal.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>

#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>

#include <cmath>

namespace karma::renderer_backend {

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

renderer::MaterialDesc buildImportedMaterialDesc(const aiMaterial& material) {
  renderer::MaterialDesc desc{};
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

  desc.transparent = desc.base_color.a < 0.999f;
  return desc;
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

  auto initialize_srb = [&](Diligent::IPipelineState* pso,
                            const char* label,
                            Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb) {
    const auto srb_start = core::SteadyClock::now();
    srb.Release();
    if (!pso) {
      logRenderResourceDiag("material_bindings", label, srb_start, core::SteadyClock::now());
      return;
    }

    pso->CreateShaderResourceBinding(&srb, true);
    if (!srb) {
      logRenderResourceDiag("material_bindings", label, srb_start, core::SteadyClock::now());
      return;
    }

    if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
      var->Set(sampler_color_);
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
    if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_MetallicRoughnessTex")) {
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
      var->Set(env_brdf_lut_srv_ ? env_brdf_lut_srv_ : default_base_color_);
    }
    if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneColor")) {
      var->Set(default_base_color_);
    }
    ensureParticleFallbackDepthResource();
    if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth")) {
      var->Set(particle_fallback_depth_srv_);
    }
    bindShadowResourcesToSrb(srb);
    logRenderResourceDiag("material_bindings", label, srb_start, core::SteadyClock::now());
  };

  initialize_srb(pipeline_state_.RawPtr(), "opaque srb", record.srb);

  // Reflection overlays and mesh-alpha transparency can use the transparent
  // pipeline even when the material itself is opaque, so keep this one hot.
  initialize_srb(transparent_pipeline_state_.RawPtr(), "transparent srb", record.transparent_srb);

  const bool double_sided = record.desc.double_sided;
  const bool additive = record.blend_mode == renderer::MaterialDesc::BlendMode::Additive;
  if (double_sided) {
    initialize_srb(transparent_double_sided_pipeline_state_.RawPtr(),
                   "transparent double-sided srb",
                   record.transparent_double_sided_srb);
  } else {
    record.transparent_double_sided_srb.Release();
  }
  if (additive) {
    initialize_srb(additive_pipeline_state_.RawPtr(), "additive srb", record.additive_srb);
  } else {
    record.additive_srb.Release();
  }
  if (additive && double_sided) {
    initialize_srb(additive_double_sided_pipeline_state_.RawPtr(),
                   "additive double-sided srb",
                   record.additive_double_sided_srb);
  } else {
    record.additive_double_sided_srb.Release();
  }
  logRenderResourceDiag("material_bindings", "total", total_start, core::SteadyClock::now());
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
  entry.materials.reserve(scene->mNumMaterials);
  for (unsigned int material_index = 0; material_index < scene->mNumMaterials; ++material_index) {
    if (scene->mMaterials[material_index] != nullptr) {
      entry.materials.push_back(
          buildImportedMaterialRecord(*scene, *scene->mMaterials[material_index], path));
    } else {
      MaterialRecord fallback{};
      initializeTextureCoordTransforms(fallback);
      fallback.desc = renderer::MaterialDesc{};
      entry.materials.push_back(std::move(fallback));
    }
  }
  stage_end = core::SteadyClock::now();
  logRenderResourceDiag("imported_material_templates", "build records", stage_start, stage_end);

  auto [it, _] = imported_material_templates_.emplace(cache_key, std::move(entry));
  logRenderResourceDiag("imported_material_templates", "total", total_start, core::SteadyClock::now());
  return &it->second;
}

renderer::MaterialId DiligentBackend::createMaterial(const renderer::MaterialDesc& material) {
  const renderer::MaterialId id = nextMaterialId_++;
  MaterialRecord record{};
  initializeTextureCoordTransforms(record);
  record.desc = material;
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
  record.shading_model = material.shading_model;
  record.shell_fresnel_power = material.shell_fresnel_power;
  record.shell_fresnel_strength = material.shell_fresnel_strength;
  record.shell_refraction_strength = material.shell_refraction_strength;
  record.shell_interior_strength = material.shell_interior_strength;
  record.shell_highlight_strength = material.shell_highlight_strength;
  record.shell_alpha_boost = material.shell_alpha_boost;
  record.shell_swirl_strength = material.shell_swirl_strength;
  record.analytic_sphere_normals = material.analytic_sphere_normals;
  record.shell_body_strength = material.shell_body_strength;
  record.screen_center_x = material.screen_center_x;
  record.screen_center_y = material.screen_center_y;
  record.screen_radius_x = material.screen_radius_x;
  record.screen_radius_y = material.screen_radius_y;
  record.wave_tint_strength = material.wave_tint_strength;
  record.wave_distortion_strength = material.wave_distortion_strength;
  record.wave_edge_strength = material.wave_edge_strength;
  record.wave_noise_strength = material.wave_noise_strength;
  record.volume_center = material.volume_center;
  record.volume_axis_x = material.volume_axis_x;
  record.volume_axis_y = material.volume_axis_y;
  record.volume_axis_z = material.volume_axis_z;
  record.volume_shape = material.volume_shape;
  record.volume_radius = material.volume_radius;
  record.volume_capsule_half_length = material.volume_capsule_half_length;
  record.volume_density = material.volume_density;
  record.volume_scattering = material.volume_scattering;
  record.volume_anisotropy = material.volume_anisotropy;
  record.volume_absorption = material.volume_absorption;
  record.volume_distortion_strength = material.volume_distortion_strength;
  record.volume_noise_strength = material.volume_noise_strength;
  record.blend_mode = material.blend_mode;
  if (material.base_color_texture != renderer::kInvalidTexture) {
    auto tex_it = textures_.find(material.base_color_texture);
    if (tex_it != textures_.end()) {
      record.base_color_srv = tex_it->second.srv;
    }
  }
  initializeMaterialBindings(record);

  materials_[id] = std::move(record);
  return id;
}

renderer::MaterialId DiligentBackend::createMaterialFromAsset(const std::filesystem::path& path,
                                                              uint32_t material_index) {
  const auto total_start = core::SteadyClock::now();
  auto stage_start = total_start;
  const auto* templates = getImportedMaterialTemplates(path);
  auto stage_end = core::SteadyClock::now();
  logRenderResourceDiag("material_from_asset", "template lookup", stage_start, stage_end);
  if (!templates || material_index >= templates->materials.size()) {
    logRenderResourceDiag("material_from_asset", "total", total_start, core::SteadyClock::now());
    return renderer::kInvalidMaterial;
  }

  stage_start = stage_end;
  const renderer::MaterialId id = nextMaterialId_++;
  MaterialRecord record = templates->materials[material_index];
  initializeMaterialBindings(record);
  stage_end = core::SteadyClock::now();
  logRenderResourceDiag("material_from_asset", "clone and bindings", stage_start, stage_end);
  materials_[id] = std::move(record);
  logRenderResourceDiag("material_from_asset", "total", total_start, core::SteadyClock::now());
  return id;
}

renderer::MaterialSetId DiligentBackend::createMaterialSetFromMesh(
    renderer::MeshId mesh,
    const renderer::MaterialResourceDesc& desc) {
  const auto total_start = core::SteadyClock::now();
  auto mesh_it = meshes_.find(mesh);
  if (mesh_it == meshes_.end()) {
    logRenderResourceDiag("material_set", "total", total_start, core::SteadyClock::now());
    return renderer::kInvalidMaterialSet;
  }

  const auto& mesh_record = mesh_it->second;
  const glm::vec4 tint(desc.base_color_tint.r,
                       desc.base_color_tint.g,
                       desc.base_color_tint.b,
                       desc.base_color_tint.a);

  auto clone_material = [&](const MaterialRecord& source) -> renderer::MaterialId {
    const renderer::MaterialId id = nextMaterialId_++;
    MaterialRecord clone = source;
    clone.base_color_factor *= tint;
    clone.desc.base_color = math::Color{clone.base_color_factor.r,
                                        clone.base_color_factor.g,
                                        clone.base_color_factor.b,
                                        clone.base_color_factor.a};
    materials_[id] = std::move(clone);
    return id;
  };

  auto create_default_tinted_material = [&]() -> renderer::MaterialId {
    renderer::MaterialDesc material_desc{};
    material_desc.base_color = math::Color{mesh_record.base_color.r * tint.r,
                                           mesh_record.base_color.g * tint.g,
                                           mesh_record.base_color.b * tint.b,
                                           mesh_record.base_color.a * tint.a};
    return createMaterial(material_desc);
  };

  MaterialSetRecord set_record{};
  set_record.source_mesh = mesh;

  switch (desc.kind) {
    case renderer::MaterialResourceDesc::Kind::MeshTint:
      break;
    case renderer::MaterialResourceDesc::Kind::Explicit: {
      const std::size_t material_count =
          mesh_record.submeshes.empty() ? 1u : mesh_record.submeshes.size();
      set_record.materials.reserve(material_count);
      for (std::size_t i = 0; i < material_count; ++i) {
        set_record.materials.push_back(createMaterial(desc.material));
      }
      break;
    }
    case renderer::MaterialResourceDesc::Kind::ImportedAssetMaterial: {
      const std::size_t material_count =
          mesh_record.submeshes.empty() ? 1u : mesh_record.submeshes.size();
      set_record.materials.reserve(material_count);
      for (std::size_t i = 0; i < material_count; ++i) {
        renderer::MaterialId material =
            createMaterialFromAsset(desc.material_asset_path, desc.material_asset_index);
        if (material == renderer::kInvalidMaterial) {
          material = createMaterial(desc.material);
        }
        set_record.materials.push_back(material);
      }
      break;
    }
  }

  if (desc.kind == renderer::MaterialResourceDesc::Kind::Explicit ||
      desc.kind == renderer::MaterialResourceDesc::Kind::ImportedAssetMaterial) {
    if (set_record.materials.empty()) {
      logRenderResourceDiag("material_set", "total", total_start, core::SteadyClock::now());
      return renderer::kInvalidMaterialSet;
    }
    const renderer::MaterialSetId set_id = nextMaterialSetId_++;
    material_sets_[set_id] = std::move(set_record);
    logRenderResourceDiag("material_set", "total", total_start, core::SteadyClock::now());
    return set_id;
  }

  if (!mesh_record.submeshes.empty()) {
    set_record.materials.reserve(mesh_record.submeshes.size());
    for (const auto& submesh : mesh_record.submeshes) {
      renderer::MaterialId material_id = renderer::kInvalidMaterial;
      if (submesh.material != renderer::kInvalidMaterial) {
        auto material_it = materials_.find(submesh.material);
        if (material_it != materials_.end()) {
          material_id = clone_material(material_it->second);
        }
      }
      if (material_id == renderer::kInvalidMaterial) {
        material_id = create_default_tinted_material();
      }
      set_record.materials.push_back(material_id);
    }
  } else {
    set_record.materials.push_back(create_default_tinted_material());
  }

  if (set_record.materials.empty()) {
    logRenderResourceDiag("material_set", "total", total_start, core::SteadyClock::now());
    return renderer::kInvalidMaterialSet;
  }

  const renderer::MaterialSetId set_id = nextMaterialSetId_++;
  material_sets_[set_id] = std::move(set_record);
  logRenderResourceDiag("material_set", "total", total_start, core::SteadyClock::now());
  return set_id;
}

void DiligentBackend::updateMaterial(renderer::MaterialId material,
                                     const renderer::MaterialDesc& desc) {
  auto it = materials_.find(material);
  if (it == materials_.end()) {
    return;
  }

  it->second.desc = desc;
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
  it->second.shading_model = desc.shading_model;
  it->second.shell_fresnel_power = desc.shell_fresnel_power;
  it->second.shell_fresnel_strength = desc.shell_fresnel_strength;
  it->second.shell_refraction_strength = desc.shell_refraction_strength;
  it->second.shell_interior_strength = desc.shell_interior_strength;
  it->second.shell_highlight_strength = desc.shell_highlight_strength;
  it->second.shell_alpha_boost = desc.shell_alpha_boost;
  it->second.shell_swirl_strength = desc.shell_swirl_strength;
  it->second.analytic_sphere_normals = desc.analytic_sphere_normals;
  it->second.shell_body_strength = desc.shell_body_strength;
  it->second.screen_center_x = desc.screen_center_x;
  it->second.screen_center_y = desc.screen_center_y;
  it->second.screen_radius_x = desc.screen_radius_x;
  it->second.screen_radius_y = desc.screen_radius_y;
  it->second.wave_tint_strength = desc.wave_tint_strength;
  it->second.wave_distortion_strength = desc.wave_distortion_strength;
  it->second.wave_edge_strength = desc.wave_edge_strength;
  it->second.wave_noise_strength = desc.wave_noise_strength;
  it->second.volume_center = desc.volume_center;
  it->second.volume_axis_x = desc.volume_axis_x;
  it->second.volume_axis_y = desc.volume_axis_y;
  it->second.volume_axis_z = desc.volume_axis_z;
  it->second.volume_shape = desc.volume_shape;
  it->second.volume_radius = desc.volume_radius;
  it->second.volume_capsule_half_length = desc.volume_capsule_half_length;
  it->second.volume_density = desc.volume_density;
  it->second.volume_scattering = desc.volume_scattering;
  it->second.volume_anisotropy = desc.volume_anisotropy;
  it->second.volume_absorption = desc.volume_absorption;
  it->second.volume_distortion_strength = desc.volume_distortion_strength;
  it->second.volume_noise_strength = desc.volume_noise_strength;
  it->second.blend_mode = desc.blend_mode;
  it->second.base_color_srv = {};
  if (desc.base_color_texture != renderer::kInvalidTexture) {
    auto tex_it = textures_.find(desc.base_color_texture);
    if (tex_it != textures_.end()) {
      it->second.base_color_srv = tex_it->second.srv;
    }
  }
  initializeMaterialBindings(it->second);
}

void DiligentBackend::destroyMaterial(renderer::MaterialId material) {
  materials_.erase(material);
}

void DiligentBackend::destroyMaterialSet(renderer::MaterialSetId set) {
  auto it = material_sets_.find(set);
  if (it == material_sets_.end()) {
    return;
  }
  for (renderer::MaterialId material : it->second.materials) {
    if (material != renderer::kInvalidMaterial) {
      materials_.erase(material);
    }
  }
  material_sets_.erase(it);
}

void DiligentBackend::setMaterialFloat(renderer::MaterialId material,
                                       std::string_view /*name*/,
                                       float /*value*/) {
  if (materials_.find(material) == materials_.end()) {
    return;
  }
}

}  // namespace karma::renderer_backend
