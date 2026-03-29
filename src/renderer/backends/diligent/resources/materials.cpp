#include "karma/renderer/backends/diligent/backend.hpp"

#include "../backend_internal.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>

namespace karma::renderer_backend {

namespace {
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

void DiligentBackend::bindShadowResourcesToSrb(Diligent::IShaderResourceBinding* srb) const {
  if (!srb) {
    return;
  }

  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ShadowMap")) {
    if (shadow_map_srv_) {
      var->Set(shadow_map_srv_);
    }
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PointShadowMap")) {
    if (point_shadow_map_srv_ || shadow_map_srv_) {
      var->Set(point_shadow_map_srv_ ? point_shadow_map_srv_ : shadow_map_srv_);
    }
  }
  if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_ShadowSampler")) {
    if (shadow_sampler_) {
      var->Set(shadow_sampler_);
    }
  }
}

void DiligentBackend::initializeMaterialBindings(MaterialRecord& record) {
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

  auto initialize_srb = [&](Diligent::IPipelineState* pso,
                            Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb) {
    srb.Release();
    if (!pso) {
      return;
    }

    pso->CreateShaderResourceBinding(&srb, true);
    if (!srb) {
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
  };

  initialize_srb(pipeline_state_.RawPtr(), record.srb);
  initialize_srb(transparent_pipeline_state_.RawPtr(), record.transparent_srb);
  initialize_srb(transparent_double_sided_pipeline_state_.RawPtr(),
                 record.transparent_double_sided_srb);
  initialize_srb(additive_pipeline_state_.RawPtr(), record.additive_srb);
  initialize_srb(additive_double_sided_pipeline_state_.RawPtr(),
                 record.additive_double_sided_srb);
}

DiligentBackend::MaterialRecord DiligentBackend::buildImportedMaterialRecord(
    const aiScene& scene,
    const aiMaterial& material,
    const std::filesystem::path& asset_path) {
  MaterialRecord record{};
  record.desc = buildImportedMaterialDesc(material);
  record.base_color_factor = glm::vec4(record.desc.base_color.r,
                                       record.desc.base_color.g,
                                       record.desc.base_color.b,
                                       record.desc.base_color.a);

  aiColor3D emissive(0.0f, 0.0f, 0.0f);
  if (material.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
    record.emissive_factor = glm::vec3(emissive.r, emissive.g, emissive.b);
  }

  float metallic = 1.0f;
  if (material.Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
    record.metallic_factor = metallic;
  }
  float roughness = 1.0f;
  if (material.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
    record.roughness_factor = roughness;
  }
  float normal_scale = 1.0f;
  if (material.Get(AI_MATKEY_TEXBLEND_NORMALS(0), normal_scale) == AI_SUCCESS) {
    record.normal_scale = normal_scale;
  }
  float occlusion_strength = 1.0f;
  if (material.Get(AI_MATKEY_TEXBLEND(aiTextureType_AMBIENT_OCCLUSION, 0), occlusion_strength) ==
          AI_SUCCESS ||
      material.Get(AI_MATKEY_TEXBLEND_LIGHTMAP(0), occlusion_strength) == AI_SUCCESS) {
    record.occlusion_strength = occlusion_strength;
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
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS ||
      material.GetTexture(aiTextureType_DIFFUSE, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.base_color_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, true, "baseColor");
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_NORMALS, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.normal_srv = loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "normal");
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_METALNESS, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS ||
      material.GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.metallic_roughness_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "metallicRoughness");
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS ||
      material.GetTexture(aiTextureType_LIGHTMAP, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.occlusion_srv =
        loadTextureFromAssimp(scene, model_key, base_dir, tex_path, false, "occlusion");
  }

  mapping = aiTextureMapping_UV;
  uv_index = 0;
  blend = 1.0f;
  if (material.GetTexture(aiTextureType_EMISSIVE, 0, &tex_path,
                          &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
    record.emissive_srv = loadTextureFromAssimp(scene, model_key, base_dir, tex_path, true, "emissive");
  }

  return record;
}

const DiligentBackend::ImportedMaterialTemplateCacheEntry* DiligentBackend::getImportedMaterialTemplates(
    const std::filesystem::path& path) {
  const std::string cache_key = path.string();
  auto cache_it = imported_material_templates_.find(cache_key);
  if (cache_it != imported_material_templates_.end()) {
    return &cache_it->second;
  }

  ImportedMaterialTemplateCacheEntry entry{};
  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(path.string(),
                                           aiProcess_Triangulate |
                                           aiProcess_GenNormals |
                                           aiProcess_CalcTangentSpace |
                                           aiProcess_JoinIdenticalVertices);
  if (!scene) {
    auto [it, _] = imported_material_templates_.emplace(cache_key, std::move(entry));
    return &it->second;
  }

  entry.materials.reserve(scene->mNumMaterials);
  for (unsigned int material_index = 0; material_index < scene->mNumMaterials; ++material_index) {
    if (scene->mMaterials[material_index] != nullptr) {
      entry.materials.push_back(
          buildImportedMaterialRecord(*scene, *scene->mMaterials[material_index], path));
    } else {
      MaterialRecord fallback{};
      fallback.desc = renderer::MaterialDesc{};
      entry.materials.push_back(std::move(fallback));
    }
  }

  auto [it, _] = imported_material_templates_.emplace(cache_key, std::move(entry));
  return &it->second;
}

renderer::MaterialId DiligentBackend::createMaterial(const renderer::MaterialDesc& material) {
  const renderer::MaterialId id = nextMaterialId_++;
  MaterialRecord record{};
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
  record.volume_radius = material.volume_radius;
  record.volume_density = material.volume_density;
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
  const auto* templates = getImportedMaterialTemplates(path);
  if (!templates || material_index >= templates->materials.size()) {
    return renderer::kInvalidMaterial;
  }

  const renderer::MaterialId id = nextMaterialId_++;
  MaterialRecord record = templates->materials[material_index];
  initializeMaterialBindings(record);
  materials_[id] = std::move(record);
  return id;
}

renderer::MaterialSetId DiligentBackend::createMaterialSetFromMesh(
    renderer::MeshId mesh,
    const renderer::MaterialResourceDesc& desc) {
  auto mesh_it = meshes_.find(mesh);
  if (mesh_it == meshes_.end()) {
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
    return renderer::kInvalidMaterialSet;
  }

  const renderer::MaterialSetId set_id = nextMaterialSetId_++;
  material_sets_[set_id] = std::move(set_record);
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
  it->second.volume_radius = desc.volume_radius;
  it->second.volume_density = desc.volume_density;
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
