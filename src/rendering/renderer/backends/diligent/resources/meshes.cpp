#include "../backend.hpp"

#include "../backend_internal.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>

#include <algorithm>
#include <limits>

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

void DiligentBackend::uploadMeshBuffers(const renderer::MeshData& mesh, MeshRecord& record) {
  record.vertex_buffer.Release();
  record.index_buffer.Release();
  record.vertex_count = 0;
  record.index_count = 0;

  if (device_ && !mesh.vertices.empty()) {
    const auto interleaved = buildInterleavedVertices(mesh);
    constexpr Diligent::Uint32 kVertexStride = static_cast<Diligent::Uint32>(20 * sizeof(float));
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
}

renderer::MeshId DiligentBackend::createMesh(const renderer::MeshData& mesh) {
  const renderer::MeshId id = nextMeshId_++;
  MeshRecord record{};
  record.data = mesh;
  computeBounds(mesh, record.bounds_center, record.bounds_radius);
  record.base_color = glm::vec4(1.0f);
  uploadMeshBuffers(mesh, record);

  if (!mesh.indices.empty()) {
    MeshRecord::Submesh submesh{};
    submesh.index_offset = 0;
    submesh.index_count = static_cast<Diligent::Uint32>(mesh.indices.size());
    record.submeshes.push_back(submesh);
  }

  meshes_[id] = std::move(record);
  return id;
}

void DiligentBackend::updateMesh(renderer::MeshId mesh, const renderer::MeshData& data) {
  auto it = meshes_.find(mesh);
  if (it == meshes_.end()) {
    return;
  }

  MeshRecord& record = it->second;
  record.data = data;
  computeBounds(data, record.bounds_center, record.bounds_radius);
  uploadMeshBuffers(data, record);
  record.submeshes.clear();
  if (!data.indices.empty()) {
    MeshRecord::Submesh submesh{};
    submesh.index_offset = 0;
    submesh.index_count = static_cast<Diligent::Uint32>(data.indices.size());
    record.submeshes.push_back(submesh);
  }
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

  MeshRecord record{};
  record.data = combined;
  record.base_color = base_color;
  computeBounds(record.data, record.bounds_center, record.bounds_radius);

  uploadMeshBuffers(combined, record);

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

bool DiligentBackend::getMeshBounds(renderer::MeshId mesh,
                                    glm::vec3& center,
                                    float& radius) const {
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

}  // namespace karma::renderer_backend
