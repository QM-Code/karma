#include "../backend.hpp"

#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace karma::rendering::backend {

namespace {

template <typename T, bool KeepStrongReferences = false>
T* getMappedData(Diligent::MapHelper<T, KeepStrongReferences>& map) {
  return static_cast<T*>(map);
}

template <typename T>
bool uploadStructuredBuffer(Diligent::IRenderDevice* device,
                            Diligent::IDeviceContext* context,
                            const char* name,
                            const std::vector<T>& data,
                            Diligent::RefCntAutoPtr<Diligent::IBuffer>& buffer,
                            Diligent::RefCntAutoPtr<Diligent::IBufferView>& srv,
                            size_t& capacity) {
  if (device == nullptr || context == nullptr) {
    return false;
  }

  const size_t required = std::max<size_t>(data.size(), 1u);
  if (!buffer || !srv || capacity < required) {
    const size_t next_capacity =
        std::max(required, capacity > 0u ? capacity * 2u : required);
    Diligent::BufferDesc desc{};
    desc.Name = name;
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
    desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = static_cast<Diligent::Uint32>(sizeof(T));
    desc.Size = static_cast<Diligent::Uint64>(next_capacity) *
                static_cast<Diligent::Uint64>(sizeof(T));

    std::vector<T> initial(next_capacity);
    if (!data.empty()) {
      std::copy(data.begin(), data.end(), initial.begin());
    }
    Diligent::BufferData init{initial.data(), desc.Size};
    Diligent::RefCntAutoPtr<Diligent::IBuffer> next_buffer;
    device->CreateBuffer(desc, &init, &next_buffer);
    if (!next_buffer) {
      return false;
    }
    buffer = std::move(next_buffer);
    srv = buffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
    capacity = next_capacity;
    return srv != nullptr;
  }

  if (!data.empty()) {
    context->UpdateBuffer(buffer,
                          0,
                          static_cast<Diligent::Uint32>(data.size() * sizeof(T)),
                          data.data(),
                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  }
  return true;
}

bool createFallbackStructuredBuffer(Diligent::IRenderDevice* device,
                                    const char* name,
                                    const void* data,
                                    size_t stride,
                                    Diligent::RefCntAutoPtr<Diligent::IBuffer>& buffer,
                                    Diligent::RefCntAutoPtr<Diligent::IBufferView>& srv) {
  if (device == nullptr || buffer) {
    return buffer && srv;
  }
  Diligent::BufferDesc desc{};
  desc.Name = name;
  desc.Usage = Diligent::USAGE_IMMUTABLE;
  desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
  desc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
  desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
  desc.ElementByteStride = static_cast<Diligent::Uint32>(stride);
  desc.Size = static_cast<Diligent::Uint64>(stride);
  Diligent::BufferData init{data, desc.Size};
  device->CreateBuffer(desc, &init, &buffer);
  if (!buffer) {
    return false;
  }
  srv = buffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
  return srv != nullptr;
}

}  // namespace

rendering::DeformationId DiligentBackend::createDeformation(
    const rendering::DeformationDesc& desc) {
  const rendering::DeformationId id = nextDeformationId_++;
  DeformationRecord record{};
  deformations_.emplace(id, std::move(record));
  updateDeformation(id, desc);
  return id;
}

void DiligentBackend::updateDeformation(rendering::DeformationId deformation,
                                        const rendering::DeformationDesc& desc) {
  if (deformation == rendering::kInvalidDeformation) {
    return;
  }
  auto it = deformations_.find(deformation);
  if (it == deformations_.end()) {
    return;
  }

  DeformationRecord& record = it->second;
  record.desc = desc;
  if (!record.desc.skinning_enabled) {
    record.desc.joint_palette.clear();
  }
  if (!record.desc.morphing_enabled) {
    record.desc.morph_weights.clear();
  }

  uploadStructuredBuffer(device_.RawPtr(),
                         context_.RawPtr(),
                         "Karma Deformation Joint Palette",
                         record.desc.joint_palette,
                         record.joint_palette_buffer,
                         record.joint_palette_srv,
                         record.joint_capacity);
  uploadStructuredBuffer(device_.RawPtr(),
                         context_.RawPtr(),
                         "Karma Deformation Morph Weights",
                         record.desc.morph_weights,
                         record.morph_weight_buffer,
                         record.morph_weight_srv,
                         record.morph_weight_capacity);
}

void DiligentBackend::destroyDeformation(rendering::DeformationId deformation) {
  deformations_.erase(deformation);
  for (auto& [id, instance] : instances_) {
    (void)id;
    if (instance.deformation == deformation) {
      instance.deformation = rendering::kInvalidDeformation;
    }
  }
}

rendering::DeformationStats DiligentBackend::getDeformationStats() const {
  rendering::DeformationStats stats{};
  stats.resource_count = static_cast<uint32_t>(
      std::min<size_t>(deformations_.size(), std::numeric_limits<uint32_t>::max()));
  for (const auto& [id, record] : deformations_) {
    (void)id;
    stats.joint_matrix_count += static_cast<uint32_t>(
        std::min<size_t>(record.desc.joint_palette.size(),
                         std::numeric_limits<uint32_t>::max() - stats.joint_matrix_count));
    stats.morph_weight_count += static_cast<uint32_t>(
        std::min<size_t>(record.desc.morph_weights.size(),
                         std::numeric_limits<uint32_t>::max() - stats.morph_weight_count));
  }
  return stats;
}

bool DiligentBackend::ensureFallbackDeformationResources() {
  if (!device_) {
    return false;
  }
  const glm::mat4 identity(1.0f);
  const float zero_weight = 0.0f;
  const MorphTargetDeltaGpu zero_delta{};
  return createFallbackStructuredBuffer(device_.RawPtr(),
                                        "Karma Fallback Joint Palette",
                                        &identity,
                                        sizeof(identity),
                                        fallback_joint_palette_buffer_,
                                        fallback_joint_palette_srv_) &&
         createFallbackStructuredBuffer(device_.RawPtr(),
                                        "Karma Fallback Morph Weight",
                                        &zero_weight,
                                        sizeof(zero_weight),
                                        fallback_morph_weight_buffer_,
                                        fallback_morph_weight_srv_) &&
         createFallbackStructuredBuffer(device_.RawPtr(),
                                        "Karma Fallback Morph Delta",
                                        &zero_delta,
                                        sizeof(zero_delta),
                                        fallback_morph_delta_buffer_,
                                        fallback_morph_delta_srv_);
}

bool DiligentBackend::bindDeformationResources(Diligent::IShaderResourceBinding* srb,
                                               const MeshRecord& mesh,
                                               rendering::DeformationId deformation) {
  if (srb == nullptr || !ensureFallbackDeformationResources()) {
    return false;
  }

  const DeformationRecord* record = nullptr;
  if (deformation != rendering::kInvalidDeformation) {
    const auto it = deformations_.find(deformation);
    if (it != deformations_.end()) {
      record = &it->second;
    }
  }

  Diligent::IBufferView* joint_srv = fallback_joint_palette_srv_;
  Diligent::IBufferView* morph_weight_srv = fallback_morph_weight_srv_;
  Diligent::IBufferView* morph_delta_srv = fallback_morph_delta_srv_;
  if (record != nullptr) {
    if (record->desc.skinning_enabled &&
        !record->desc.joint_palette.empty() &&
        record->joint_palette_srv) {
      joint_srv = record->joint_palette_srv;
    }
    if (record->desc.morphing_enabled &&
        !record->desc.morph_weights.empty() &&
        record->morph_weight_srv) {
      morph_weight_srv = record->morph_weight_srv;
      if (mesh.morph_delta_srv && mesh.morph_target_count > 0u) {
        morph_delta_srv = mesh.morph_delta_srv;
      }
    }
  }

  if (auto* var =
          srb->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "g_DeformationMatrices")) {
    var->Set(joint_srv);
  }
  if (auto* var =
          srb->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "g_MorphWeights")) {
    var->Set(morph_weight_srv);
  }
  if (auto* var =
          srb->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "g_MorphTargetDeltas")) {
    var->Set(morph_delta_srv);
  }
  return updateDeformationConstants(mesh, deformation);
}

bool DiligentBackend::updateDeformationConstants(const MeshRecord& mesh,
                                                 rendering::DeformationId deformation) {
  if (context_ == nullptr || deformation_constants_ == nullptr) {
    return false;
  }

  DeformationConstants constants{};
  const DeformationRecord* record = nullptr;
  if (deformation != rendering::kInvalidDeformation) {
    const auto it = deformations_.find(deformation);
    if (it != deformations_.end()) {
      record = &it->second;
    }
  }

  if (record != nullptr &&
      record->desc.skinning_enabled &&
      !record->desc.joint_palette.empty()) {
    constants.params[0] = 1.0f;
    constants.params[1] = static_cast<float>(record->desc.joint_palette.size());
  }
  if (record != nullptr &&
      record->desc.morphing_enabled &&
      !record->desc.morph_weights.empty() &&
      mesh.morph_delta_srv &&
      mesh.morph_target_count > 0u &&
      mesh.vertex_count > 0u) {
    constants.params[2] = static_cast<float>(
        std::min<size_t>(record->desc.morph_weights.size(), mesh.morph_target_count));
    constants.params[3] = static_cast<float>(mesh.vertex_count);
  }

  Diligent::MapHelper<DeformationConstants> mapped(
      context_, deformation_constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
  auto* mapped_constants = getMappedData(mapped);
  if (mapped_constants == nullptr) {
    return false;
  }
  *mapped_constants = constants;
  return true;
}

}  // namespace karma::rendering::backend
