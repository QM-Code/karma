#include "karma/renderer/backends/diligent/backend.hpp"

#include "backend_internal.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>

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
}  // namespace

renderer::MeshId DiligentBackend::createMesh(const renderer::MeshData& mesh) {
  const renderer::MeshId id = nextMeshId_++;
  MeshRecord record{};
  record.data = mesh;
  computeBounds(mesh, record.bounds_center, record.bounds_radius);
  record.base_color = glm::vec4(1.0f);
  spdlog::warn("Karma: Diligent createMesh id={} verts={} indices={}", id, mesh.vertices.size(),
               mesh.indices.size());

  if (device_ && !mesh.vertices.empty()) {
    const auto interleaved = buildInterleavedVertices(mesh);
    Diligent::BufferDesc vb_desc{};
    vb_desc.Name = "Karma VB";
    vb_desc.Usage = Diligent::USAGE_IMMUTABLE;
    vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
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
  spdlog::warn("Karma: Diligent createMeshFromFile id={} path='{}' exists={}",
               id,
               path.string(),
               !path.empty() && std::filesystem::exists(path));
  const aiScene* scene = importer.ReadFile(path.string(),
                                           aiProcess_Triangulate |
                                           aiProcess_GenNormals |
                                           aiProcess_CalcTangentSpace |
                                           aiProcess_JoinIdenticalVertices |
                                           aiProcess_PreTransformVertices);
  if (!scene || !scene->mRootNode) {
    spdlog::error("Karma: Failed to load model at path {} ({})",
                  path.string(),
                  importer.GetErrorString());
    meshes_[id] = MeshRecord{};
    return id;
  }

  glm::vec4 base_color(1.0f);
  std::vector<SubmeshInfo> submesh_infos;
  const auto combined = combineMeshes(*scene, base_color, submesh_infos);
  if (combined.vertices.empty()) {
    spdlog::warn("Karma: Model '{}' has no vertices", path.string());
  } else {
    spdlog::warn("Karma: Model '{}' vertices={}, indices={}",
                 path.string(),
                 combined.vertices.size(),
                 combined.indices.size());
    if (!combined.uvs.empty()) {
      glm::vec2 uv_min = combined.uvs.front();
      glm::vec2 uv_max = combined.uvs.front();
      for (const auto& uv : combined.uvs) {
        uv_min.x = std::min(uv_min.x, uv.x);
        uv_min.y = std::min(uv_min.y, uv.y);
        uv_max.x = std::max(uv_max.x, uv.x);
        uv_max.y = std::max(uv_max.y, uv.y);
      }
      spdlog::warn("Karma: Model '{}' UV range min=({}, {}) max=({}, {})",
                   path.string(),
                   uv_min.x,
                   uv_min.y,
                   uv_max.x,
                   uv_max.y);
    } else {
      spdlog::warn("Karma: Model '{}' has no UVs", path.string());
    }
  }

  MeshRecord record{};
  record.data = combined;
  record.base_color = base_color;
  computeBounds(record.data, record.bounds_center, record.bounds_radius);

  if (device_ && !combined.vertices.empty()) {
    const auto interleaved = buildInterleavedVertices(combined);
    Diligent::BufferDesc vb_desc{};
    vb_desc.Name = "Karma VB";
    vb_desc.Usage = Diligent::USAGE_IMMUTABLE;
    vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
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
  const std::filesystem::path base_dir = path.parent_path();

  for (unsigned int mat_index = 0; mat_index < scene->mNumMaterials; ++mat_index) {
    const aiMaterial* material = scene->mMaterials[mat_index];
    if (!material) {
      continue;
    }

    renderer::MaterialId mat_id = nextMaterialId_++;
    MaterialRecord mat_record{};
    mat_record.base_color_factor = glm::vec4(1.0f);
    mat_record.emissive_factor = glm::vec3(0.0f);
    mat_record.metallic_factor = 1.0f;
    mat_record.roughness_factor = 1.0f;

    aiColor4D base_factor(1.0f, 1.0f, 1.0f, 1.0f);
    if (material->Get(AI_MATKEY_BASE_COLOR, base_factor) == AI_SUCCESS) {
      mat_record.base_color_factor = glm::vec4(base_factor.r, base_factor.g, base_factor.b, base_factor.a);
    } else {
      aiColor3D diffuse(1.0f, 1.0f, 1.0f);
      if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
        mat_record.base_color_factor = glm::vec4(diffuse.r, diffuse.g, diffuse.b, 1.0f);
      }
    }

    aiColor3D emissive(0.0f, 0.0f, 0.0f);
    if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
      mat_record.emissive_factor = glm::vec3(emissive.r, emissive.g, emissive.b);
    }

    float metallic = 1.0f;
    if (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
      mat_record.metallic_factor = metallic;
    }
    float roughness = 1.0f;
    if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
      mat_record.roughness_factor = roughness;
    }
    float normal_scale = 1.0f;
    if (material->Get(AI_MATKEY_TEXBLEND_NORMALS(0), normal_scale) == AI_SUCCESS) {
      mat_record.normal_scale = normal_scale;
    }
    float occlusion_strength = 1.0f;
    if (material->Get(AI_MATKEY_TEXBLEND(aiTextureType_AMBIENT_OCCLUSION, 0), occlusion_strength) == AI_SUCCESS ||
        material->Get(AI_MATKEY_TEXBLEND_LIGHTMAP(0), occlusion_strength) == AI_SUCCESS) {
      mat_record.occlusion_strength = occlusion_strength;
    }

    auto log_texture = [&](aiTextureType type, const char* label, const aiString& tex_path,
                           aiTextureMapping mapping, unsigned int uv_index, float blend) {
      spdlog::warn("Karma: Material {} texture {} path='{}' mapping={} uv={} blend={}",
                   mat_index, label, tex_path.C_Str(),
                   static_cast<int>(mapping), uv_index, blend);
      if (uv_index != 0) {
        spdlog::warn("Karma: Material {} texture {} uses UV channel {} (only UV0 supported)",
                     mat_index, label, uv_index);
      }
      (void)type;
    };

    aiString tex_path;
    aiTextureMapping mapping = aiTextureMapping_UV;
    unsigned int uv_index = 0;
    float blend = 1.0f;
    aiTextureOp op = aiTextureOp_Multiply;
    aiTextureMapMode mapmode[2] = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
    if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &tex_path,
                             &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS ||
        material->GetTexture(aiTextureType_DIFFUSE, 0, &tex_path,
                             &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
      log_texture(aiTextureType_BASE_COLOR, "baseColor", tex_path, mapping, uv_index, blend);
      mat_record.base_color_srv = loadTextureFromAssimp(*scene, path.string(), base_dir, tex_path, true, "baseColor");
    }
    if (!mat_record.base_color_srv) {
      mat_record.base_color_srv = default_base_color_;
    }

    mapping = aiTextureMapping_UV;
    uv_index = 0;
    blend = 1.0f;
    if (material->GetTexture(aiTextureType_NORMALS, 0, &tex_path,
                             &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
      log_texture(aiTextureType_NORMALS, "normal", tex_path, mapping, uv_index, blend);
      mat_record.normal_srv = loadTextureFromAssimp(*scene, path.string(), base_dir, tex_path, false, "normal");
    }
    if (!mat_record.normal_srv) {
      mat_record.normal_srv = default_normal_;
    }

    mapping = aiTextureMapping_UV;
    uv_index = 0;
    blend = 1.0f;
    if (material->GetTexture(aiTextureType_METALNESS, 0, &tex_path,
                             &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS ||
        material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &tex_path,
                             &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
      log_texture(aiTextureType_METALNESS, "metallicRoughness", tex_path, mapping, uv_index, blend);
      mat_record.metallic_roughness_srv =
          loadTextureFromAssimp(*scene, path.string(), base_dir, tex_path, false, "metallicRoughness");
    }
    if (!mat_record.metallic_roughness_srv) {
      mat_record.metallic_roughness_srv = default_metallic_roughness_;
    }

    mapping = aiTextureMapping_UV;
    uv_index = 0;
    blend = 1.0f;
    if (material->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &tex_path,
                             &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS ||
        material->GetTexture(aiTextureType_LIGHTMAP, 0, &tex_path,
                             &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
      log_texture(aiTextureType_AMBIENT_OCCLUSION, "occlusion", tex_path, mapping, uv_index, blend);
      mat_record.occlusion_srv =
          loadTextureFromAssimp(*scene, path.string(), base_dir, tex_path, false, "occlusion");
    }
    if (!mat_record.occlusion_srv) {
      mat_record.occlusion_srv = default_occlusion_;
    }

    mapping = aiTextureMapping_UV;
    uv_index = 0;
    blend = 1.0f;
    if (material->GetTexture(aiTextureType_EMISSIVE, 0, &tex_path,
                             &mapping, &uv_index, &blend, &op, mapmode) == AI_SUCCESS) {
      log_texture(aiTextureType_EMISSIVE, "emissive", tex_path, mapping, uv_index, blend);
      mat_record.emissive_srv = loadTextureFromAssimp(*scene, path.string(), base_dir, tex_path, true, "emissive");
    }
    if (!mat_record.emissive_srv) {
      mat_record.emissive_srv = default_emissive_;
    }

    if (pipeline_state_) {
      pipeline_state_->CreateShaderResourceBinding(&mat_record.srb, true);
      if (mat_record.srb) {
        if (auto* var = mat_record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
          var->Set(sampler_color_);
        }
        if (auto* var = mat_record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
          var->Set(sampler_data_);
        }
        if (auto* var = mat_record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_BaseColorTex")) {
          var->Set(mat_record.base_color_srv);
        }
        if (auto* var = mat_record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_NormalTex")) {
          var->Set(mat_record.normal_srv);
        }
        if (auto* var = mat_record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_MetallicRoughnessTex")) {
          var->Set(mat_record.metallic_roughness_srv);
        }
        if (auto* var = mat_record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_OcclusionTex")) {
          var->Set(mat_record.occlusion_srv);
        }
        if (auto* var = mat_record.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_EmissiveTex")) {
          var->Set(mat_record.emissive_srv);
        }
      }
    }

    materials_[mat_id] = mat_record;
    material_ids[mat_index] = mat_id;
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
  spdlog::warn("Karma: Mesh '{}' submeshes={} materials={}",
               path.string(), record.submeshes.size(), material_ids.size());

  meshes_[id] = std::move(record);
  return id;
}

void DiligentBackend::destroyMesh(renderer::MeshId mesh) {
  spdlog::warn("Karma: Diligent destroyMesh id={}", mesh);
  meshes_.erase(mesh);
}

renderer::MaterialId DiligentBackend::createMaterial(const renderer::MaterialDesc& material) {
  const renderer::MaterialId id = nextMaterialId_++;
  MaterialRecord record{};
  record.desc = material;
  record.base_color_factor = material.base_color;
  record.emissive_factor = glm::vec3(0.0f);
  record.metallic_factor = 1.0f;
  record.roughness_factor = 1.0f;
  if (material.base_color_texture != renderer::kInvalidTexture) {
    auto tex_it = textures_.find(material.base_color_texture);
    if (tex_it != textures_.end()) {
      record.base_color_srv = tex_it->second.srv;
    }
  }
  if (!record.base_color_srv) {
    record.base_color_srv = default_base_color_;
  }
  record.normal_srv = default_normal_;
  record.metallic_roughness_srv = default_metallic_roughness_;
  record.occlusion_srv = default_occlusion_;
  record.emissive_srv = default_emissive_;

  if (pipeline_state_) {
    pipeline_state_->CreateShaderResourceBinding(&record.srb, true);
    if (record.srb) {
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
    }
  }

  materials_[id] = std::move(record);
  return id;
}

void DiligentBackend::updateMaterial(renderer::MaterialId material, const renderer::MaterialDesc& desc) {
  auto it = materials_.find(material);
  if (it == materials_.end()) {
    return;
  }
  it->second.desc = desc;
  it->second.base_color_factor = desc.base_color;
}

void DiligentBackend::destroyMaterial(renderer::MaterialId material) {
  materials_.erase(material);
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
        view.Attach(raw_view);
        record.srv = view;
        if (desc.generate_mips && context_) {
          context_->GenerateMips(record.srv);
        }
      } else {
        spdlog::warn("Karma: Failed to create SRV for runtime texture");
      }
    }
  }
  textures_[id] = record;
  return id;
}

void DiligentBackend::destroyTexture(renderer::TextureId texture) {
  textures_.erase(texture);
}

renderer::RenderTargetId DiligentBackend::createRenderTarget(const renderer::RenderTargetDesc& desc) {
  const renderer::RenderTargetId id = nextTargetId_++;
  targets_[id] = RenderTargetRecord{desc};
  return id;
}

void DiligentBackend::destroyRenderTarget(renderer::RenderTargetId target) {
  targets_.erase(target);
}

}  // namespace karma::renderer_backend
