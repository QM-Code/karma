#include "karma/renderer/backends/diligent/backend.hpp"

#include "backend_internal.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <filesystem>

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>

namespace karma::renderer_backend {

namespace {
void computeBounds(const renderer::MeshData& mesh, glm::vec3& out_center, float& out_radius) {
  if (mesh.vertices.empty()) {
    out_center = glm::vec3(0.0f);
    out_radius = 0.0f;
    return;
  }
  glm::vec3 min_v{std::numeric_limits<float>::max()};
  glm::vec3 max_v{std::numeric_limits<float>::lowest()};
  for (const auto& v : mesh.vertices) {
    min_v = glm::min(min_v, v);
    max_v = glm::max(max_v, v);
  }
  out_center = (min_v + max_v) * 0.5f;
  const glm::vec3 extents = max_v - min_v;
  out_radius = 0.5f * glm::length(extents);
}

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

  record.srb.Release();
  if (!pipeline_state_) {
    return;
  }

  pipeline_state_->CreateShaderResourceBinding(&record.srb, true);
  if (!record.srb) {
    return;
  }

  if (auto* var = record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
    var->Set(sampler_color_);
  }
  if (auto* var = record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
    var->Set(sampler_data_);
  }
  if (auto* var = record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BaseColorTex")) {
    var->Set(record.base_color_srv);
  }
  if (auto* var = record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_NormalTex")) {
    var->Set(record.normal_srv);
  }
  if (auto* var = record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_MetallicRoughnessTex")) {
    var->Set(record.metallic_roughness_srv);
  }
  if (auto* var = record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_OcclusionTex")) {
    var->Set(record.occlusion_srv);
  }
  if (auto* var = record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_EmissiveTex")) {
    var->Set(record.emissive_srv);
  }
  if (auto* var = record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_IrradianceTex")) {
    var->Set(env_irradiance_srv_ ? env_irradiance_srv_ : default_env_);
  }
  if (auto* var = record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_PrefilterTex")) {
    var->Set(env_prefilter_srv_ ? env_prefilter_srv_ : default_env_);
  }
  if (auto* var = record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BRDFLUT")) {
    var->Set(env_brdf_lut_srv_ ? env_brdf_lut_srv_ : default_base_color_);
  }
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

renderer::MeshId DiligentBackend::createMesh(const renderer::MeshData& mesh) {
  const renderer::MeshId id = nextMeshId_++;
  MeshRecord record{};
  record.data = mesh;
  computeBounds(mesh, record.bounds_center, record.bounds_radius);
  record.base_color = glm::vec4(1.0f);

  if (device_ && !mesh.vertices.empty()) {
    const auto interleaved = buildInterleavedVertices(mesh);
    constexpr Diligent::Uint32 kVertexStride = static_cast<Diligent::Uint32>(12 * sizeof(float));
    Diligent::BufferDesc vb_desc{};
    vb_desc.Name = "Karma VB";
    vb_desc.Usage = Diligent::USAGE_IMMUTABLE;
    vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    vb_desc.ElementByteStride = kVertexStride;
    vb_desc.Size = static_cast<Diligent::Uint32>(interleaved.size() * sizeof(float));
    Diligent::BufferData vb_data{interleaved.data(), vb_desc.Size};
    device_->CreateBuffer(vb_desc, &vb_data, &record.vertex_buffer);
    record.vertex_count = static_cast<Diligent::Uint32>(mesh.vertices.size());
  }

  if (device_ && !mesh.indices.empty()) {
    Diligent::BufferDesc ib_desc{};
    ib_desc.Name = "Karma IB";
    ib_desc.Usage = Diligent::USAGE_IMMUTABLE;
    ib_desc.BindFlags = Diligent::BIND_INDEX_BUFFER;
    ib_desc.Size = static_cast<Diligent::Uint32>(mesh.indices.size() * sizeof(uint32_t));
    Diligent::BufferData ib_data{mesh.indices.data(), ib_desc.Size};
    device_->CreateBuffer(ib_desc, &ib_data, &record.index_buffer);
    record.index_count = static_cast<Diligent::Uint32>(mesh.indices.size());
  }

  if (!mesh.indices.empty()) {
    MeshRecord::Submesh submesh{};
    submesh.index_offset = 0;
    submesh.index_count = static_cast<Diligent::Uint32>(mesh.indices.size());
    record.submeshes.push_back(submesh);
  }

  meshes_[id] = std::move(record);
  return id;
}

renderer::MeshId DiligentBackend::createMeshFromFile(const std::filesystem::path& path) {
  const renderer::MeshId id = nextMeshId_++;

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(path.string(),
                                           aiProcess_Triangulate |
                                           aiProcess_GenNormals |
                                           aiProcess_CalcTangentSpace |
                                           aiProcess_JoinIdenticalVertices |
                                           aiProcess_PreTransformVertices);
  if (!scene || !scene->mRootNode) {
    meshes_[id] = MeshRecord{};
    return id;
  }

  glm::vec4 base_color(1.0f);
  std::vector<SubmeshInfo> submesh_infos;
  const auto combined = combineMeshes(*scene, base_color, submesh_infos);
  if (combined.vertices.empty()) {
  } else {
    if (!combined.uvs.empty()) {
      glm::vec2 uv_min = combined.uvs.front();
      glm::vec2 uv_max = combined.uvs.front();
      for (const auto& uv : combined.uvs) {
        uv_min.x = std::min(uv_min.x, uv.x);
        uv_min.y = std::min(uv_min.y, uv.y);
        uv_max.x = std::max(uv_max.x, uv.x);
        uv_max.y = std::max(uv_max.y, uv.y);
      }
    } else {
    }
  }

  MeshRecord record{};
  record.data = combined;
  record.base_color = base_color;
  computeBounds(record.data, record.bounds_center, record.bounds_radius);

  if (device_ && !combined.vertices.empty()) {
    const auto interleaved = buildInterleavedVertices(combined);
    constexpr Diligent::Uint32 kVertexStride = static_cast<Diligent::Uint32>(12 * sizeof(float));
    Diligent::BufferDesc vb_desc{};
    vb_desc.Name = "Karma VB";
    vb_desc.Usage = Diligent::USAGE_IMMUTABLE;
    vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    vb_desc.ElementByteStride = kVertexStride;
    vb_desc.Size = static_cast<Diligent::Uint32>(interleaved.size() * sizeof(float));
    Diligent::BufferData vb_data{interleaved.data(), vb_desc.Size};
    device_->CreateBuffer(vb_desc, &vb_data, &record.vertex_buffer);
    record.vertex_count = static_cast<Diligent::Uint32>(combined.vertices.size());
  }

  if (device_ && !combined.indices.empty()) {
    Diligent::BufferDesc ib_desc{};
    ib_desc.Name = "Karma IB";
    ib_desc.Usage = Diligent::USAGE_IMMUTABLE;
    ib_desc.BindFlags = Diligent::BIND_INDEX_BUFFER;
    ib_desc.Size = static_cast<Diligent::Uint32>(combined.indices.size() * sizeof(uint32_t));
    Diligent::BufferData ib_data{combined.indices.data(), ib_desc.Size};
    device_->CreateBuffer(ib_desc, &ib_data, &record.index_buffer);
    record.index_count = static_cast<Diligent::Uint32>(combined.indices.size());
  }

  std::vector<renderer::MaterialId> material_ids;
  material_ids.resize(scene->mNumMaterials, renderer::kInvalidMaterial);
  for (unsigned int mat_index = 0; mat_index < scene->mNumMaterials; ++mat_index) {
    const aiMaterial* material = scene->mMaterials[mat_index];
    if (!material) {
      continue;
    }

    renderer::MaterialId mat_id = nextMaterialId_++;
    MaterialRecord mat_record = buildImportedMaterialRecord(*scene, *material, path);
    initializeMaterialBindings(mat_record);

    materials_[mat_id] = std::move(mat_record);
    material_ids[mat_index] = mat_id;
    record.owned_materials.push_back(mat_id);
  }

  for (const auto& sub : submesh_infos) {
    MeshRecord::Submesh submesh{};
    submesh.index_offset = sub.index_offset;
    submesh.index_count = sub.index_count;
    if (sub.material_index < material_ids.size()) {
      submesh.material = material_ids[sub.material_index];
    } else {
      submesh.material = renderer::kInvalidMaterial;
    }
    record.submeshes.push_back(submesh);
  }

  meshes_[id] = std::move(record);
  return id;
}

void DiligentBackend::destroyMesh(renderer::MeshId mesh) {
  auto mesh_it = meshes_.find(mesh);
  if (mesh_it == meshes_.end()) {
    return;
  }
  std::vector<renderer::MaterialSetId> owned_sets;
  owned_sets.reserve(material_sets_.size());
  for (const auto& [set_id, set_record] : material_sets_) {
    if (set_record.source_mesh == mesh) {
      owned_sets.push_back(set_id);
    }
  }
  for (renderer::MaterialSetId set_id : owned_sets) {
    destroyMaterialSet(set_id);
  }
  for (const renderer::MaterialId material : mesh_it->second.owned_materials) {
    materials_.erase(material);
  }
  for (auto it = instances_.begin(); it != instances_.end();) {
    if (it->second.mesh == mesh) {
      it = instances_.erase(it);
    } else {
      ++it;
    }
  }
  meshes_.erase(mesh_it);
}

bool DiligentBackend::getMeshBounds(renderer::MeshId mesh, glm::vec3& center, float& radius) const {
  auto mesh_it = meshes_.find(mesh);
  if (mesh_it == meshes_.end()) {
    return false;
  }
  const auto& record = mesh_it->second;
  if (record.data.vertices.empty()) {
    return false;
  }
  center = record.bounds_center;
  radius = record.bounds_radius;
  return true;
}

renderer::MaterialId DiligentBackend::createMaterial(const renderer::MaterialDesc& material) {
  const renderer::MaterialId id = nextMaterialId_++;
  MaterialRecord record{};
  record.desc = material;
  record.base_color_factor = glm::vec4(material.base_color.r,
                                       material.base_color.g,
                                       material.base_color.b,
                                       material.base_color.a);
  record.emissive_factor = glm::vec3(0.0f);
  record.metallic_factor = 1.0f;
  record.roughness_factor = 1.0f;
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

void DiligentBackend::updateMaterial(renderer::MaterialId material, const renderer::MaterialDesc& desc) {
  auto it = materials_.find(material);
  if (it == materials_.end()) {
    return;
  }
  it->second.desc = desc;
  it->second.base_color_factor = glm::vec4(desc.base_color.r,
                                           desc.base_color.g,
                                           desc.base_color.b,
                                           desc.base_color.a);
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

renderer::TextureId DiligentBackend::createTexture(const renderer::TextureDesc& desc) {
  const renderer::TextureId id = nextTextureId_++;
  TextureRecord record{};
  record.desc = desc;
  if (device_ && desc.width > 0 && desc.height > 0) {
    Diligent::TextureDesc tex_desc{};
    tex_desc.Name = "Karma Texture";
    tex_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    tex_desc.Width = static_cast<Diligent::Uint32>(desc.width);
    tex_desc.Height = static_cast<Diligent::Uint32>(desc.height);
    tex_desc.MipLevels = desc.generate_mips ? 0 : 1;
    tex_desc.BindFlags = Diligent::BIND_SHADER_RESOURCE |
                         (desc.generate_mips ? Diligent::BIND_RENDER_TARGET : Diligent::BIND_NONE);
    tex_desc.MiscFlags = desc.generate_mips ? Diligent::MISC_TEXTURE_FLAG_GENERATE_MIPS
                                            : Diligent::MISC_TEXTURE_FLAG_NONE;
    switch (desc.format) {
      case renderer::TextureFormat::RGB8:
        tex_desc.Format = desc.srgb ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB
                                    : Diligent::TEX_FORMAT_RGBA8_UNORM;
        break;
      case renderer::TextureFormat::R8:
        tex_desc.Format = desc.srgb ? Diligent::TEX_FORMAT_R8_UNORM
                                    : Diligent::TEX_FORMAT_R8_UNORM;
        break;
      case renderer::TextureFormat::RGBA8:
      default:
        tex_desc.Format = desc.srgb ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB
                                    : Diligent::TEX_FORMAT_RGBA8_UNORM;
        break;
    }
    device_->CreateTexture(tex_desc, nullptr, &record.texture);
    if (record.texture) {
      auto* raw_view = record.texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
      if (raw_view) {
        Diligent::RefCntAutoPtr<Diligent::ITextureView> view;
        view = raw_view;
        record.srv = view;
        if (desc.generate_mips && context_) {
          context_->GenerateMips(record.srv);
        }
      } else {
      }
    }
  }
  textures_[id] = record;
  return id;
}

void DiligentBackend::destroyTexture(renderer::TextureId texture) {
  textures_.erase(texture);
}

void DiligentBackend::updateTextureRGBA8(renderer::TextureId texture,
                                         int w,
                                         int h,
                                         const void* pixels) {
  if (!device_ || !context_ || texture == renderer::kInvalidTexture || !pixels || w <= 0 || h <= 0) {
    return;
  }
  auto it = textures_.find(texture);
  if (it == textures_.end()) {
    return;
  }

  auto& record = it->second;
  const bool size_changed = record.desc.width != w || record.desc.height != h;
  const bool format_changed = record.desc.format != renderer::TextureFormat::RGBA8;
  if (!record.texture || size_changed || format_changed) {
    record.desc.width = w;
    record.desc.height = h;
    record.desc.format = renderer::TextureFormat::RGBA8;
    record.desc.srgb = false;
    record.desc.generate_mips = false;

    Diligent::TextureDesc tex_desc{};
    tex_desc.Name = "Karma UI Texture";
    tex_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    tex_desc.Width = static_cast<Diligent::Uint32>(w);
    tex_desc.Height = static_cast<Diligent::Uint32>(h);
    tex_desc.MipLevels = 1;
    tex_desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    tex_desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    tex_desc.Usage = Diligent::USAGE_DEFAULT;

    record.texture.Release();
    record.srv.Release();
    Diligent::TextureSubResData subres{};
    subres.pData = pixels;
    subres.Stride = static_cast<Diligent::Uint32>(w * 4);
    Diligent::TextureData init_data{};
    init_data.pSubResources = &subres;
    init_data.NumSubresources = 1;
    device_->CreateTexture(tex_desc, &init_data, &record.texture);
    if (record.texture) {
      auto* raw_view = record.texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
      if (raw_view) {
        Diligent::RefCntAutoPtr<Diligent::ITextureView> view;
        view = raw_view;
        record.srv = view;
      }
    }
    return;
  }

  if (!record.texture) {
    return;
  }

  Diligent::TextureSubResData subres{};
  subres.pData = pixels;
  subres.Stride = static_cast<Diligent::Uint32>(w * 4);

  Diligent::Box box{};
  box.MinX = 0;
  box.MaxX = static_cast<Diligent::Uint32>(w);
  box.MinY = 0;
  box.MaxY = static_cast<Diligent::Uint32>(h);
  box.MinZ = 0;
  box.MaxZ = 1;

  context_->UpdateTexture(record.texture, 0, 0, box, subres,
                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

renderer::RenderTargetId DiligentBackend::createRenderTarget(const renderer::RenderTargetDesc& desc) {
  RenderTargetRecord record{};
  record.desc = desc;
  const int target_width = desc.width > 0 ? desc.width : current_width_;
  const int target_height = desc.height > 0 ? desc.height : current_height_;
  recreateRenderTargetResources(record, target_width, target_height);

  const renderer::RenderTargetId id = nextTargetId_++;
  targets_[id] = std::move(record);
  return id;
}

void DiligentBackend::destroyRenderTarget(renderer::RenderTargetId target) {
  targets_.erase(target);
}

void DiligentBackend::recreateRenderTargetResources(RenderTargetRecord& record, int width, int height) {
  record.color_texture.Release();
  record.color_srv.Release();
  record.color_rtv.Release();
  record.depth_texture.Release();
  record.depth_dsv.Release();
  record.width = 0;
  record.height = 0;

  if (!device_ || width <= 0 || height <= 0) {
    return;
  }

  Diligent::TextureDesc color_desc{};
  color_desc.Name = "Karma Render Target Color";
  color_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  color_desc.Width = static_cast<Diligent::Uint32>(width);
  color_desc.Height = static_cast<Diligent::Uint32>(height);
  color_desc.MipLevels = 1;
  color_desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
  color_desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
  device_->CreateTexture(color_desc, nullptr, &record.color_texture);
  if (!record.color_texture) {
    return;
  }

  record.color_srv = record.color_texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  record.color_rtv = record.color_texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
  if (!record.color_rtv) {
    record.color_texture.Release();
    record.color_srv.Release();
    return;
  }

  if (record.desc.depth) {
    Diligent::TextureDesc depth_desc{};
    depth_desc.Name = "Karma Render Target Depth";
    depth_desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    depth_desc.Width = static_cast<Diligent::Uint32>(width);
    depth_desc.Height = static_cast<Diligent::Uint32>(height);
    depth_desc.MipLevels = 1;
    depth_desc.Format = record.desc.stencil ? Diligent::TEX_FORMAT_D24_UNORM_S8_UINT
                                            : Diligent::TEX_FORMAT_D32_FLOAT;
    depth_desc.BindFlags = Diligent::BIND_DEPTH_STENCIL;
    device_->CreateTexture(depth_desc, nullptr, &record.depth_texture);
    if (record.depth_texture) {
      record.depth_dsv = record.depth_texture->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
    }
  }

  record.width = width;
  record.height = height;
}

}  // namespace karma::renderer_backend
