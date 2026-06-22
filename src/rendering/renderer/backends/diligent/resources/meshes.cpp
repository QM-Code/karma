#include "../backend.hpp"

#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/geometric.hpp>

namespace karma::rendering::backend {

namespace {
void computeBounds(const world::MeshData& mesh, glm::vec3& out_center, float& out_radius) {
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

std::vector<ParticleGpuMeshSample> buildParticleMeshSamples(const world::MeshData& mesh) {
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

void DiligentBackend::refreshSubmeshesFromMeshData(MeshRecord& record) {
  record.submeshes.clear();
  if (!record.data.submeshes.empty()) {
    record.submeshes.reserve(record.data.submeshes.size());
    for (const auto& src : record.data.submeshes) {
      MeshRecord::Submesh submesh{};
      submesh.index_offset = src.index_offset;
      submesh.index_count = src.index_count;
      submesh.material_slot = src.material_slot;
      record.submeshes.push_back(submesh);
    }
    return;
  }

  if (!record.data.indices.empty()) {
    MeshRecord::Submesh submesh{};
    submesh.index_offset = 0;
    submesh.index_count = static_cast<Diligent::Uint32>(record.data.indices.size());
    submesh.material_slot = 0;
    record.submeshes.push_back(submesh);
  }
}

void DiligentBackend::uploadMeshBuffers(const world::MeshData& mesh, MeshRecord& record) {
  record.vertex_buffer.Release();
  record.index_buffer.Release();
  record.vertex_count = 0;
  record.index_count = 0;
  record.morph_target_count = 0;

  if (device_ && !mesh.vertices.empty()) {
    const auto interleaved = buildInterleavedVertices(mesh);
    constexpr Diligent::Uint32 kVertexStride = static_cast<Diligent::Uint32>(22 * sizeof(float));
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

  uploadMeshMorphBuffers(mesh, record);
}

void DiligentBackend::uploadMeshMorphBuffers(const world::MeshData& mesh,
                                             MeshRecord& record) {
  record.morph_delta_buffer.Release();
  record.morph_delta_srv.Release();
  record.morph_target_count = 0;
  if (!device_ || mesh.vertices.empty() || mesh.morph_targets.empty()) {
    return;
  }

  std::vector<MorphTargetDeltaGpu> deltas;
  deltas.reserve(mesh.vertices.size() * mesh.morph_targets.size());
  for (const world::MeshData::MorphTarget& target : mesh.morph_targets) {
    for (size_t vertex_index = 0; vertex_index < mesh.vertices.size(); ++vertex_index) {
      MorphTargetDeltaGpu delta{};
      if (vertex_index < target.position_deltas.size()) {
        const glm::vec3& v = target.position_deltas[vertex_index];
        delta.position[0] = v.x;
        delta.position[1] = v.y;
        delta.position[2] = v.z;
      }
      if (vertex_index < target.normal_deltas.size()) {
        const glm::vec3& n = target.normal_deltas[vertex_index];
        delta.normal[0] = n.x;
        delta.normal[1] = n.y;
        delta.normal[2] = n.z;
      }
      if (vertex_index < target.tangent_deltas.size()) {
        const glm::vec3& t = target.tangent_deltas[vertex_index];
        delta.tangent[0] = t.x;
        delta.tangent[1] = t.y;
        delta.tangent[2] = t.z;
      }
      deltas.push_back(delta);
    }
  }
  if (deltas.empty()) {
    return;
  }

  Diligent::BufferDesc desc{};
  desc.Name = "Karma Morph Target Deltas";
  desc.Usage = Diligent::USAGE_IMMUTABLE;
  desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
  desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
  desc.ElementByteStride = static_cast<Diligent::Uint32>(sizeof(MorphTargetDeltaGpu));
  desc.Size = static_cast<Diligent::Uint64>(deltas.size()) *
              static_cast<Diligent::Uint64>(sizeof(MorphTargetDeltaGpu));
  Diligent::BufferData data{deltas.data(), desc.Size};
  device_->CreateBuffer(desc, &data, &record.morph_delta_buffer);
  if (!record.morph_delta_buffer) {
    return;
  }
  record.morph_delta_srv =
      record.morph_delta_buffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
  if (record.morph_delta_srv) {
    record.morph_target_count = static_cast<Diligent::Uint32>(mesh.morph_targets.size());
  }
}

rendering::MeshId DiligentBackend::createMesh(const world::MeshData& mesh) {
  const rendering::MeshId id = nextMeshId_++;
  MeshRecord record{};
  record.data = mesh;
  computeBounds(mesh, record.bounds_center, record.bounds_radius);
  record.particle_source_samples = buildParticleMeshSamples(record.data);
  record.base_color = glm::vec4(1.0f);
  uploadMeshBuffers(mesh, record);
  refreshSubmeshesFromMeshData(record);

  meshes_[id] = std::move(record);
  return id;
}

void DiligentBackend::updateMesh(rendering::MeshId mesh, const world::MeshData& data) {
  auto it = meshes_.find(mesh);
  if (it == meshes_.end()) {
    return;
  }

  MeshRecord& record = it->second;
  const std::vector<world::MeshSubmesh> previous_submeshes = record.data.submeshes;
  const std::vector<world::MeshMaterialSlot> previous_material_slots = record.data.material_slots;
  record.data = data;
  if (record.data.submeshes.empty()) {
    record.data.submeshes = previous_submeshes;
  }
  if (record.data.material_slots.empty()) {
    record.data.material_slots = previous_material_slots;
  }
  computeBounds(data, record.bounds_center, record.bounds_radius);
  record.particle_source_samples = buildParticleMeshSamples(record.data);
  uploadMeshBuffers(data, record);
  refreshSubmeshesFromMeshData(record);
}

void DiligentBackend::destroyMesh(rendering::MeshId mesh) {
  auto mesh_it = meshes_.find(mesh);
  if (mesh_it == meshes_.end()) {
    return;
  }

  for (const rendering::MaterialId material : mesh_it->second.owned_materials) {
    materials_.erase(material);
  }

  for (auto it = instances_.begin(); it != instances_.end();) {
    if (it->second.mesh == mesh) {
      if (it->second.shadow_visible) {
        directional_shadow_scene_dirty_ = true;
        point_shadow_scene_dirty_ = true;
      }
      it = instances_.erase(it);
    } else {
      ++it;
    }
  }
  for (const auto& entry : instanced_records_) {
    const auto& record = entry.second;
    if (record.mesh == mesh && record.shadow_visible) {
      directional_shadow_scene_dirty_ = true;
      point_shadow_scene_dirty_ = true;
      break;
    }
  }

  meshes_.erase(mesh_it);
}

bool DiligentBackend::getMeshBounds(rendering::MeshId mesh,
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

bool DiligentBackend::getMeshMaterialSlots(
    rendering::MeshId mesh,
    std::vector<world::MeshMaterialSlot>& out_slots) const {
  auto mesh_it = meshes_.find(mesh);
  if (mesh_it == meshes_.end()) {
    out_slots.clear();
    return false;
  }
  out_slots = mesh_it->second.data.material_slots;
  return true;
}

}  // namespace karma::rendering::backend
