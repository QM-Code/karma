#include "../backend.hpp"

#include "../backend_internal.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <glm/geometric.hpp>
#include <spdlog/spdlog.h>

#include "karma/core/time.h"

namespace karma::renderer_backend {

namespace {
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

bool renderResourceDiagEnabled() {
  static const bool enabled = envFlagEnabled(std::getenv("KARMA_RENDER_RESOURCE_DIAG"));
  return enabled;
}

void computeBounds(const geometry::MeshData& mesh, glm::vec3& out_center, float& out_radius) {
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

std::vector<ParticleGpuMeshSample> buildParticleMeshSamples(const geometry::MeshData& mesh) {
  std::vector<ParticleGpuMeshSample> samples;
  float cumulative_area = 0.0f;

  auto append_triangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
    if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
      return;
    }
    const glm::vec3& p0 = mesh.vertices[i0];
    const glm::vec3& p1 = mesh.vertices[i1];
    const glm::vec3& p2 = mesh.vertices[i2];
    const float area = 0.5f * glm::length(glm::cross(p1 - p0, p2 - p0));
    if (!(area > 1.0e-7f) || !std::isfinite(area)) {
      return;
    }

    cumulative_area += area;
    ParticleGpuMeshSample sample{};
    sample.p0[0] = p0.x;
    sample.p0[1] = p0.y;
    sample.p0[2] = p0.z;
    sample.p0[3] = cumulative_area;
    sample.p1[0] = p1.x;
    sample.p1[1] = p1.y;
    sample.p1[2] = p1.z;
    sample.p2[0] = p2.x;
    sample.p2[1] = p2.y;
    sample.p2[2] = p2.z;
    samples.push_back(sample);
  };

  if (!mesh.indices.empty()) {
    samples.reserve(mesh.indices.size() / 3u);
    for (std::size_t i = 0u; i + 2u < mesh.indices.size(); i += 3u) {
      append_triangle(mesh.indices[i], mesh.indices[i + 1u], mesh.indices[i + 2u]);
    }
  } else {
    samples.reserve(mesh.vertices.size() / 3u);
    for (std::size_t i = 0u; i + 2u < mesh.vertices.size(); i += 3u) {
      append_triangle(static_cast<uint32_t>(i),
                      static_cast<uint32_t>(i + 1u),
                      static_cast<uint32_t>(i + 2u));
    }
  }

  return samples;
}

}  // namespace

void DiligentBackend::uploadMeshBuffers(const geometry::MeshData& mesh, MeshRecord& record) {
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

renderer::MeshId DiligentBackend::createMesh(const geometry::MeshData& mesh) {
  const renderer::MeshId id = nextMeshId_++;
  MeshRecord record{};
  record.data = mesh;
  computeBounds(mesh, record.bounds_center, record.bounds_radius);
  record.particle_source_samples = buildParticleMeshSamples(record.data);
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

void DiligentBackend::updateMesh(renderer::MeshId mesh, const geometry::MeshData& data) {
  auto it = meshes_.find(mesh);
  if (it == meshes_.end()) {
    return;
  }

  MeshRecord& record = it->second;
  record.data = data;
  computeBounds(data, record.bounds_center, record.bounds_radius);
  record.particle_source_samples = buildParticleMeshSamples(record.data);
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
  const bool diag_enabled = renderResourceDiagEnabled();
  const auto total_start = core::SteadyClock::now();

  Assimp::Importer importer;
  auto section_start = total_start;
  const aiScene* scene = importer.ReadFile(path.string(),
                                           aiProcess_Triangulate |
                                           aiProcess_GenNormals |
                                           aiProcess_CalcTangentSpace |
                                           aiProcess_JoinIdenticalVertices |
                                           aiProcess_PreTransformVertices);
  auto section_end = core::SteadyClock::now();
  if (diag_enabled) {
    spdlog::info("Render resource '{}' Assimp import took {:.2f} ms",
                 path.string(),
                 core::elapsedMilliseconds(section_start, section_end));
  }
  if (!scene || !scene->mRootNode) {
    meshes_[id] = MeshRecord{};
    return id;
  }

  glm::vec4 base_color(1.0f);
  std::vector<SubmeshInfo> submesh_infos;
  section_start = section_end;
  const auto combined = combineMeshes(*scene, base_color, submesh_infos);
  section_end = core::SteadyClock::now();
  if (diag_enabled) {
    spdlog::info(
        "Render resource '{}' combineMeshes took {:.2f} ms (vertices={} indices={} submeshes={})",
        path.string(),
        core::elapsedMilliseconds(section_start, section_end),
        combined.vertices.size(),
        combined.indices.size(),
        submesh_infos.size());
  }

  MeshRecord record{};
  record.data = combined;
  record.base_color = base_color;
  section_start = section_end;
  computeBounds(record.data, record.bounds_center, record.bounds_radius);
  record.particle_source_samples = buildParticleMeshSamples(record.data);
  section_end = core::SteadyClock::now();
  if (diag_enabled) {
    spdlog::info("Render resource '{}' bounds took {:.2f} ms",
                 path.string(),
                 core::elapsedMilliseconds(section_start, section_end));
  }

  section_start = section_end;
  uploadMeshBuffers(combined, record);
  section_end = core::SteadyClock::now();
  if (diag_enabled) {
    spdlog::info("Render resource '{}' GPU mesh upload took {:.2f} ms",
                 path.string(),
                 core::elapsedMilliseconds(section_start, section_end));
  }

  std::vector<renderer::MaterialId> material_ids;
  material_ids.resize(scene->mNumMaterials, renderer::kInvalidMaterial);
  section_start = section_end;
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
  section_end = core::SteadyClock::now();
  if (diag_enabled) {
    spdlog::info("Render resource '{}' material setup took {:.2f} ms (materials={})",
                 path.string(),
                 core::elapsedMilliseconds(section_start, section_end),
                 scene->mNumMaterials);
  }

  section_start = section_end;
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
  section_end = core::SteadyClock::now();
  if (diag_enabled) {
    spdlog::info("Render resource '{}' submesh finalize took {:.2f} ms",
                 path.string(),
                 core::elapsedMilliseconds(section_start, section_end));
  }

  meshes_[id] = std::move(record);
  if (diag_enabled) {
    spdlog::info("Render resource '{}' createMeshFromFile total took {:.2f} ms",
                 path.string(),
                 core::elapsedMillisecondsSince(total_start));
  }
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
