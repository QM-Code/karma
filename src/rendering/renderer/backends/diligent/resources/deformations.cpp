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
#include <utility>
#include <vector>

namespace karma::rendering::backend {

namespace {

template <typename T, bool KeepStrongReferences = false>
T* getMappedData(Diligent::MapHelper<T, KeepStrongReferences>& map) {
  return static_cast<T*>(map);
}

template <typename T>
struct PreparedStructuredBufferUpload {
  Diligent::RefCntAutoPtr<Diligent::IBuffer> buffer;
  Diligent::RefCntAutoPtr<Diligent::IBufferView> srv;
  size_t capacity = 0u;
  Diligent::Uint64 upload_size = 0u;
};

template <typename T>
bool prepareStructuredBufferUpload(
    Diligent::IRenderDevice* device,
    Diligent::IDeviceContext* context,
    const char* name,
    const std::vector<T>& data,
    const Diligent::RefCntAutoPtr<Diligent::IBuffer>& current_buffer,
    const Diligent::RefCntAutoPtr<Diligent::IBufferView>& current_srv,
    size_t current_capacity,
    PreparedStructuredBufferUpload<T>& out) {
  static_assert(sizeof(T) <= std::numeric_limits<Diligent::Uint32>::max());
  if (data.empty()) {
    out.buffer = current_buffer;
    out.srv = current_srv;
    out.capacity = current_capacity;
    out.upload_size = 0u;
    return true;
  }
  if (device == nullptr || context == nullptr) {
    return false;
  }

  const Diligent::Uint64 max_capacity_u64 =
      std::numeric_limits<Diligent::Uint64>::max() / sizeof(T);
  const size_t max_capacity = static_cast<size_t>(std::min<Diligent::Uint64>(
      max_capacity_u64, std::numeric_limits<size_t>::max()));
  if (data.size() > max_capacity) {
    return false;
  }
  out.upload_size = static_cast<Diligent::Uint64>(data.size()) *
                    static_cast<Diligent::Uint64>(sizeof(T));

  const size_t required = data.size();
  if (!current_buffer || !current_srv || current_capacity < required) {
    const size_t grown_capacity =
        current_capacity == 0u
            ? required
            : (current_capacity > max_capacity / 2u
                   ? max_capacity
                   : current_capacity * 2u);
    const size_t next_capacity = std::max(required, grown_capacity);
    Diligent::BufferDesc desc{};
    desc.Name = name;
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
    desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = static_cast<Diligent::Uint32>(sizeof(T));
    desc.Size = static_cast<Diligent::Uint64>(next_capacity) *
                static_cast<Diligent::Uint64>(sizeof(T));

    Diligent::RefCntAutoPtr<Diligent::IBuffer> next_buffer;
    device->CreateBuffer(desc, nullptr, &next_buffer);
    if (!next_buffer) {
      return false;
    }
    Diligent::RefCntAutoPtr<Diligent::IBufferView> next_srv;
    next_srv = next_buffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
    if (!next_srv) {
      return false;
    }
    out.buffer = std::move(next_buffer);
    out.srv = std::move(next_srv);
    out.capacity = next_capacity;
    return true;
  }

  out.buffer = current_buffer;
  out.srv = current_srv;
  out.capacity = current_capacity;
  return true;
}

template <typename T>
void commitStructuredBufferUpload(
    Diligent::IDeviceContext* context,
    const std::vector<T>& data,
    const PreparedStructuredBufferUpload<T>& upload) {
  if (context == nullptr || data.empty() || !upload.buffer || upload.upload_size == 0u) {
    return;
  }
  context->UpdateBuffer(upload.buffer,
                        0u,
                        upload.upload_size,
                        data.data(),
                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

rendering::DeformationDesc sanitizedDeformationDesc(
    const rendering::DeformationDesc& desc) {
  rendering::DeformationDesc sanitized{};
  sanitized.skinning_enabled = desc.skinning_enabled;
  sanitized.morphing_enabled = desc.morphing_enabled;
  if (sanitized.skinning_enabled) {
    sanitized.joint_palette = desc.joint_palette;
  }
  if (sanitized.morphing_enabled) {
    sanitized.morph_weights = desc.morph_weights;
  }
  return sanitized;
}

bool deformationDescriptorShapeMatches(const rendering::DeformationDesc& lhs,
                                       const rendering::DeformationDesc& rhs) {
  return lhs.skinning_enabled == rhs.skinning_enabled &&
         lhs.morphing_enabled == rhs.morphing_enabled &&
         lhs.joint_palette.size() == rhs.joint_palette.size() &&
         lhs.morph_weights.size() == rhs.morph_weights.size();
}

bool createFallbackStructuredBuffer(Diligent::IRenderDevice* device,
                                    const char* name,
                                    const void* data,
                                    size_t stride,
                                    Diligent::RefCntAutoPtr<Diligent::IBuffer>& buffer,
                                    Diligent::RefCntAutoPtr<Diligent::IBufferView>& srv) {
  if (device == nullptr || data == nullptr || stride == 0u ||
      stride > std::numeric_limits<Diligent::Uint32>::max()) {
    return false;
  }
  if (buffer && srv) {
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
  Diligent::RefCntAutoPtr<Diligent::IBuffer> replacement_buffer;
  device->CreateBuffer(desc, &init, &replacement_buffer);
  if (!replacement_buffer) {
    return false;
  }
  Diligent::RefCntAutoPtr<Diligent::IBufferView> replacement_srv;
  replacement_srv =
      replacement_buffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
  if (!replacement_srv) {
    return false;
  }
  buffer = std::move(replacement_buffer);
  srv = std::move(replacement_srv);
  return true;
}

}  // namespace

rendering::DeformationId DiligentBackend::allocateDeformationId() noexcept {
  if (nextDeformationId_ == rendering::kInvalidDeformation) {
    return rendering::kInvalidDeformation;
  }
  const rendering::DeformationId id = nextDeformationId_;
  nextDeformationId_ = id == std::numeric_limits<rendering::DeformationId>::max()
                            ? rendering::kInvalidDeformation
                            : id + 1u;
  return id;
}

rendering::DeformationId DiligentBackend::createDeformation(
    const rendering::DeformationDesc& desc) {
  const rendering::DeformationId id = allocateDeformationId();
  if (id == rendering::kInvalidDeformation) {
    return rendering::kInvalidDeformation;
  }
  DeformationRecord record{};
  deformations_.emplace(id, std::move(record));
  updateDeformation(id, desc);
  const rendering::DeformationDesc expected = sanitizedDeformationDesc(desc);
  const auto created = deformations_.find(id);
  if (created == deformations_.end() ||
      !deformationDescriptorShapeMatches(created->second.desc, expected)) {
    deformations_.erase(id);
    return rendering::kInvalidDeformation;
  }
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
  rendering::DeformationDesc next_desc = sanitizedDeformationDesc(desc);
  PreparedStructuredBufferUpload<glm::mat4> joint_upload;
  PreparedStructuredBufferUpload<float> morph_upload;
  if (!prepareStructuredBufferUpload(device_.RawPtr(),
                                     context_.RawPtr(),
                                     "Karma Deformation Joint Palette",
                                     next_desc.joint_palette,
                                     record.joint_palette_buffer,
                                     record.joint_palette_srv,
                                     record.joint_capacity,
                                     joint_upload) ||
      !prepareStructuredBufferUpload(device_.RawPtr(),
                                     context_.RawPtr(),
                                     "Karma Deformation Morph Weights",
                                     next_desc.morph_weights,
                                     record.morph_weight_buffer,
                                     record.morph_weight_srv,
                                     record.morph_weight_capacity,
                                     morph_upload)) {
    return;
  }

  commitStructuredBufferUpload(context_.RawPtr(), next_desc.joint_palette, joint_upload);
  commitStructuredBufferUpload(context_.RawPtr(), next_desc.morph_weights, morph_upload);
  record.joint_palette_buffer = std::move(joint_upload.buffer);
  record.joint_palette_srv = std::move(joint_upload.srv);
  record.joint_capacity = joint_upload.capacity;
  record.morph_weight_buffer = std::move(morph_upload.buffer);
  record.morph_weight_srv = std::move(morph_upload.srv);
  record.morph_weight_capacity = morph_upload.capacity;
  record.desc = std::move(next_desc);

  const bool affects_shadow_caster = std::any_of(
      instances_.begin(), instances_.end(), [deformation](const auto& entry) {
        return entry.second.deformation == deformation && entry.second.shadow_visible;
      });
  if (affects_shadow_caster) {
    directional_shadow_scene_dirty_ = true;
    point_shadow_scene_dirty_ = true;
  }
}

void DiligentBackend::destroyDeformation(rendering::DeformationId deformation) {
  deformations_.erase(deformation);
  bool affected_shadow_caster = false;
  for (auto& [id, instance] : instances_) {
    (void)id;
    if (instance.deformation == deformation) {
      affected_shadow_caster = affected_shadow_caster || instance.shadow_visible;
      instance.deformation = rendering::kInvalidDeformation;
    }
  }
  if (affected_shadow_caster) {
    directional_shadow_scene_dirty_ = true;
    point_shadow_scene_dirty_ = true;
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
