#include "../backend.hpp"

#include "../backend_internal.h"
#include "private/rendering/lod_selection.hpp"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/BufferView.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <spdlog/spdlog.h>

namespace karma::rendering::backend {

namespace {

struct alignas(16) InstanceGpuData {
  float col0[4];
  float col1[4];
  float col2[4];
  float col3[4];
  float params[4];
};

template <typename T, bool KeepStrongReferences = false>
T* getMappedData(Diligent::MapHelper<T, KeepStrongReferences>& map) {
  return static_cast<T*>(map);
}

bool uploadInstanceData(Diligent::IDeviceContext* context,
                        Diligent::IBuffer* buffer,
                        const InstanceGpuData* instances,
                        size_t instance_count) {
  if (!context || !buffer || !instances || instance_count == 0) {
    return false;
  }

  Diligent::PVoid mapped_data = nullptr;
  context->MapBuffer(buffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD, mapped_data);
  if (mapped_data != nullptr) {
    std::memcpy(mapped_data, instances, instance_count * sizeof(InstanceGpuData));
  }
  context->UnmapBuffer(buffer, Diligent::MAP_WRITE);
  return mapped_data != nullptr;
}

InstanceGpuData packInstanceData(const glm::mat4& transform, const glm::vec4& params) {
  InstanceGpuData packed{};
  const float* transform_data = glm::value_ptr(transform);
  std::memcpy(packed.col0, transform_data, sizeof(packed.col0));
  std::memcpy(packed.col1, transform_data + 4, sizeof(packed.col1));
  std::memcpy(packed.col2, transform_data + 8, sizeof(packed.col2));
  std::memcpy(packed.col3, transform_data + 12, sizeof(packed.col3));
  const float* params_data = glm::value_ptr(params);
  std::memcpy(packed.params, params_data, sizeof(packed.params));
  return packed;
}

float maxTransformScale(const glm::mat4& transform) {
  const float sx = glm::length(glm::vec3(transform[0]));
  const float sy = glm::length(glm::vec3(transform[1]));
  const float sz = glm::length(glm::vec3(transform[2]));
  return std::max(sx, std::max(sy, sz));
}

glm::vec4 transformBoundingSphere(const glm::mat4& world,
                                  const glm::vec3& local_center,
                                  float local_radius) {
  if (local_radius <= 0.0f) {
    return glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
  }
  const glm::vec3 center = glm::vec3(world * glm::vec4(local_center, 1.0f));
  const float radius = local_radius * maxTransformScale(world);
  return glm::vec4(center, radius);
}

void mergeBoundingSphere(glm::vec3& center,
                         float& radius,
                         bool& valid,
                         const glm::vec3& next_center,
                         float next_radius) {
  if (next_radius <= 0.0f || !std::isfinite(next_radius)) {
    return;
  }
  if (!valid) {
    center = next_center;
    radius = next_radius;
    valid = true;
    return;
  }
  const glm::vec3 delta = next_center - center;
  const float distance = glm::length(delta);
  if (!std::isfinite(distance) || distance + next_radius <= radius) {
    return;
  }
  if (distance + radius <= next_radius) {
    center = next_center;
    radius = next_radius;
    return;
  }
  if (distance <= 1.0e-5f) {
    radius = std::max(radius, next_radius);
    return;
  }
  const float merged_radius = (radius + distance + next_radius) * 0.5f;
  center += delta * ((merged_radius - radius) / distance);
  radius = merged_radius;
}

glm::mat4 planarInstanceTransform(const rendering::PlanarInstanceData& instance) {
  const glm::vec3 position(instance.position_yaw);
  const float yaw = instance.position_yaw.w;
  const glm::vec3 scale(instance.scale_pad);
  glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
  transform = glm::rotate(transform, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
  transform = glm::scale(transform, scale);
  return transform;
}

bool sphereIntersectsClipVolume(const glm::mat4& clip_from_world,
                                const glm::vec4& sphere,
                                bool is_gl_ndc) {
  if (sphere.w <= 0.0f) {
    return true;
  }
  const glm::vec4 clip = clip_from_world * glm::vec4(sphere.x, sphere.y, sphere.z, 1.0f);
  if (std::abs(clip.w) <= 1e-5f) {
    return true;
  }
  const float sx = glm::length(glm::vec3(clip_from_world[0]));
  const float sy = glm::length(glm::vec3(clip_from_world[1]));
  const float sz = glm::length(glm::vec3(clip_from_world[2]));
  const float r = sphere.w * std::max(sx, std::max(sy, sz));

  if (clip.x < -clip.w - r || clip.x > clip.w + r) {
    return false;
  }
  if (clip.y < -clip.w - r || clip.y > clip.w + r) {
    return false;
  }
  if (is_gl_ndc) {
    if (clip.z < -clip.w - r || clip.z > clip.w + r) {
      return false;
    }
  } else {
    if (clip.z < -r || clip.z > clip.w + r) {
      return false;
    }
  }
  return true;
}

Diligent::Viewport buildViewport(int render_width, int render_height) {
  Diligent::Viewport viewport{};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(render_width);
  viewport.Height = static_cast<float>(render_height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  return viewport;
}

#if defined(NDEBUG)
constexpr auto kHotPathDrawFlags = Diligent::DRAW_FLAG_NONE;
#else
constexpr auto kHotPathDrawFlags = Diligent::DRAW_FLAG_VERIFY_ALL;
#endif

static constexpr uint32_t kInstancedGpuLodBucketCapacity = 4u;

static constexpr const char* kInstancedGpuCullingShader = R"(
cbuffer InstancedGpuCullingConstants
{
    float4x4 g_ViewProj;
    float4x4 g_BatchTransform;
    float4 g_MeshBounds;
    float4 g_LodReferenceBounds;
    float4 g_CameraPosition;
    float4 g_DistanceParams;
    uint4 g_Params;
};

#if KARMA_MATRIX_INSTANCE_LAYOUT
struct InstancePayload
{
    float4 col0;
    float4 col1;
    float4 col2;
    float4 col3;
    float4 params;
};

float3 TransformSourcePoint(InstancePayload instance, float3 local_point)
{
    return (instance.col0 * local_point.x + instance.col1 * local_point.y +
            instance.col2 * local_point.z + instance.col3).xyz;
}
#else
struct InstancePayload
{
    float4 position_yaw;
    float4 scale_pad;
    float4 params;
};

float3 TransformSourcePoint(InstancePayload instance, float3 local_point)
{
    float3 scaled = local_point * instance.scale_pad.xyz;
    float s = sin(instance.position_yaw.w);
    float c = cos(instance.position_yaw.w);
    return instance.position_yaw.xyz +
           float3(scaled.x * c + scaled.z * s,
                  scaled.y,
                  -scaled.x * s + scaled.z * c);
}
#endif

float3 TransformFinalPoint(InstancePayload instance, float3 local_point)
{
    float4 batch_point = mul(g_BatchTransform, float4(local_point, 1.0));
    return TransformSourcePoint(instance,
                                batch_point.xyz / max(abs(batch_point.w), 1.0e-5));
}

struct DrawIndexedIndirectArgs
{
    uint NumIndices;
    uint NumInstances;
    uint FirstIndexLocation;
    uint BaseVertex;
    uint FirstInstanceLocation;
};

StructuredBuffer<InstancePayload> g_SourceInstances;
RWStructuredBuffer<InstancePayload> g_VisibleInstances;
RWStructuredBuffer<DrawIndexedIndirectArgs> g_DrawArgs;

bool SphereIntersectsClipVolume(float3 center, float radius)
{
    if (radius <= 0.0)
    {
        return true;
    }

    float4 clip = mul(g_ViewProj, float4(center, 1.0));
    float r = radius * max(length(float3(g_ViewProj._11, g_ViewProj._12, g_ViewProj._13)),
                           max(length(float3(g_ViewProj._21, g_ViewProj._22, g_ViewProj._23)),
                               length(float3(g_ViewProj._31, g_ViewProj._32, g_ViewProj._33))));
    if (abs(clip.w) <= 1.0e-5)
    {
        return true;
    }
    if (clip.x < -clip.w - r || clip.x > clip.w + r)
    {
        return false;
    }
    if (clip.y < -clip.w - r || clip.y > clip.w + r)
    {
        return false;
    }
    if (g_Params.w != 0u)
    {
        if (clip.z < -clip.w - r || clip.z > clip.w + r)
        {
            return false;
        }
    }
    else if (clip.z < -r || clip.z > clip.w + r)
    {
        return false;
    }
    return true;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint instance_index = dispatch_id.x;
    uint instance_count = g_Params.x;
    if (instance_index >= instance_count)
    {
        return;
    }

    InstancePayload instance = g_SourceInstances[instance_index];
    float3 origin = TransformFinalPoint(instance, float3(0.0, 0.0, 0.0));
    float3 axis_x = TransformFinalPoint(instance, float3(1.0, 0.0, 0.0)) - origin;
    float3 axis_y = TransformFinalPoint(instance, float3(0.0, 1.0, 0.0)) - origin;
    float3 axis_z = TransformFinalPoint(instance, float3(0.0, 0.0, 1.0)) - origin;
    float3 center = TransformFinalPoint(instance, g_MeshBounds.xyz);
    float radius = g_MeshBounds.w * max(length(axis_x), max(length(axis_y), length(axis_z)));
    if (!SphereIntersectsClipVolume(center, radius))
    {
        return;
    }
    float3 lod_reference_center =
        TransformFinalPoint(instance, g_LodReferenceBounds.xyz);
    float distance_to_camera = length(lod_reference_center - g_CameraPosition.xyz);
    if (distance_to_camera < g_DistanceParams.x ||
        distance_to_camera >= g_DistanceParams.y)
    {
        return;
    }

    uint visible_index = 0u;
    InterlockedAdd(g_DrawArgs[0].NumInstances, 1u, visible_index);
    g_VisibleInstances[visible_index] = instance;
}
)";

static constexpr const char* kInstancedGpuLodCullingShader = R"(
cbuffer InstancedGpuCullingConstants
{
    float4x4 g_ViewProj;
    float4x4 g_BatchTransform;
    float4 g_MeshBounds;
    float4 g_LodReferenceBounds;
    float4 g_CameraPosition;
    float4 g_DistanceParams;
    uint4 g_Params;
};

#if KARMA_MATRIX_INSTANCE_LAYOUT
struct InstancePayload
{
    float4 col0;
    float4 col1;
    float4 col2;
    float4 col3;
    float4 params;
};

float3 TransformSourcePoint(InstancePayload instance, float3 local_point)
{
    return (instance.col0 * local_point.x + instance.col1 * local_point.y +
            instance.col2 * local_point.z + instance.col3).xyz;
}
#else
struct InstancePayload
{
    float4 position_yaw;
    float4 scale_pad;
    float4 params;
};

float3 TransformSourcePoint(InstancePayload instance, float3 local_point)
{
    float3 scaled = local_point * instance.scale_pad.xyz;
    float s = sin(instance.position_yaw.w);
    float c = cos(instance.position_yaw.w);
    return instance.position_yaw.xyz +
           float3(scaled.x * c + scaled.z * s,
                  scaled.y,
                  -scaled.x * s + scaled.z * c);
}
#endif

float3 TransformFinalPoint(InstancePayload instance, float3 local_point)
{
    float4 batch_point = mul(g_BatchTransform, float4(local_point, 1.0));
    return TransformSourcePoint(instance,
                                batch_point.xyz / max(abs(batch_point.w), 1.0e-5));
}

struct DrawIndexedIndirectArgs
{
    uint NumIndices;
    uint NumInstances;
    uint FirstIndexLocation;
    uint BaseVertex;
    uint FirstInstanceLocation;
};

StructuredBuffer<InstancePayload> g_SourceInstances;
RWStructuredBuffer<InstancePayload> g_VisibleInstances0;
RWStructuredBuffer<InstancePayload> g_VisibleInstances1;
RWStructuredBuffer<InstancePayload> g_VisibleInstances2;
RWStructuredBuffer<InstancePayload> g_VisibleInstances3;
RWStructuredBuffer<DrawIndexedIndirectArgs> g_DrawArgs0;
RWStructuredBuffer<DrawIndexedIndirectArgs> g_DrawArgs1;
RWStructuredBuffer<DrawIndexedIndirectArgs> g_DrawArgs2;
RWStructuredBuffer<DrawIndexedIndirectArgs> g_DrawArgs3;

bool SphereIntersectsClipVolume(float3 center, float radius)
{
    if (radius <= 0.0)
    {
        return true;
    }

    float4 clip = mul(g_ViewProj, float4(center, 1.0));
    float r = radius * max(length(float3(g_ViewProj._11, g_ViewProj._12, g_ViewProj._13)),
                           max(length(float3(g_ViewProj._21, g_ViewProj._22, g_ViewProj._23)),
                               length(float3(g_ViewProj._31, g_ViewProj._32, g_ViewProj._33))));
    if (abs(clip.w) <= 1.0e-5)
    {
        return true;
    }
    if (clip.x < -clip.w - r || clip.x > clip.w + r)
    {
        return false;
    }
    if (clip.y < -clip.w - r || clip.y > clip.w + r)
    {
        return false;
    }
    if (g_Params.w != 0u)
    {
        if (clip.z < -clip.w - r || clip.z > clip.w + r)
        {
            return false;
        }
    }
    else if (clip.z < -r || clip.z > clip.w + r)
    {
        return false;
    }
    return true;
}

void AppendVisible(uint bucket, InstancePayload instance)
{
    uint visible_index = 0u;
    if (bucket == 0u)
    {
        InterlockedAdd(g_DrawArgs0[0].NumInstances, 1u, visible_index);
        g_VisibleInstances0[visible_index] = instance;
    }
    else if (bucket == 1u)
    {
        InterlockedAdd(g_DrawArgs1[0].NumInstances, 1u, visible_index);
        g_VisibleInstances1[visible_index] = instance;
    }
    else if (bucket == 2u)
    {
        InterlockedAdd(g_DrawArgs2[0].NumInstances, 1u, visible_index);
        g_VisibleInstances2[visible_index] = instance;
    }
    else
    {
        InterlockedAdd(g_DrawArgs3[0].NumInstances, 1u, visible_index);
        g_VisibleInstances3[visible_index] = instance;
    }
}

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint instance_index = dispatch_id.x;
    uint instance_count = g_Params.x;
    if (instance_index >= instance_count)
    {
        return;
    }

    InstancePayload instance = g_SourceInstances[instance_index];
    float3 origin = TransformFinalPoint(instance, float3(0.0, 0.0, 0.0));
    float3 axis_x = TransformFinalPoint(instance, float3(1.0, 0.0, 0.0)) - origin;
    float3 axis_y = TransformFinalPoint(instance, float3(0.0, 1.0, 0.0)) - origin;
    float3 axis_z = TransformFinalPoint(instance, float3(0.0, 0.0, 1.0)) - origin;
    float3 center = TransformFinalPoint(instance, g_MeshBounds.xyz);
    float radius = g_MeshBounds.w * max(length(axis_x), max(length(axis_y), length(axis_z)));
    if (!SphereIntersectsClipVolume(center, radius))
    {
        return;
    }

    float3 lod_reference_center =
        TransformFinalPoint(instance, g_LodReferenceBounds.xyz);
    float distance_to_camera = length(lod_reference_center - g_CameraPosition.xyz);
    uint bucket_count = min(max(g_Params.y, 1u), 4u);
    uint bucket = 0u;
    if (bucket_count > 1u && distance_to_camera >= g_DistanceParams.x)
    {
        bucket = 1u;
    }
    if (bucket_count > 2u && distance_to_camera >= g_DistanceParams.y)
    {
        bucket = 2u;
    }
    if (bucket_count > 3u && distance_to_camera >= g_DistanceParams.z)
    {
        bucket = 3u;
    }
    AppendVisible(bucket, instance);
}
)";

bool envFlagDisabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") == 0 ||
         std::strcmp(value, "false") == 0 ||
         std::strcmp(value, "FALSE") == 0 ||
         std::strcmp(value, "off") == 0 ||
         std::strcmp(value, "OFF") == 0;
}

}  // namespace

bool DiligentBackend::instancedGpuCullingEnabled() const {
  return !envFlagDisabled(std::getenv("KARMA_RENDER_GPU_CULLING"));
}

bool DiligentBackend::ensureInstancedGpuCullingResources(
    rendering::InstanceGpuLayout layout) {
  if (!instancedGpuCullingEnabled()) {
    return false;
  }
  if (!device_ || !context_) {
    return false;
  }
  const auto& draw_caps = device_->GetAdapterInfo().DrawCommand;
  if ((draw_caps.CapFlags & Diligent::DRAW_COMMAND_CAP_FLAG_DRAW_INDIRECT) == 0) {
    if (!warned_instanced_gpu_culling_unsupported_) {
      spdlog::warn("Instanced GPU culling disabled: adapter does not support indirect draw");
      warned_instanced_gpu_culling_unsupported_ = true;
    }
    return false;
  }
  const size_t layout_index = instanceGpuLayoutIndex(layout);
  auto& culling_pso = instanced_gpu_culling_psos_[layout_index];
  auto& culling_srb = instanced_gpu_culling_srbs_[layout_index];
  auto*& source_var = instanced_gpu_culling_source_vars_[layout_index];
  auto*& visible_var = instanced_gpu_culling_visible_vars_[layout_index];
  auto*& args_var = instanced_gpu_culling_args_vars_[layout_index];
  if (culling_pso &&
      culling_srb &&
      instanced_gpu_culling_cb_) {
    return true;
  }

  const Diligent::ShaderMacro layout_macro{
      "KARMA_MATRIX_INSTANCE_LAYOUT",
      layout == rendering::InstanceGpuLayout::Matrix4x4Params ? "1" : "0"};
  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.Macros = Diligent::ShaderMacroArray{&layout_macro, 1u};
  shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};
  shader_ci.Desc.Name = "Karma Instanced GPU Culling CS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kInstancedGpuCullingShader;
  Diligent::RefCntAutoPtr<Diligent::IShader> culling_cs =
      device_with_cache_.CreateShader(shader_ci);
  if (!culling_cs) {
    return false;
  }

  Diligent::ComputePipelineStateCreateInfo pso_ci{};
  pso_ci.PSODesc.Name = "Karma Instanced GPU Culling Pipeline";
  pso_ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
  pso_ci.pCS = culling_cs;

  constexpr auto kStorageBufferVariableFlags =
      Diligent::SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS;
  Diligent::ShaderResourceVariableDesc variables[] = {
      {Diligent::SHADER_TYPE_COMPUTE,
       "InstancedGpuCullingConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_SourceInstances",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
       kStorageBufferVariableFlags},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_VisibleInstances",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
       kStorageBufferVariableFlags},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_DrawArgs",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
       kStorageBufferVariableFlags},
  };
  pso_ci.PSODesc.ResourceLayout.Variables = variables;
  pso_ci.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(std::size(variables));

  const auto pso_start = core::SteadyClock::now();
  culling_pso = createComputePipelineState(pso_ci);
  recordPipelineCreation("instancing",
                         "Karma Instanced GPU Culling Pipeline",
                         pso_start,
                         core::SteadyClock::now());
  if (!culling_pso) {
    return false;
  }

  if (!instanced_gpu_culling_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma Instanced GPU Culling Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(InstancedGpuCullingConstants);
    const auto cb_start = core::SteadyClock::now();
    device_->CreateBuffer(cb_desc, nullptr, &instanced_gpu_culling_cb_);
    recordResourceCreation("instancing",
                           "instanced gpu culling constants",
                           cb_start,
                           core::SteadyClock::now());
  }
  if (!instanced_gpu_culling_cb_) {
    return false;
  }
  if (auto* var = culling_pso->GetStaticVariableByName(
          Diligent::SHADER_TYPE_COMPUTE, "InstancedGpuCullingConstants")) {
    var->Set(instanced_gpu_culling_cb_);
  }

  const auto srb_start = core::SteadyClock::now();
  culling_pso->CreateShaderResourceBinding(&culling_srb, true);
  recordResourceCreation("instancing",
                         "instanced gpu culling SRB",
                         srb_start,
                         core::SteadyClock::now());
  if (!culling_srb) {
    return false;
  }
  source_var = culling_srb->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                              "g_SourceInstances");
  visible_var = culling_srb->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                               "g_VisibleInstances");
  args_var = culling_srb->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                            "g_DrawArgs");
  return source_var && visible_var && args_var;
}

bool DiligentBackend::ensureInstancedGpuLodCullingResources(
    rendering::InstanceGpuLayout layout) {
  if (!ensureInstancedGpuCullingResources(layout)) {
    return false;
  }
  const size_t layout_index = instanceGpuLayoutIndex(layout);
  auto& culling_pso = instanced_gpu_lod_culling_psos_[layout_index];
  auto& culling_srb = instanced_gpu_lod_culling_srbs_[layout_index];
  auto*& source_var = instanced_gpu_lod_culling_source_vars_[layout_index];
  auto& visible_vars = instanced_gpu_lod_culling_visible_vars_[layout_index];
  auto& args_vars = instanced_gpu_lod_culling_args_vars_[layout_index];
  if (culling_pso &&
      culling_srb &&
      instanced_gpu_culling_cb_ &&
      source_var) {
    bool variables_ready = true;
    for (size_t i = 0; i < kInstancedGpuLodBucketCapacity; ++i) {
      variables_ready = variables_ready &&
                        visible_vars[i] && args_vars[i];
    }
    if (variables_ready) {
      return true;
    }
  }

  const Diligent::ShaderMacro layout_macro{
      "KARMA_MATRIX_INSTANCE_LAYOUT",
      layout == rendering::InstanceGpuLayout::Matrix4x4Params ? "1" : "0"};
  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.Macros = Diligent::ShaderMacroArray{&layout_macro, 1u};
  shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};
  shader_ci.Desc.Name = "Karma Instanced GPU LOD Culling CS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_COMPUTE;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kInstancedGpuLodCullingShader;
  Diligent::RefCntAutoPtr<Diligent::IShader> culling_cs =
      device_with_cache_.CreateShader(shader_ci);
  if (!culling_cs) {
    return false;
  }

  Diligent::ComputePipelineStateCreateInfo pso_ci{};
  pso_ci.PSODesc.Name = "Karma Instanced GPU LOD Culling Pipeline";
  pso_ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
  pso_ci.pCS = culling_cs;

  constexpr auto kStorageBufferVariableFlags =
      Diligent::SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS;
  Diligent::ShaderResourceVariableDesc variables[] = {
      {Diligent::SHADER_TYPE_COMPUTE,
       "InstancedGpuCullingConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_SourceInstances",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
       kStorageBufferVariableFlags},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_VisibleInstances0",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
       kStorageBufferVariableFlags},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_VisibleInstances1",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
       kStorageBufferVariableFlags},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_VisibleInstances2",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
       kStorageBufferVariableFlags},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_VisibleInstances3",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
       kStorageBufferVariableFlags},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_DrawArgs0",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
       kStorageBufferVariableFlags},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_DrawArgs1",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
       kStorageBufferVariableFlags},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_DrawArgs2",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
       kStorageBufferVariableFlags},
      {Diligent::SHADER_TYPE_COMPUTE,
       "g_DrawArgs3",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE,
       kStorageBufferVariableFlags},
  };
  pso_ci.PSODesc.ResourceLayout.Variables = variables;
  pso_ci.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(std::size(variables));

  const auto pso_start = core::SteadyClock::now();
  culling_pso = createComputePipelineState(pso_ci);
  recordPipelineCreation("instancing",
                         "Karma Instanced GPU LOD Culling Pipeline",
                         pso_start,
                         core::SteadyClock::now());
  if (!culling_pso) {
    return false;
  }

  if (auto* var = culling_pso->GetStaticVariableByName(
          Diligent::SHADER_TYPE_COMPUTE, "InstancedGpuCullingConstants")) {
    var->Set(instanced_gpu_culling_cb_);
  }

  const auto srb_start = core::SteadyClock::now();
  culling_pso->CreateShaderResourceBinding(&culling_srb, true);
  recordResourceCreation("instancing",
                         "instanced gpu LOD culling SRB",
                         srb_start,
                         core::SteadyClock::now());
  if (!culling_srb) {
    return false;
  }

  source_var = culling_srb->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE,
                                              "g_SourceInstances");
  const char* visible_names[] = {
      "g_VisibleInstances0",
      "g_VisibleInstances1",
      "g_VisibleInstances2",
      "g_VisibleInstances3",
  };
  const char* args_names[] = {
      "g_DrawArgs0",
      "g_DrawArgs1",
      "g_DrawArgs2",
      "g_DrawArgs3",
  };
  for (size_t i = 0; i < kInstancedGpuLodBucketCapacity; ++i) {
    visible_vars[i] =
        culling_srb->GetVariableByName(
            Diligent::SHADER_TYPE_COMPUTE,
            visible_names[i]);
    args_vars[i] =
        culling_srb->GetVariableByName(
            Diligent::SHADER_TYPE_COMPUTE,
            args_names[i]);
    if (!visible_vars[i] || !args_vars[i]) {
      return false;
    }
  }
  return source_var != nullptr;
}

bool DiligentBackend::ensureInstancedGpuCullingOutputBuffers(
    rendering::InstanceGpuLayout layout,
    size_t instance_count,
    Diligent::RefCntAutoPtr<Diligent::IBuffer>& visible_buffer,
    Diligent::RefCntAutoPtr<Diligent::IBufferView>& visible_uav,
    size_t& visible_buffer_capacity_bytes,
    rendering::InstanceGpuLayout& visible_buffer_layout,
    Diligent::RefCntAutoPtr<Diligent::IBuffer>& indirect_args_buffer,
    Diligent::RefCntAutoPtr<Diligent::IBufferView>& indirect_args_uav) {
  if (instance_count == 0u || !device_) {
    return false;
  }
  const auto& draw_caps = device_->GetAdapterInfo().DrawCommand;
  if ((draw_caps.CapFlags & Diligent::DRAW_COMMAND_CAP_FLAG_DRAW_INDIRECT) == 0) {
    return false;
  }

  const size_t stride = rendering::instanceGpuLayoutStride(layout);
  const size_t required_bytes = instance_count * stride;
  if (required_bytes == 0u ||
      required_bytes > static_cast<size_t>(std::numeric_limits<Diligent::Uint32>::max())) {
    return false;
  }
  if (visible_buffer_layout != layout) {
    visible_buffer.Release();
    visible_uav.Release();
    visible_buffer_capacity_bytes = 0u;
    visible_buffer_layout = layout;
  }
  if (!visible_buffer ||
      !visible_uav ||
      visible_buffer_capacity_bytes < required_bytes) {
    const size_t next_capacity =
        std::max(required_bytes,
                 visible_buffer_capacity_bytes > 0u
                     ? visible_buffer_capacity_bytes * 2u
                     : static_cast<size_t>(128u) * stride);
    Diligent::BufferDesc desc{};
    desc.Name = "Karma GPU-Culled Instance Buffer";
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = Diligent::BIND_VERTEX_BUFFER | Diligent::BIND_UNORDERED_ACCESS;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
    desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = static_cast<Diligent::Uint32>(stride);
    desc.Size = static_cast<Diligent::Uint64>(next_capacity);
    visible_buffer.Release();
    visible_uav.Release();
    const auto buffer_start = core::SteadyClock::now();
    device_->CreateBuffer(desc, nullptr, &visible_buffer);
    recordResourceCreation("instancing",
                           "gpu culled instance buffer",
                           buffer_start,
                           core::SteadyClock::now());
    if (!visible_buffer) {
      visible_buffer_capacity_bytes = 0u;
      return false;
    }
    visible_uav = visible_buffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS);
    if (!visible_uav) {
      visible_buffer.Release();
      visible_buffer_capacity_bytes = 0u;
      return false;
    }
    visible_buffer_capacity_bytes = next_capacity;
  }

  if (!indirect_args_buffer || !indirect_args_uav) {
    Diligent::BufferDesc desc{};
    desc.Name = "Karma Instanced GPU Culling Indirect Args";
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = Diligent::BIND_UNORDERED_ACCESS |
                     Diligent::BIND_INDIRECT_DRAW_ARGS;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_NONE;
    desc.Mode = Diligent::BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride =
        static_cast<Diligent::Uint32>(sizeof(InstancedIndexedIndirectArgs));
    desc.Size = sizeof(InstancedIndexedIndirectArgs);
    indirect_args_buffer.Release();
    indirect_args_uav.Release();
    const auto buffer_start = core::SteadyClock::now();
    device_->CreateBuffer(desc, nullptr, &indirect_args_buffer);
    recordResourceCreation("instancing",
                           "gpu culling indirect args",
                           buffer_start,
                           core::SteadyClock::now());
    if (!indirect_args_buffer) {
      return false;
    }
    indirect_args_uav =
        indirect_args_buffer->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS);
    if (!indirect_args_uav) {
      indirect_args_buffer.Release();
      return false;
    }
  }
  return true;
}

bool DiligentBackend::ensureInstancedGpuCullingRecordBuffers(
    InstancedRecord& record,
    const InstanceSetRecord& instance_set) {
  if (!instancedGpuCullingEnabled() ||
      instance_set.dynamic ||
      instance_set.instanceCount() == 0u ||
      !device_) {
    return false;
  }
  return ensureInstancedGpuCullingOutputBuffers(
      instance_set.gpu_layout,
      instance_set.instanceCount(),
      record.gpu_culled_instance_buffer,
      record.gpu_culled_instance_uav,
      record.gpu_culled_instance_buffer_capacity_bytes,
      record.gpu_culled_layout,
      record.gpu_culling_indirect_args_buffer,
      record.gpu_culling_indirect_args_uav);
}

void DiligentBackend::collectForwardLayerState(rendering::LayerId layer,
                                               const glm::mat4& view_proj,
                                               const glm::vec3& camera_position,
                                               const glm::vec3& camera_forward,
                                               bool is_gl,
                                               ForwardLayerState& out_state,
                                               ForwardLayerStats& out_stats) const {
  struct ForwardBatchKeyHash {
    size_t operator()(const ForwardBatchKey& key) const noexcept {
      size_t h = static_cast<size_t>(key.mesh);
      h ^= static_cast<size_t>(key.material) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= static_cast<size_t>(key.index_offset) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= static_cast<size_t>(key.index_count) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= static_cast<size_t>(key.indexed ? 1u : 0u) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= static_cast<size_t>(key.deformed ? 1u : 0u) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= static_cast<size_t>(key.gpu_layout) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= static_cast<size_t>(key.render_mode) + 0x9e3779b9 + (h << 6) + (h >> 2);
      return h;
    }
  };

  static thread_local std::vector<std::vector<rendering::InstanceData>>
      opaque_instance_vector_pool;
  for (ForwardBatch& batch : out_state.opaque_batches) {
    if (batch.instances.capacity() == 0u) {
      continue;
    }
    batch.instances.clear();
    opaque_instance_vector_pool.push_back(std::move(batch.instances));
  }
  out_state.opaque_batches.clear();
  out_state.deformed_opaque_draws.clear();
  out_state.scene_reflection_draws.clear();
  out_state.transparent_draws.clear();
  out_state.pre_particle_scene_sample_draws.clear();
  out_state.post_particle_draws.clear();
  out_state.opaque_batches.reserve(instances_.size() + instanced_records_.size());
  out_state.deformed_opaque_draws.reserve(instances_.size() / 4 + 1);
  out_state.scene_reflection_draws.reserve(instances_.size() / 4 + 1);
  out_state.transparent_draws.reserve(instances_.size());
  out_state.pre_particle_scene_sample_draws.reserve(instances_.size() / 4 + 1);
  out_state.post_particle_draws.reserve(instances_.size() / 4 + 1);
  out_stats = ForwardLayerStats{};

  struct ForwardBatchLookupValue {
    size_t index = 0u;
    uint64_t generation = 0u;
  };
  static thread_local std::unordered_map<ForwardBatchKey,
                                         ForwardBatchLookupValue,
                                         ForwardBatchKeyHash>
      opaque_batch_lookup;
  static thread_local uint64_t opaque_batch_lookup_generation = 0u;
  ++opaque_batch_lookup_generation;
  if (opaque_batch_lookup_generation == 0u) {
    opaque_batch_lookup.clear();
    opaque_batch_lookup_generation = 1u;
  }
  opaque_batch_lookup.reserve(instances_.size() + instanced_records_.size());

  auto append_opaque_forward_batch = [&](const ForwardBatchKey& key,
                                         const rendering::InstanceData& instance,
                                         rendering::DeformationId deformation) {
    if (key.deformed) {
      out_state.deformed_opaque_draws.push_back(DeformedForwardDraw{
          .key = key,
          .transform = instance.transform,
          .params = instance.params,
          .deformation = deformation,
      });
      return;
    }
    auto lookup_it = opaque_batch_lookup.find(key);
    if (lookup_it == opaque_batch_lookup.end() ||
        lookup_it->second.generation != opaque_batch_lookup_generation) {
      const size_t idx = out_state.opaque_batches.size();
      ForwardBatch batch{.key = key};
      if (!opaque_instance_vector_pool.empty()) {
        batch.instances = std::move(opaque_instance_vector_pool.back());
        opaque_instance_vector_pool.pop_back();
      }
      out_state.opaque_batches.push_back(std::move(batch));
      if (lookup_it == opaque_batch_lookup.end()) {
        lookup_it = opaque_batch_lookup
                        .emplace(key,
                                 ForwardBatchLookupValue{
                                     .index = idx,
                                     .generation = opaque_batch_lookup_generation,
                                 })
                        .first;
      } else {
        lookup_it->second = ForwardBatchLookupValue{
            .index = idx,
            .generation = opaque_batch_lookup_generation,
        };
      }
    }
    out_state.opaque_batches[lookup_it->second.index].instances.push_back(instance);
  };

  auto append_persistent_instanced_batch = [&](const ForwardBatchKey& key,
                                              rendering::InstanceId instance,
                                              Diligent::Uint32 instance_count,
                                              uint32_t lod_index = UINT32_MAX) {
    ForwardBatch batch{};
    batch.key = key;
    batch.instanced_record = instance;
    batch.persistent_instance_count = instance_count;
    batch.instanced_lod_index = lod_index;
    out_state.opaque_batches.push_back(std::move(batch));
  };

  auto resolve_bound_material =
      [&](const std::vector<rendering::DrawMaterialBinding>& materials,
          rendering::MaterialId material,
          uint32_t material_slot,
          rendering::MaterialId fallback_material) -> rendering::MaterialId {
    for (const auto& binding : materials) {
      if (binding.slot == material_slot &&
          binding.material != rendering::kInvalidMaterial) {
        return binding.material;
      }
    }
    if (material != rendering::kInvalidMaterial) {
      return material;
    }
    return fallback_material;
  };

  auto lookup_material = [&](rendering::MaterialId material_id) -> const MaterialRecord* {
    if (material_id == rendering::kInvalidMaterial) {
      return nullptr;
    }
    auto mat_it = materials_.find(material_id);
    return mat_it != materials_.end() ? &mat_it->second : nullptr;
  };

  auto uses_transparent_forward_path = [](const MaterialRecord* mat,
                                          const MeshRecord& mesh) {
    if (mat) {
      return mat->desc.transparent ||
             mat->desc.alpha_mode == rendering::MaterialDesc::AlphaMode::Blend ||
             (mat->shading_model == MaterialPipelineKind::Standard &&
              mat->transmission_factor > 0.001f);
    }
    return mesh.base_color.a < 0.999f;
  };

  auto uses_post_particle_transparent_pass = [&](const MaterialRecord* mat) {
    if (!mat) {
      return false;
    }
    return mat->shading_model == MaterialPipelineKind::EnergyShell ||
           mat->shading_model == MaterialPipelineKind::SphereHalo ||
           mat->shading_model == MaterialPipelineKind::ScreenWave ||
           mat->shading_model == MaterialPipelineKind::SphereGlowVolume ||
           mat->shading_model == MaterialPipelineKind::VolumetricSolid;
  };

  auto uses_pre_particle_scene_sample_pass = [&](const MaterialRecord* mat) {
    if (!mat) {
      return false;
    }
    return mat->shading_model == MaterialPipelineKind::WaveVolume ||
           (mat->shading_model == MaterialPipelineKind::Standard &&
            mat->transmission_factor > 0.001f);
  };

  auto select_transparent_draws = [&](const MaterialRecord* mat,
                                      bool requires_scene_sample,
                                      bool post_particle_scene_sample)
      -> std::vector<TransparentForwardDraw>& {
    if (requires_scene_sample) {
      return post_particle_scene_sample ? out_state.post_particle_draws
                                        : out_state.pre_particle_scene_sample_draws;
    }
    return uses_pre_particle_scene_sample_pass(mat)
               ? out_state.pre_particle_scene_sample_draws
               : (uses_post_particle_transparent_pass(mat)
                      ? out_state.post_particle_draws
                      : out_state.transparent_draws);
  };

  auto scene_sample_mode_for = [](const InstanceRecord& instance) {
    return instance.requires_scene_sample ? TransparentForwardDraw::SceneSampleMode::Required
                                          : TransparentForwardDraw::SceneSampleMode::None;
  };

  auto uses_scene_reflection_overlay = [&](const MaterialRecord* mat) {
    if (!mat || mat->shading_model != MaterialPipelineKind::Standard) {
      return false;
    }
    return mat->clearcoat_factor > 0.001f ||
           (mat->metallic_factor > 0.45f && mat->roughness_factor < 0.82f);
  };

  auto resolve_transparent_sort_depth = [&](const MaterialRecord* mat,
                                            const MeshRecord& mesh,
                                            const glm::mat4& transform) {
    glm::vec3 world_center =
        mesh.bounds_radius > 0.0f
            ? glm::vec3(transform * glm::vec4(mesh.bounds_center, 1.0f))
            : glm::vec3(transform[3]);
    if (mat && mat->shading_model == MaterialPipelineKind::VolumetricSolid) {
      world_center = mat->volume_center;
    }
    return glm::dot(world_center - camera_position, camera_forward);
  };

  for (const auto& entry : instances_) {
    const auto& instance = entry.second;
    if (instance.layer != layer) {
      out_stats.skipped_layer += 1;
      continue;
    }
    if (!instance.visible) {
      out_stats.skipped_hidden += 1;
      continue;
    }
    auto base_mesh_it = meshes_.find(instance.mesh);
    if (base_mesh_it == meshes_.end()) {
      out_stats.skipped_missing_mesh += 1;
      continue;
    }
    const MeshRecord& base_mesh = base_mesh_it->second;
    rendering::MeshId selected_mesh = instance.mesh;
    rendering::MaterialId selected_material = instance.material;
    const std::vector<rendering::DrawMaterialBinding>* selected_materials =
        &instance.materials;
    rendering::LodRenderMode selected_render_mode = rendering::LodRenderMode::Mesh;
    const glm::vec3 base_world_center =
        base_mesh.bounds_radius > 0.0f
            ? glm::vec3(instance.transform *
                        glm::vec4(base_mesh.bounds_center, 1.0f))
            : glm::vec3(instance.transform[3]);
    const float distance_to_camera = glm::length(base_world_center - camera_position);
    const auto lod_is_renderable = [&](const auto& lod) {
      const auto candidate = meshes_.find(lod.mesh);
      return lod.mesh != rendering::kInvalidMesh &&
             candidate != meshes_.end() && candidate->second.vertex_buffer;
    };
    const size_t selected_lod_bucket =
        rendering::detail::selectRenderableLodBucket(
            distance_to_camera, instance.lods, lod_is_renderable);
    if (selected_lod_bucket != 0u) {
      const auto& lod = instance.lods[selected_lod_bucket - 1u];
      selected_mesh = lod.mesh;
      selected_material = lod.material;
      selected_materials = &lod.materials;
      selected_render_mode = lod.render_mode;
    }
    auto mesh_it = meshes_.find(selected_mesh);
    if (mesh_it == meshes_.end()) {
      out_stats.skipped_missing_mesh += 1;
      continue;
    }
    const auto& mesh = mesh_it->second;
    if (!mesh.vertex_buffer) {
      out_stats.skipped_missing_vb += 1;
      continue;
    }
    if (mesh.bounds_radius > 0.0f) {
      const glm::vec4 world_bounds_sphere =
          transformBoundingSphere(instance.transform, mesh.bounds_center, mesh.bounds_radius);
      if (!sphereIntersectsClipVolume(view_proj, world_bounds_sphere, is_gl)) {
        out_stats.skipped_hidden += 1;
        continue;
      }
    }

    const rendering::InstanceData submitted_instance{
        .transform = instance.transform,
        .params = instance.params,
    };
    const bool indexed_mesh = mesh.index_buffer && mesh.index_count > 0;
    if (!mesh.submeshes.empty()) {
      for (size_t submesh_index = 0; submesh_index < mesh.submeshes.size(); ++submesh_index) {
        const auto& submesh = mesh.submeshes[submesh_index];
        const rendering::MaterialId mat_id =
            resolve_bound_material(*selected_materials,
                                   selected_material,
                                   submesh.material_slot,
                                   submesh.material);
        const ForwardBatchKey key{
            .mesh = selected_mesh,
            .material = mat_id,
            .index_offset = submesh.index_offset,
            .index_count = submesh.index_count,
            .indexed = indexed_mesh && submesh.index_count > 0,
            .deformed = instance.deformation != rendering::kInvalidDeformation,
            .render_mode = selected_render_mode,
        };
        const MaterialRecord* mat = lookup_material(mat_id);
        const bool transparent = uses_transparent_forward_path(mat, mesh);
        if (transparent) {
          auto& target_draws = select_transparent_draws(mat,
                                                        instance.requires_scene_sample,
                                                        instance.post_particle_scene_sample);
          target_draws.push_back(TransparentForwardDraw{
              .key = key,
              .transform = instance.transform,
              .params = instance.params,
              .volume_params = instance.volume_params,
              .has_volume_params = instance.has_volume_params,
              .deformation = instance.deformation,
              .depth = instance.has_volume_params
                           ? glm::dot(instance.volume_params.center - camera_position,
                                      camera_forward)
                           : resolve_transparent_sort_depth(mat, mesh, instance.transform),
              .scene_sample_mode = scene_sample_mode_for(instance),
          });
        } else {
          append_opaque_forward_batch(key, submitted_instance, instance.deformation);
          if (uses_scene_reflection_overlay(mat)) {
            out_state.scene_reflection_draws.push_back(TransparentForwardDraw{
                .key = key,
                .transform = instance.transform,
                .params = instance.params,
                .deformation = instance.deformation,
                .depth = resolve_transparent_sort_depth(mat, mesh, instance.transform),
                .scene_sample_mode = TransparentForwardDraw::SceneSampleMode::ReflectionOverlay,
            });
          }
        }
      }
    } else {
      const rendering::MaterialId mat_id =
          resolve_bound_material(*selected_materials,
                                 selected_material,
                                 0,
                                 rendering::kInvalidMaterial);
      const ForwardBatchKey key{
          .mesh = selected_mesh,
          .material = mat_id,
          .index_offset = 0,
          .index_count = mesh.index_count,
          .indexed = indexed_mesh,
          .deformed = instance.deformation != rendering::kInvalidDeformation,
          .render_mode = selected_render_mode,
      };
      const MaterialRecord* mat = lookup_material(mat_id);
      const bool transparent = uses_transparent_forward_path(mat, mesh);
      if (transparent) {
        auto& target_draws = select_transparent_draws(mat,
                                                      instance.requires_scene_sample,
                                                      instance.post_particle_scene_sample);
        target_draws.push_back(TransparentForwardDraw{
            .key = key,
            .transform = instance.transform,
            .params = instance.params,
            .volume_params = instance.volume_params,
            .has_volume_params = instance.has_volume_params,
            .deformation = instance.deformation,
            .depth = instance.has_volume_params
                         ? glm::dot(instance.volume_params.center - camera_position,
                                    camera_forward)
                         : resolve_transparent_sort_depth(mat, mesh, instance.transform),
            .scene_sample_mode = scene_sample_mode_for(instance),
        });
      } else {
        append_opaque_forward_batch(key, submitted_instance, instance.deformation);
        if (uses_scene_reflection_overlay(mat)) {
          out_state.scene_reflection_draws.push_back(TransparentForwardDraw{
              .key = key,
              .transform = instance.transform,
              .params = instance.params,
              .deformation = instance.deformation,
              .depth = resolve_transparent_sort_depth(mat, mesh, instance.transform),
              .scene_sample_mode = TransparentForwardDraw::SceneSampleMode::ReflectionOverlay,
          });
        }
      }
    }
  }

  for (const auto& entry : instanced_records_) {
    const rendering::InstanceId record_id = entry.first;
    const auto& record = entry.second;
    const auto source_it = instance_sets_.find(record.instance_set);
    if (source_it == instance_sets_.end()) {
      out_stats.skipped_missing_mesh += 1;
      continue;
    }
    const auto& instance_set = source_it->second;
    if (record.layer != layer) {
      out_stats.skipped_layer += 1;
      continue;
    }
    const size_t record_instance_count = instance_set.instanceCount();
    if (!record.visible || record_instance_count == 0u) {
      out_stats.skipped_hidden += 1;
      if (record_instance_count > 0u) {
        out_stats.instanced_culled_batches += 1u;
      }
      continue;
    }
    auto mesh_it = meshes_.find(record.mesh);
    if (mesh_it == meshes_.end()) {
      out_stats.skipped_missing_mesh += 1;
      continue;
    }
    const auto& mesh = mesh_it->second;
    if (!mesh.vertex_buffer) {
      out_stats.skipped_missing_vb += 1;
      continue;
    }
    if (record.bounds_valid) {
      const glm::vec4 bounds(record.bounds_center, record.bounds_radius);
      if (!sphereIntersectsClipVolume(view_proj, bounds, is_gl)) {
        out_stats.skipped_hidden += 1;
        out_stats.instanced_culled_batches += 1u;
        continue;
      }
    }

    auto level_uses_transparency =
        [&](const MeshRecord& level_mesh,
            const std::vector<rendering::DrawMaterialBinding>& level_materials,
            rendering::MaterialId level_material) {
      if (level_mesh.submeshes.empty()) {
        const rendering::MaterialId material_id = resolve_bound_material(
            level_materials, level_material, 0u, rendering::kInvalidMaterial);
        return uses_transparent_forward_path(lookup_material(material_id), level_mesh);
      }
      return std::any_of(
          level_mesh.submeshes.begin(),
          level_mesh.submeshes.end(),
          [&](const auto& submesh) {
            const rendering::MaterialId material_id = resolve_bound_material(
                level_materials,
                level_material,
                submesh.material_slot,
                submesh.material);
            return uses_transparent_forward_path(lookup_material(material_id), level_mesh);
          });
    };
    const bool indirect_draw_supported =
        (device_->GetAdapterInfo().DrawCommand.CapFlags &
         Diligent::DRAW_COMMAND_CAP_FLAG_DRAW_INDIRECT) != 0;
    const bool camera_shader_override =
        !camera_.shader_override_vertex_path.empty() &&
        !camera_.shader_override_fragment_path.empty();
    const auto lod_is_renderable = [&](const auto& lod) {
      const auto lod_mesh_it = meshes_.find(lod.mesh);
      return lod.mesh != rendering::kInvalidMesh &&
             lod_mesh_it != meshes_.end() &&
             lod_mesh_it->second.vertex_buffer;
    };
    bool use_cpu_lod_classification =
        !record.lods.empty() &&
        (instance_set.dynamic || !instancedGpuCullingEnabled() ||
         !indirect_draw_supported || camera_shader_override ||
         !mesh.index_buffer || mesh.index_count == 0u ||
         rendering::detail::hasUnrenderableLod(record.lods,
                                               lod_is_renderable));
    if (!record.lods.empty() &&
        level_uses_transparency(mesh, record.materials, record.material)) {
      use_cpu_lod_classification = true;
    }
    if (!record.lods.empty() && !use_cpu_lod_classification) {
      for (const auto& lod : record.lods) {
        const auto lod_mesh_it = meshes_.find(lod.mesh);
        if (lod_mesh_it != meshes_.end() &&
            ((!lod_mesh_it->second.index_buffer ||
              lod_mesh_it->second.index_count == 0u) ||
             level_uses_transparency(lod_mesh_it->second,
                                     lod.materials,
                                     lod.material))) {
          use_cpu_lod_classification = true;
          break;
        }
      }
    }
    if (use_cpu_lod_classification) {
      auto emit_classified_instance = [&](const glm::mat4& source_transform,
                                          const glm::vec4& params) {
        const glm::mat4 transform = source_transform * record.batch_transform;
        const glm::vec3 base_center =
            glm::vec3(transform * glm::vec4(mesh.bounds_center, 1.0f));
        const float distance_to_camera = glm::length(base_center - camera_position);
        rendering::MeshId selected_mesh_id = record.mesh;
        rendering::MaterialId selected_material = record.material;
        const std::vector<rendering::DrawMaterialBinding>* selected_materials =
            &record.materials;
        rendering::LodRenderMode selected_render_mode = rendering::LodRenderMode::Mesh;
        const size_t selected_lod_bucket =
            rendering::detail::selectRenderableLodBucket(
                distance_to_camera, record.lods, lod_is_renderable);
        if (selected_lod_bucket != 0u) {
          const auto& lod = record.lods[selected_lod_bucket - 1u];
          selected_mesh_id = lod.mesh;
          selected_material = lod.material;
          selected_materials = &lod.materials;
          selected_render_mode = lod.render_mode;
        }
        const auto selected_mesh_it = meshes_.find(selected_mesh_id);
        if (selected_mesh_it == meshes_.end() ||
            !selected_mesh_it->second.vertex_buffer) {
          return;
        }
        const MeshRecord& selected_mesh = selected_mesh_it->second;
        const bool selected_indexed =
            selected_mesh.index_buffer && selected_mesh.index_count > 0u;
        auto emit_range = [&](uint32_t material_slot,
                              Diligent::Uint32 index_offset,
                              Diligent::Uint32 index_count,
                              bool indexed,
                              rendering::MaterialId fallback_material) {
          const rendering::MaterialId material_id = resolve_bound_material(
              *selected_materials,
              selected_material,
              material_slot,
              fallback_material);
          const MaterialRecord* material = lookup_material(material_id);
          const ForwardBatchKey key{
              .mesh = selected_mesh_id,
              .material = material_id,
              .index_offset = index_offset,
              .index_count = index_count,
              .indexed = indexed,
              .deformed = false,
              .gpu_layout = rendering::InstanceGpuLayout::Matrix4x4Params,
              .render_mode = selected_render_mode,
          };
          if (uses_transparent_forward_path(material, selected_mesh)) {
            auto& target_draws = uses_pre_particle_scene_sample_pass(material)
                                     ? out_state.pre_particle_scene_sample_draws
                                     : (uses_post_particle_transparent_pass(material)
                                            ? out_state.post_particle_draws
                                            : out_state.transparent_draws);
            target_draws.push_back(TransparentForwardDraw{
                .key = key,
                .transform = transform,
                .params = params,
                .deformation = rendering::kInvalidDeformation,
                .depth = resolve_transparent_sort_depth(material,
                                                        selected_mesh,
                                                        transform),
            });
            return;
          }
          append_opaque_forward_batch(
              key,
              rendering::InstanceData{.transform = transform, .params = params},
              rendering::kInvalidDeformation);
          if (uses_scene_reflection_overlay(material)) {
            out_state.scene_reflection_draws.push_back(TransparentForwardDraw{
                .key = key,
                .transform = transform,
                .params = params,
                .deformation = rendering::kInvalidDeformation,
                .depth = resolve_transparent_sort_depth(material,
                                                        selected_mesh,
                                                        transform),
                .scene_sample_mode =
                    TransparentForwardDraw::SceneSampleMode::ReflectionOverlay,
            });
          }
        };
        if (!selected_mesh.submeshes.empty()) {
          for (const auto& submesh : selected_mesh.submeshes) {
            emit_range(submesh.material_slot,
                       submesh.index_offset,
                       submesh.index_count,
                       selected_indexed && submesh.index_count > 0u,
                       submesh.material);
          }
        } else {
          emit_range(0u,
                     0u,
                     selected_mesh.index_count,
                     selected_indexed,
                     rendering::kInvalidMaterial);
        }
      };
      if (instance_set.gpu_layout ==
          rendering::InstanceGpuLayout::PositionYawScaleParams) {
        for (const auto& instance : instance_set.planar_instances) {
          emit_classified_instance(planarInstanceTransform(instance), instance.params);
        }
      } else {
        for (const auto& instance : instance_set.instances) {
          emit_classified_instance(instance.transform, instance.params);
        }
      }
      continue;
    }

    const bool indexed_mesh = mesh.index_buffer && mesh.index_count > 0;
    auto emit_instanced_submesh = [&](uint32_t material_slot,
                                      Diligent::Uint32 index_offset,
                                      Diligent::Uint32 index_count,
                                      bool indexed,
                                      rendering::MaterialId fallback_material) {
      const rendering::MaterialId mat_id = resolve_bound_material(
          record.materials, record.material, material_slot, fallback_material);
      const ForwardBatchKey key{
          .mesh = record.mesh,
          .material = mat_id,
          .index_offset = index_offset,
          .index_count = index_count,
          .indexed = indexed,
          .deformed = false,
          .gpu_layout = instance_set.gpu_layout,
      };
      const MaterialRecord* mat = lookup_material(mat_id);
      const bool transparent = uses_transparent_forward_path(mat, mesh);
      if (!transparent) {
        append_persistent_instanced_batch(
            key,
            record_id,
            static_cast<Diligent::Uint32>(
                std::min<size_t>(record_instance_count,
                                 std::numeric_limits<Diligent::Uint32>::max())));
        return;
      }

      auto& target_draws = uses_pre_particle_scene_sample_pass(mat)
                               ? out_state.pre_particle_scene_sample_draws
                               : (uses_post_particle_transparent_pass(mat)
                                      ? out_state.post_particle_draws
                                      : out_state.transparent_draws);
      auto append_transparent_instance = [&](const glm::mat4& transform,
                                             const glm::vec4& params) {
        target_draws.push_back(TransparentForwardDraw{
            .key = ForwardBatchKey{
                .mesh = key.mesh,
                .material = key.material,
                .index_offset = key.index_offset,
                .index_count = key.index_count,
                .indexed = key.indexed,
                .deformed = false,
            },
            .transform = transform,
            .params = params,
            .deformation = rendering::kInvalidDeformation,
            .depth = resolve_transparent_sort_depth(mat, mesh, transform),
        });
      };
      if (instance_set.gpu_layout ==
          rendering::InstanceGpuLayout::PositionYawScaleParams) {
        for (const rendering::PlanarInstanceData& instance :
             instance_set.planar_instances) {
          append_transparent_instance(
              planarInstanceTransform(instance) * record.batch_transform,
              instance.params);
        }
      } else {
        for (const rendering::InstanceData& instance : instance_set.instances) {
          append_transparent_instance(instance.transform * record.batch_transform,
                                      instance.params);
        }
      }
    };

    if (!mesh.submeshes.empty()) {
      for (const auto& submesh : mesh.submeshes) {
        const rendering::MaterialId mat_id =
            resolve_bound_material(record.materials,
                                   record.material,
                                   submesh.material_slot,
                                   submesh.material);
        const ForwardBatchKey key{
            .mesh = record.mesh,
            .material = mat_id,
            .index_offset = submesh.index_offset,
            .index_count = submesh.index_count,
            .indexed = indexed_mesh && submesh.index_count > 0,
            .deformed = false,
            .gpu_layout = instance_set.gpu_layout,
        };
        const MaterialRecord* mat = lookup_material(mat_id);
        if (uses_transparent_forward_path(mat, mesh)) {
          emit_instanced_submesh(submesh.material_slot,
                                 submesh.index_offset,
                                 submesh.index_count,
                                 indexed_mesh && submesh.index_count > 0,
                                 submesh.material);
        } else {
          append_persistent_instanced_batch(
              key,
              record_id,
              static_cast<Diligent::Uint32>(
                  std::min<size_t>(record_instance_count,
                                   std::numeric_limits<Diligent::Uint32>::max())));
        }
      }
    } else {
      const rendering::MaterialId mat_id =
          resolve_bound_material(record.materials,
                                 record.material,
                                 0,
                                 rendering::kInvalidMaterial);
      const ForwardBatchKey key{
          .mesh = record.mesh,
          .material = mat_id,
          .index_offset = 0,
          .index_count = mesh.index_count,
          .indexed = indexed_mesh,
          .deformed = false,
          .gpu_layout = instance_set.gpu_layout,
      };
      const MaterialRecord* mat = lookup_material(mat_id);
      if (uses_transparent_forward_path(mat, mesh)) {
        if (instance_set.gpu_layout ==
            rendering::InstanceGpuLayout::PositionYawScaleParams) {
          for (const rendering::PlanarInstanceData& instance :
               instance_set.planar_instances) {
            const glm::mat4 transform =
                planarInstanceTransform(instance) * record.batch_transform;
            auto& target_draws = uses_pre_particle_scene_sample_pass(mat)
                                     ? out_state.pre_particle_scene_sample_draws
                                     : (uses_post_particle_transparent_pass(mat)
                                            ? out_state.post_particle_draws
                                            : out_state.transparent_draws);
            target_draws.push_back(TransparentForwardDraw{
                .key = ForwardBatchKey{
                    .mesh = key.mesh,
                    .material = key.material,
                    .index_offset = key.index_offset,
                    .index_count = key.index_count,
                    .indexed = key.indexed,
                    .deformed = false,
                },
                .transform = transform,
                .params = instance.params,
                .deformation = rendering::kInvalidDeformation,
                .depth = resolve_transparent_sort_depth(mat, mesh, transform),
            });
          }
        } else {
          for (const rendering::InstanceData& submitted_instance :
               instance_set.instances) {
            auto& target_draws = uses_pre_particle_scene_sample_pass(mat)
                                     ? out_state.pre_particle_scene_sample_draws
                                     : (uses_post_particle_transparent_pass(mat)
                                            ? out_state.post_particle_draws
                                            : out_state.transparent_draws);
            target_draws.push_back(TransparentForwardDraw{
                .key = ForwardBatchKey{
                    .mesh = key.mesh,
                    .material = key.material,
                    .index_offset = key.index_offset,
                    .index_count = key.index_count,
                    .indexed = key.indexed,
                    .deformed = false,
                },
                .transform = submitted_instance.transform * record.batch_transform,
                .params = submitted_instance.params,
                .deformation = rendering::kInvalidDeformation,
                .depth = resolve_transparent_sort_depth(
                    mat,
                    mesh,
                    submitted_instance.transform * record.batch_transform),
            });
          }
        }
      } else {
        append_persistent_instanced_batch(
            key,
            record_id,
            static_cast<Diligent::Uint32>(
                std::min<size_t>(record_instance_count,
                                 std::numeric_limits<Diligent::Uint32>::max())));
      }
    }

    for (uint32_t lod_index = 0;
         lod_index < static_cast<uint32_t>(record.lods.size());
         ++lod_index) {
      const auto& lod = record.lods[lod_index];
      if (lod.mesh == rendering::kInvalidMesh) {
        continue;
      }
      auto lod_mesh_it = meshes_.find(lod.mesh);
      if (lod_mesh_it == meshes_.end()) {
        continue;
      }
      const auto& lod_mesh = lod_mesh_it->second;
      if (!lod_mesh.vertex_buffer) {
        continue;
      }
      const bool lod_indexed_mesh = lod_mesh.index_buffer && lod_mesh.index_count > 0;
      auto emit_lod_submesh = [&](uint32_t material_slot,
                                  Diligent::Uint32 index_offset,
                                  Diligent::Uint32 index_count,
                                  bool indexed,
                                  rendering::MaterialId fallback_material) {
        const rendering::MaterialId mat_id = resolve_bound_material(
            lod.materials, lod.material, material_slot, fallback_material);
        const MaterialRecord* mat = lookup_material(mat_id);
        if (uses_transparent_forward_path(mat, lod_mesh)) {
          return;
        }
        const ForwardBatchKey key{
            .mesh = lod.mesh,
            .material = mat_id,
            .index_offset = index_offset,
            .index_count = index_count,
            .indexed = indexed,
            .deformed = false,
            .gpu_layout = instance_set.gpu_layout,
            .render_mode = lod.render_mode,
        };
        append_persistent_instanced_batch(
            key,
            record_id,
            static_cast<Diligent::Uint32>(
                std::min<size_t>(record_instance_count,
                                 std::numeric_limits<Diligent::Uint32>::max())),
            lod_index);
      };
      if (!lod_mesh.submeshes.empty()) {
        for (const auto& submesh : lod_mesh.submeshes) {
          emit_lod_submesh(submesh.material_slot,
                           submesh.index_offset,
                           submesh.index_count,
                           lod_indexed_mesh && submesh.index_count > 0,
                           submesh.material);
        }
      } else {
        emit_lod_submesh(0,
                         0,
                         lod_mesh.index_count,
                         lod_indexed_mesh,
                         rendering::kInvalidMaterial);
      }
    }
  }

  std::sort(out_state.opaque_batches.begin(),
            out_state.opaque_batches.end(),
            [](const ForwardBatch& a, const ForwardBatch& b) {
              if (a.key.material != b.key.material) {
                return a.key.material < b.key.material;
              }
              if (a.key.mesh != b.key.mesh) {
                return a.key.mesh < b.key.mesh;
              }
              if (a.key.index_offset != b.key.index_offset) {
                return a.key.index_offset < b.key.index_offset;
              }
              if (a.key.index_count != b.key.index_count) {
                return a.key.index_count < b.key.index_count;
              }
              if (a.key.indexed != b.key.indexed) {
                return static_cast<uint32_t>(a.key.indexed) < static_cast<uint32_t>(b.key.indexed);
              }
              if (a.key.gpu_layout != b.key.gpu_layout) {
                return static_cast<uint32_t>(a.key.gpu_layout) <
                       static_cast<uint32_t>(b.key.gpu_layout);
              }
              return static_cast<uint32_t>(a.key.render_mode) <
                     static_cast<uint32_t>(b.key.render_mode);
            });

  auto compare_transparent_draws = [&](const TransparentForwardDraw& a,
                                       const TransparentForwardDraw& b) {
    const bool finite_a = std::isfinite(a.depth);
    const bool finite_b = std::isfinite(b.depth);
    if (finite_a != finite_b) {
      return finite_a;
    }
    if (finite_a && a.depth != b.depth) {
      return a.depth > b.depth;
    }
    const MaterialRecord* mat_a = lookup_material(a.key.material);
    const MaterialRecord* mat_b = lookup_material(b.key.material);
    const bool additive_a =
        mat_a && mat_a->blend_mode == rendering::MaterialDesc::BlendMode::Additive;
    const bool additive_b =
        mat_b && mat_b->blend_mode == rendering::MaterialDesc::BlendMode::Additive;
    if (additive_a != additive_b) {
      return !additive_a && additive_b;
    }
    if (a.key.material != b.key.material) {
      return a.key.material < b.key.material;
    }
    if (a.key.mesh != b.key.mesh) {
      return a.key.mesh < b.key.mesh;
    }
    if (a.key.index_offset != b.key.index_offset) {
      return a.key.index_offset < b.key.index_offset;
    }
    return a.key.index_count < b.key.index_count;
  };

  std::sort(out_state.transparent_draws.begin(),
            out_state.transparent_draws.end(),
            compare_transparent_draws);
  std::sort(out_state.scene_reflection_draws.begin(),
            out_state.scene_reflection_draws.end(),
            compare_transparent_draws);
  std::sort(out_state.pre_particle_scene_sample_draws.begin(),
            out_state.pre_particle_scene_sample_draws.end(),
            compare_transparent_draws);
  std::sort(out_state.post_particle_draws.begin(),
            out_state.post_particle_draws.end(),
            compare_transparent_draws);

  const size_t lookup_retention_limit =
      std::max<size_t>(256u, out_state.opaque_batches.size() * 4u + 64u);
  if (opaque_batch_lookup.size() > lookup_retention_limit) {
    for (auto it = opaque_batch_lookup.begin(); it != opaque_batch_lookup.end();) {
      if (it->second.generation != opaque_batch_lookup_generation) {
        it = opaque_batch_lookup.erase(it);
      } else {
        ++it;
      }
    }
  }
}

Diligent::Uint32 DiligentBackend::renderOpaqueForwardLayer(
    const ForwardLayerState& state,
    const DrawConstants& base_constants,
    Diligent::IPipelineState* active_forward_pipeline,
    bool use_custom_shader_override,
    Diligent::ITextureView* active_rtv,
    Diligent::ITextureView* active_dsv,
    int render_width,
    int render_height,
    bool is_gl) {
  if (!active_forward_pipeline || !constants_) {
    return 0;
  }
  const bool draw_debug = [] {
    const char* value = std::getenv("KARMA_DRAW_DEBUG");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
  }();

  auto ensure_instance_buffer = [&](size_t instance_count) {
    if (instance_count == 0) {
      return false;
    }
    constexpr size_t kMaxInstanceCount =
        static_cast<size_t>(std::numeric_limits<Diligent::Uint32>::max()) /
        sizeof(InstanceGpuData);
    if (instance_count > kMaxInstanceCount) {
      return false;
    }
    if (instance_vb_ && instance_vb_capacity_ >= instance_count) {
      return true;
    }
    const size_t grown_capacity =
        instance_vb_capacity_ > 0u && instance_vb_capacity_ <= kMaxInstanceCount / 2u
            ? instance_vb_capacity_ * 2u
            : kMaxInstanceCount;
    const size_t new_capacity = std::min(
        kMaxInstanceCount,
        std::max(instance_count,
                 instance_vb_capacity_ > 0u ? grown_capacity : static_cast<size_t>(128u)));
    Diligent::BufferDesc ib_desc{};
    ib_desc.Name = "Karma Instance Buffer";
    ib_desc.Usage = Diligent::USAGE_DYNAMIC;
    ib_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    ib_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    ib_desc.Size = static_cast<Diligent::Uint32>(new_capacity * sizeof(InstanceGpuData));
    instance_vb_.Release();
    device_->CreateBuffer(ib_desc, nullptr, &instance_vb_);
    if (!instance_vb_) {
      return false;
    }
    instance_vb_capacity_ = new_capacity;
    return true;
  };

  auto is_valid_indexed_draw = [&](const MeshRecord& mesh,
                                   Diligent::Uint32 first_index,
                                   Diligent::Uint32 index_count) {
    if (!mesh.index_buffer || mesh.index_count == 0 || index_count == 0) {
      return false;
    }
    const uint64_t first = static_cast<uint64_t>(first_index);
    const uint64_t count = static_cast<uint64_t>(index_count);
    const uint64_t total = static_cast<uint64_t>(mesh.index_count);
    if (first >= total) {
      return false;
    }
    return first + count <= total;
  };

  auto lookup_material = [&](rendering::MaterialId material_id) -> MaterialRecord* {
    if (material_id == rendering::kInvalidMaterial) {
      return nullptr;
    }
    auto mat_it = materials_.find(material_id);
    return mat_it != materials_.end() ? &mat_it->second : nullptr;
  };

  thread_local std::vector<InstanceGpuData> fallback_packed_instances;
  auto pack_instances = [&](const std::vector<rendering::InstanceData>& instances) {
    fallback_packed_instances.clear();
    fallback_packed_instances.reserve(instances.size());
    for (const rendering::InstanceData& instance : instances) {
      fallback_packed_instances.push_back(packInstanceData(instance.transform, instance.params));
    }
  };

  auto update_forward_material_constants = [&](rendering::MaterialId material_id,
                                               rendering::MeshId mesh_id,
                                               const MeshRecord& mesh,
                                               const MaterialRecord* mat,
                                               rendering::InstanceGpuLayout layout,
                                               rendering::LodRenderMode render_mode,
                                               const glm::mat4& batch_transform,
                                               rendering::MaterialId& last_constants_material,
                                               rendering::MeshId& last_constants_mesh,
                                               rendering::InstanceGpuLayout& last_constants_layout,
                                               rendering::LodRenderMode&
                                                   last_constants_render_mode,
                                               glm::mat4& last_constants_batch_transform) -> bool {
    if (material_id == last_constants_material &&
        (material_id != rendering::kInvalidMaterial || mesh_id == last_constants_mesh) &&
        layout == last_constants_layout &&
        render_mode == last_constants_render_mode &&
        std::memcmp(glm::value_ptr(batch_transform),
                    glm::value_ptr(last_constants_batch_transform),
                    sizeof(glm::mat4)) == 0) {
      return true;
    }
    DrawConstants constants = base_constants;
    copyMat4(constants.instance_batch_transform, batch_transform);
    constants.instance_params[0] =
        layout == rendering::InstanceGpuLayout::PositionYawScaleParams ? 1.0f : 0.0f;
    constants.instance_params[1] =
        render_mode == rendering::LodRenderMode::UprightBillboard ? 1.0f : 0.0f;
    constants.instance_params[2] = 0.0f;
    constants.instance_params[3] = 0.0f;
    glm::vec4 base_color = mat ? mat->base_color_factor : mesh.base_color;
    if (!mat && base_color == glm::vec4(1.0f)) {
      base_color = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    }
    constants.base_color_factor[0] = base_color.r;
    constants.base_color_factor[1] = base_color.g;
    constants.base_color_factor[2] = base_color.b;
    constants.base_color_factor[3] = base_color.a;
    const glm::vec3 emissive = mat ? mat->emissive_factor : glm::vec3(0.0f);
    constants.emissive_factor[0] = emissive.x;
    constants.emissive_factor[1] = emissive.y;
    constants.emissive_factor[2] = emissive.z;
    constants.emissive_factor[3] = 1.0f;
    constants.pbr_params[0] = mat ? mat->metallic_factor : 1.0f;
    constants.pbr_params[1] = mat ? mat->roughness_factor : 1.0f;
    constants.pbr_params[2] = mat ? mat->occlusion_strength : 1.0f;
    constants.pbr_params[3] = mat ? mat->normal_scale : 1.0f;
    constants.material_params0[0] =
        mat ? static_cast<float>(static_cast<uint32_t>(mat->shading_model)) : 0.0f;
    constants.material_params0[1] = mat ? mat->shell_fresnel_power : 5.0f;
    constants.material_params0[2] = mat ? mat->shell_fresnel_strength : 1.0f;
    constants.material_params0[3] = mat ? mat->shell_refraction_strength : 0.08f;
    if (mat && (mat->shading_model == MaterialPipelineKind::WaveVolume ||
                mat->shading_model == MaterialPipelineKind::ScreenWave)) {
      constants.material_params1[0] = mat->wave_tint_strength;
      constants.material_params1[1] = mat->wave_distortion_strength;
      constants.material_params1[2] = mat->wave_edge_strength;
      constants.material_params1[3] = mat->wave_noise_strength;
    } else if (mat &&
               mat->shading_model == MaterialPipelineKind::VolumetricSolid) {
      constants.material_params0[1] = static_cast<float>(mat->volume_shape);
      constants.material_params0[2] = mat->volume_anisotropy;
      constants.material_params0[3] = mat->volume_absorption;
      constants.material_params1[0] = mat->volume_center.x;
      constants.material_params1[1] = mat->volume_center.y;
      constants.material_params1[2] = mat->volume_center.z;
      constants.material_params1[3] = mat->volume_radius;
      constants.material_params2[0] = mat->volume_axis_x.x;
      constants.material_params2[1] = mat->volume_axis_x.y;
      constants.material_params2[2] = mat->volume_axis_x.z;
      constants.material_params2[3] = mat->volume_capsule_half_length;
      constants.material_params3[0] = mat->volume_axis_y.x;
      constants.material_params3[1] = mat->volume_axis_y.y;
      constants.material_params3[2] = mat->volume_axis_y.z;
      constants.material_params3[3] = mat->volume_density;
      constants.material_params4[0] = mat->volume_axis_z.x;
      constants.material_params4[1] = mat->volume_axis_z.y;
      constants.material_params4[2] = mat->volume_axis_z.z;
      constants.material_params4[3] = mat->volume_scattering;
      constants.material_params5[0] = mat->volume_distortion_strength;
      constants.material_params5[1] = mat->volume_noise_strength;
      constants.material_params5[2] = 0.0f;
      constants.material_params5[3] = 0.0f;
    } else if (!mat || mat->shading_model == MaterialPipelineKind::Standard ||
               mat->shading_model == MaterialPipelineKind::Foliage) {
      constexpr bool kAlphaToCoverageActive = false;
      const bool alpha_to_coverage_requested = mat && mat->desc.alpha_to_coverage;
      constants.material_params1[0] = mat ? mat->desc.alpha_softness : 0.0f;
      constants.material_params1[1] = (mat && mat->desc.alpha_dither) ? 1.0f : 0.0f;
      constants.material_params1[2] = alpha_to_coverage_requested ? 1.0f : 0.0f;
      constants.material_params1[3] =
          (alpha_to_coverage_requested && kAlphaToCoverageActive) ? 1.0f : 0.0f;
    } else {
      constants.material_params1[0] = mat ? mat->shell_interior_strength : 0.4f;
      constants.material_params1[1] = mat ? mat->shell_highlight_strength : 1.0f;
      constants.material_params1[2] = mat ? mat->shell_alpha_boost : 0.0f;
      constants.material_params1[3] = mat ? mat->shell_swirl_strength : 0.0f;
    }
    if (mat && mat->shading_model == MaterialPipelineKind::SphereHalo) {
      constants.material_params2[0] = mat->screen_center_x;
      constants.material_params2[1] = mat->screen_center_y;
      constants.material_params2[2] = mat->screen_radius_x;
      constants.material_params2[3] = mat->screen_radius_y;
    } else if (mat && mat->shading_model == MaterialPipelineKind::ScreenWave) {
      constants.material_params2[0] = mat->screen_center_x;
      constants.material_params2[1] = mat->screen_center_y;
      constants.material_params2[2] = mat->screen_radius_x;
      constants.material_params2[3] = mat->screen_radius_y;
    } else if (mat &&
               mat->shading_model == MaterialPipelineKind::VolumetricSolid) {
      // Volumetric solids pack params 2-5 above with shape axes and optical data.
    } else {
      constants.material_params2[0] = (mat && mat->analytic_sphere_normals) ? 1.0f : 0.0f;
      constants.material_params2[1] = mat ? mat->shell_body_strength : 1.0f;
      constants.material_params2[2] = (mat && mat->desc.unlit) ? 1.0f : 0.0f;
      constants.material_params2[3] =
          (mat && mat->desc.alpha_mode == rendering::MaterialDesc::AlphaMode::Masked)
              ? mat->desc.alpha_cutoff
              : -1.0f;
    }
    if (!mat || mat->shading_model == MaterialPipelineKind::Standard ||
        mat->shading_model == MaterialPipelineKind::Foliage) {
      constants.material_params3[0] = mat ? mat->clearcoat_factor : 0.0f;
      constants.material_params3[1] = mat ? mat->clearcoat_roughness_factor : 0.0f;
      constants.material_params3[2] = mat ? mat->sheen_roughness_factor : 0.0f;
      constants.material_params3[3] = mat ? mat->anisotropy_factor : 0.0f;
      const glm::vec3 sheen_color = mat ? mat->sheen_color_factor : glm::vec3(0.0f);
      constants.material_params4[0] = sheen_color.r;
      constants.material_params4[1] = sheen_color.g;
      constants.material_params4[2] = sheen_color.b;
      constants.material_params4[3] = mat ? mat->transmission_factor : 0.0f;
      constants.material_params5[0] =
          (mat && std::isfinite(mat->ior)) ? std::max(mat->ior, 1.0f) : 1.5f;
      constants.material_params5[1] = mat ? mat->thickness_factor : 0.0f;
      constants.material_params5[2] =
          (mat && std::isfinite(mat->attenuation_distance)) ? mat->attenuation_distance : 0.0f;
      constants.material_params5[3] =
          (mat && mat->desc.normal_map_convention ==
                      rendering::MaterialDesc::NormalMapConvention::DirectX)
              ? -1.0f
              : 1.0f;
      const glm::vec3 attenuation_color = mat ? mat->attenuation_color : glm::vec3(1.0f);
      constants.material_params6[0] = attenuation_color.r;
      constants.material_params6[1] = attenuation_color.g;
      constants.material_params6[2] = attenuation_color.b;
      constants.material_params6[3] = 0.0f;
      const glm::vec3 specular_color = mat ? mat->specular_color_factor : glm::vec3(1.0f);
      constants.material_params7[0] = specular_color.r;
      constants.material_params7[1] = specular_color.g;
      constants.material_params7[2] = specular_color.b;
      constants.material_params7[3] = mat ? mat->specular_factor : 1.0f;
      const bool lightmap_ready =
          mat && mat->lightmap_enabled && mat->lightmap_srv &&
          mat->lightmap_srv.RawPtr() != default_lightmap_.RawPtr();
      constants.lightmap_params[0] = lightmap_ready ? 1.0f : 0.0f;
      constants.lightmap_params[1] = mat ? mat->lightmap_intensity : 1.0f;
      constants.lightmap_params[2] = 0.0f;
      constants.lightmap_params[3] = 0.0f;
      const glm::vec4 lightmap_uv =
          mat ? mat->lightmap_uv_scale_offset : glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
      constants.lightmap_uv_scale_offset[0] = lightmap_uv.x;
      constants.lightmap_uv_scale_offset[1] = lightmap_uv.y;
      constants.lightmap_uv_scale_offset[2] = lightmap_uv.z;
      constants.lightmap_uv_scale_offset[3] = lightmap_uv.w;
      constants.lightmap_mixed_mask[0] = mat ? mat->lightmap_mixed_mask_low : 0u;
      constants.lightmap_mixed_mask[1] = mat ? mat->lightmap_mixed_mask_high : 0u;
      constants.lightmap_mixed_mask[2] = 0u;
      constants.lightmap_mixed_mask[3] = 0u;
      if (mat) {
        for (size_t slot = 0; slot < MaterialRecord::kTextureCoordSlotCount; ++slot) {
          constants.texcoord_row0[slot][0] = mat->texcoord_row0[slot].x;
          constants.texcoord_row0[slot][1] = mat->texcoord_row0[slot].y;
          constants.texcoord_row0[slot][2] = mat->texcoord_row0[slot].z;
          constants.texcoord_row0[slot][3] = mat->texcoord_row0[slot].w;
          constants.texcoord_row1[slot][0] = mat->texcoord_row1[slot].x;
          constants.texcoord_row1[slot][1] = mat->texcoord_row1[slot].y;
          constants.texcoord_row1[slot][2] = mat->texcoord_row1[slot].z;
          constants.texcoord_row1[slot][3] = mat->texcoord_row1[slot].w;
        }
      } else {
        for (size_t slot = 0; slot < MaterialRecord::kTextureCoordSlotCount; ++slot) {
          constants.texcoord_row0[slot][0] = 1.0f;
          constants.texcoord_row0[slot][1] = 0.0f;
          constants.texcoord_row0[slot][2] = 0.0f;
          constants.texcoord_row0[slot][3] = 0.0f;
          constants.texcoord_row1[slot][0] = 0.0f;
          constants.texcoord_row1[slot][1] = 1.0f;
          constants.texcoord_row1[slot][2] = 0.0f;
          constants.texcoord_row1[slot][3] = 0.0f;
        }
      }
    }
    if (mat && materialUsesCustomForwardPipeline(*mat)) {
      auto copy_custom_param = [](const glm::vec4& value, float out[4]) {
        out[0] = value.x;
        out[1] = value.y;
        out[2] = value.z;
        out[3] = value.w;
      };
      if (mat->custom_material_param_enabled[0]) {
        copy_custom_param(mat->custom_material_params[0], constants.material_params0);
      }
      if (mat->custom_material_param_enabled[1]) {
        copy_custom_param(mat->custom_material_params[1], constants.material_params1);
      }
      if (mat->custom_material_param_enabled[2]) {
        copy_custom_param(mat->custom_material_params[2], constants.material_params2);
      }
      if (mat->custom_material_param_enabled[3]) {
        copy_custom_param(mat->custom_material_params[3], constants.material_params3);
      }
      if (mat->custom_material_param_enabled[4]) {
        copy_custom_param(mat->custom_material_params[4], constants.material_params4);
      }
      if (mat->custom_material_param_enabled[5]) {
        copy_custom_param(mat->custom_material_params[5], constants.material_params5);
      }
      if (mat->custom_material_param_enabled[6]) {
        copy_custom_param(mat->custom_material_params[6], constants.material_params6);
      }
    }
    {
      Diligent::MapHelper<DrawConstants> mapped(
          context_, constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
      auto* mapped_constants = getMappedData(mapped);
      if (mapped_constants == nullptr) {
        return false;
      }
      *mapped_constants = constants;
    }
    last_constants_material = material_id;
    last_constants_mesh = mesh_id;
    last_constants_layout = layout;
    last_constants_render_mode = render_mode;
    last_constants_batch_transform = batch_transform;
    return true;
  };

  auto resolve_forward_pipeline = [&](MaterialRecord* mat,
                                      rendering::InstanceGpuLayout layout,
                                      bool& custom_pipeline) -> Diligent::IPipelineState* {
    custom_pipeline = false;
    if (use_custom_shader_override) {
      return active_forward_pipeline;
    }
    const ForwardPipelineVariant variant =
        (mat && mat->desc.double_sided) ? ForwardPipelineVariant::OpaqueDoubleSided
                                        : ForwardPipelineVariant::Opaque;
    if (mat && materialUsesCustomForwardPipeline(*mat)) {
      if (Diligent::IPipelineState* custom =
              ensureCustomForwardPipeline(*mat, variant, layout)) {
        custom_pipeline = true;
        return custom;
      }
    }
    return ensureForwardPipeline(variant, layout);
  };

  auto resolve_forward_srb = [&](MaterialRecord* mat,
                                 bool custom_pipeline,
                                 rendering::InstanceGpuLayout layout) -> Diligent::IShaderResourceBinding* {
    if (use_custom_shader_override) {
      return camera_override_srb_;
    }
    const ForwardPipelineVariant variant =
        (mat && mat->desc.double_sided) ? ForwardPipelineVariant::OpaqueDoubleSided
                                        : ForwardPipelineVariant::Opaque;
    if (mat) {
      if (Diligent::IShaderResourceBinding* srb =
              ensureMaterialForwardSrb(*mat, variant, custom_pipeline, layout)) {
        return srb;
      }
    }
    if (editorWireframeViewEnabled()) {
      const size_t slot =
          forwardPipelineVariantIndex(variant) * kInstanceGpuLayoutCount +
          instanceGpuLayoutIndex(layout);
      auto& wireframe_srb = editor_wireframe_default_material_srbs_[slot];
      return wireframe_srb ? wireframe_srb.RawPtr() : shader_resources_.RawPtr();
    }
    if (layout == rendering::InstanceGpuLayout::PositionYawScaleParams) {
      auto& compact_srb = compact_default_material_srbs_[forwardPipelineVariantIndex(variant)];
      return compact_srb ? compact_srb.RawPtr() : shader_resources_.RawPtr();
    }
    if (variant == ForwardPipelineVariant::OpaqueDoubleSided &&
        opaque_double_sided_default_material_srb_) {
      return opaque_double_sided_default_material_srb_;
    }
    return default_material_srb_ ? default_material_srb_.RawPtr() : shader_resources_.RawPtr();
  };

  auto bind_forward_instance_buffer =
      [&](const MeshRecord& mesh,
          Diligent::IBuffer* instance_buffer,
          Diligent::IBuffer*& bound_mesh_vb,
          Diligent::IBuffer*& bound_instance_vb) -> bool {
    if (!instance_buffer) {
      return false;
    }

    Diligent::IBuffer* mesh_vb = mesh.vertex_buffer.RawPtr();
    Diligent::IBuffer* instance_vb = instance_buffer;
    if (mesh_vb != bound_mesh_vb || instance_vb != bound_instance_vb) {
      Diligent::IBuffer* vbs[] = {mesh.vertex_buffer.RawPtr(), instance_vb};
      Diligent::Uint64 offsets[] = {0, 0};
      context_->SetVertexBuffers(0,
                                 2,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
      bound_mesh_vb = mesh_vb;
      bound_instance_vb = instance_vb;
    }
    return true;
  };

  auto bind_forward_geometry =
      [&](const MeshRecord& mesh,
          const InstanceGpuData* instances,
          size_t instance_count,
          Diligent::IBuffer*& bound_mesh_vb,
          Diligent::IBuffer*& bound_instance_vb) -> bool {
    if (!ensure_instance_buffer(instance_count)) {
      return false;
    }
    if (!uploadInstanceData(context_, instance_vb_, instances, instance_count)) {
      return false;
    }
    instancing_stats_.instance_buffer_updates += 1u;
    instancing_stats_.instance_upload_bytes +=
        static_cast<uint64_t>(instance_count * sizeof(InstanceGpuData));
    return bind_forward_instance_buffer(
        mesh, instance_vb_.RawPtr(), bound_mesh_vb, bound_instance_vb);
  };

  auto draw_forward_batch = [&](const MeshRecord& mesh,
                                const ForwardBatchKey& key,
                                Diligent::Uint32 instance_count,
                                Diligent::Uint32 first_instance,
                                Diligent::IBuffer*& bound_index_buffer,
                                const char* pass_label) {
    auto log_draw = [&](const char* draw_kind) {
      if (!draw_debug) {
        return;
      }
      std::fprintf(stderr,
                   "[Karma][DrawDebug] pass=%s kind=%s mesh=%u material=%u "
                   "indexed=%s deformed=%s vertices=%u mesh_indices=%u "
                   "first_index=%u draw_indices=%u instances=%u vb=%s ib=%s\n",
                   pass_label,
                   draw_kind,
                   key.mesh,
                   key.material,
                   key.indexed ? "true" : "false",
                   key.deformed ? "true" : "false",
                   mesh.vertex_count,
                   mesh.index_count,
                   key.index_offset,
                   key.index_count,
                   instance_count,
                   mesh.vertex_buffer ? "yes" : "no",
                   mesh.index_buffer ? "yes" : "no");
      std::fflush(stderr);
    };
    if (key.indexed) {
      if (!is_valid_indexed_draw(mesh, key.index_offset, key.index_count)) {
        log_draw("invalid-indexed-skip");
        return false;
      }
      Diligent::IBuffer* index_buffer = mesh.index_buffer.RawPtr();
      if (index_buffer != bound_index_buffer) {
        context_->SetIndexBuffer(mesh.index_buffer,
                                 0,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_index_buffer = index_buffer;
      }
      Diligent::DrawIndexedAttribs indexed{};
      indexed.IndexType = Diligent::VT_UINT32;
      indexed.NumIndices = key.index_count;
      indexed.FirstIndexLocation = key.index_offset;
      indexed.NumInstances = instance_count;
      indexed.FirstInstanceLocation = first_instance;
      indexed.Flags = kHotPathDrawFlags;
      log_draw("indexed");
      context_->DrawIndexed(indexed);
      return true;
    }

    Diligent::DrawAttribs draw_attrs{};
    draw_attrs.NumVertices = mesh.vertex_count;
    draw_attrs.NumInstances = instance_count;
    draw_attrs.FirstInstanceLocation = first_instance;
    draw_attrs.Flags = kHotPathDrawFlags;
    log_draw("non-indexed");
    context_->Draw(draw_attrs);
    return true;
  };

  auto draw_gpu_culled_forward_batch =
      [&](InstancedRecord& record,
          InstanceSetRecord& instance_set,
          const MeshRecord& mesh,
          const ForwardBatchKey& key,
          Diligent::Uint32 instance_count,
          Diligent::IPipelineState* graphics_pipeline,
          Diligent::IShaderResourceBinding* graphics_srb,
          Diligent::IBuffer* visible_instance_buffer,
          Diligent::IBufferView* visible_instance_uav,
          Diligent::IBuffer* indirect_args_buffer,
          Diligent::IBufferView* indirect_args_uav,
          float distance_min,
          float distance_max,
          Diligent::IBuffer*& bound_mesh_vb,
          Diligent::IBuffer*& bound_instance_vb,
          Diligent::IBuffer*& bound_index_buffer) -> bool {
    if (use_custom_shader_override ||
        !key.indexed ||
        instance_set.dynamic ||
        !instance_set.instance_srv ||
        !instance_set.instance_buffer ||
        !visible_instance_buffer ||
        !visible_instance_uav ||
        !indirect_args_buffer ||
        !indirect_args_uav ||
        !graphics_pipeline ||
        !graphics_srb ||
        instance_count == 0u ||
        !ensureInstancedGpuCullingResources(instance_set.gpu_layout) ||
        !is_valid_indexed_draw(mesh, key.index_offset, key.index_count)) {
      return false;
    }
    const size_t layout_index = instanceGpuLayoutIndex(instance_set.gpu_layout);
    auto& culling_pso = instanced_gpu_culling_psos_[layout_index];
    auto& culling_srb = instanced_gpu_culling_srbs_[layout_index];

    InstancedIndexedIndirectArgs args{};
    args.num_indices = key.index_count;
    args.num_instances = 0u;
    args.first_index_location = key.index_offset;
    args.base_vertex = 0u;
    args.first_instance_location = 0u;
    context_->UpdateBuffer(indirect_args_buffer,
                           0,
                           sizeof(args),
                           &args,
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    InstancedGpuCullingConstants culling_constants{};
    std::memcpy(culling_constants.view_proj,
                base_constants.mvp,
                sizeof(culling_constants.view_proj));
    copyMat4(culling_constants.batch_transform, record.batch_transform);
    culling_constants.mesh_bounds[0] = mesh.bounds_center.x;
    culling_constants.mesh_bounds[1] = mesh.bounds_center.y;
    culling_constants.mesh_bounds[2] = mesh.bounds_center.z;
    culling_constants.mesh_bounds[3] = mesh.bounds_radius;
    const auto base_mesh_it = meshes_.find(record.mesh);
    const MeshRecord& lod_reference_mesh =
        base_mesh_it != meshes_.end() ? base_mesh_it->second : mesh;
    culling_constants.lod_reference_bounds[0] = lod_reference_mesh.bounds_center.x;
    culling_constants.lod_reference_bounds[1] = lod_reference_mesh.bounds_center.y;
    culling_constants.lod_reference_bounds[2] = lod_reference_mesh.bounds_center.z;
    culling_constants.lod_reference_bounds[3] = lod_reference_mesh.bounds_radius;
    culling_constants.camera_position[0] = base_constants.camera_pos[0];
    culling_constants.camera_position[1] = base_constants.camera_pos[1];
    culling_constants.camera_position[2] = base_constants.camera_pos[2];
    culling_constants.camera_position[3] = 1.0f;
    culling_constants.distance_params[0] = std::max(distance_min, 0.0f);
    culling_constants.distance_params[1] = std::max(distance_max, culling_constants.distance_params[0]);
    culling_constants.distance_params[2] = 0.0f;
    culling_constants.distance_params[3] = 0.0f;
    culling_constants.params[0] = instance_count;
    culling_constants.params[1] = key.index_count;
    culling_constants.params[2] = key.index_offset;
    culling_constants.params[3] = is_gl ? 1u : 0u;
    {
      Diligent::MapHelper<InstancedGpuCullingConstants> mapped(
          context_,
          instanced_gpu_culling_cb_,
          Diligent::MAP_WRITE,
          Diligent::MAP_FLAG_DISCARD);
      auto* mapped_constants = getMappedData(mapped);
      if (mapped_constants == nullptr) {
        return false;
      }
      *mapped_constants = culling_constants;
    }

    instanced_gpu_culling_source_vars_[layout_index]->Set(
        instance_set.instance_srv,
        Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    instanced_gpu_culling_visible_vars_[layout_index]->Set(
        visible_instance_uav,
        Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    instanced_gpu_culling_args_vars_[layout_index]->Set(
        indirect_args_uav,
        Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    context_->SetPipelineState(culling_pso);
    context_->CommitShaderResources(culling_srb,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::DispatchComputeAttribs dispatch{};
    dispatch.ThreadGroupCountX = (instance_count + 63u) / 64u;
    dispatch.ThreadGroupCountY = 1u;
    dispatch.ThreadGroupCountZ = 1u;
    context_->DispatchCompute(dispatch);
    instancing_stats_.gpu_culling_batches += 1u;
    instancing_stats_.gpu_culling_dispatches += 1u;
    instancing_stats_.gpu_culling_candidate_instances += instance_count;

    Diligent::StateTransitionDesc visible_transition{};
    visible_transition.pResource = visible_instance_buffer;
    visible_transition.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    visible_transition.NewState = Diligent::RESOURCE_STATE_VERTEX_BUFFER;
    visible_transition.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
    context_->TransitionResourceStates(1, &visible_transition);

    context_->SetPipelineState(graphics_pipeline);
    context_->CommitShaderResources(graphics_srb,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (!bind_forward_instance_buffer(mesh,
                                      visible_instance_buffer,
                                      bound_mesh_vb,
                                      bound_instance_vb)) {
      return false;
    }
    Diligent::IBuffer* index_buffer = mesh.index_buffer.RawPtr();
    if (index_buffer != bound_index_buffer) {
      context_->SetIndexBuffer(mesh.index_buffer,
                               0,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      bound_index_buffer = index_buffer;
    }

    Diligent::DrawIndexedIndirectAttribs indirect{};
    indirect.IndexType = Diligent::VT_UINT32;
    indirect.pAttribsBuffer = indirect_args_buffer;
    indirect.DrawArgsOffset = 0u;
    indirect.DrawArgsStride =
        static_cast<Diligent::Uint32>(sizeof(InstancedIndexedIndirectArgs));
    indirect.DrawCount = 1u;
    indirect.Flags = kHotPathDrawFlags;
    indirect.AttribsBufferStateTransitionMode =
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context_->DrawIndexedIndirect(indirect);
    instancing_stats_.gpu_indirect_draws += 1u;
    return true;
  };

  struct GpuLodBucketDraw {
    const ForwardBatch* batch = nullptr;
    Diligent::IBuffer* visible_instance_buffer = nullptr;
    Diligent::IBufferView* visible_instance_uav = nullptr;
    Diligent::IBuffer* indirect_args_buffer = nullptr;
    Diligent::IBufferView* indirect_args_uav = nullptr;
  };

  struct GpuLodClassification {
    bool ready = false;
    uint32_t bucket_count = 0;
    std::array<GpuLodBucketDraw, kInstancedGpuLodBucketCapacity> buckets{};
  };

  static thread_local std::unordered_map<rendering::InstanceId, GpuLodClassification>
      gpu_lod_classifications;
  gpu_lod_classifications.clear();
  gpu_lod_classifications.reserve(instanced_records_.size());

  auto lod_bucket_index_for_batch =
      [&](const ForwardBatch& batch,
          const InstancedRecord& record,
          uint32_t& bucket_index) -> bool {
    if (batch.instanced_lod_index == UINT32_MAX) {
      bucket_index = 0u;
      return true;
    }
    if (batch.instanced_lod_index >= record.lods.size()) {
      return false;
    }
    bucket_index = batch.instanced_lod_index + 1u;
    return bucket_index < kInstancedGpuLodBucketCapacity;
  };

  struct GpuLodBatchSet {
    std::array<const ForwardBatch*, kInstancedGpuLodBucketCapacity> batches{};
    bool invalid = false;
  };
  static thread_local std::unordered_map<rendering::InstanceId, GpuLodBatchSet>
      gpu_lod_batch_sets;
  gpu_lod_batch_sets.clear();
  gpu_lod_batch_sets.reserve(instanced_records_.size());
  for (const ForwardBatch& batch : state.opaque_batches) {
    if (batch.instanced_record == rendering::kInvalidInstance) {
      continue;
    }
    const auto record_it = instanced_records_.find(batch.instanced_record);
    if (record_it == instanced_records_.end() || record_it->second.lods.empty()) {
      continue;
    }
    GpuLodBatchSet& batch_set = gpu_lod_batch_sets[batch.instanced_record];
    uint32_t bucket_index = 0u;
    if (!lod_bucket_index_for_batch(batch, record_it->second, bucket_index) ||
        batch_set.batches[bucket_index] != nullptr) {
      batch_set.invalid = true;
      continue;
    }
    batch_set.batches[bucket_index] = &batch;
  }

  auto draw_preclassified_gpu_lod_batch =
      [&](const MeshRecord& mesh,
          const ForwardBatchKey& key,
          Diligent::IPipelineState* graphics_pipeline,
          Diligent::IShaderResourceBinding* graphics_srb,
          Diligent::IBuffer* visible_instance_buffer,
          Diligent::IBuffer* indirect_args_buffer,
          Diligent::IBuffer*& bound_mesh_vb,
          Diligent::IBuffer*& bound_instance_vb,
          Diligent::IBuffer*& bound_index_buffer) -> bool {
    if (use_custom_shader_override ||
        !key.indexed ||
        !visible_instance_buffer ||
        !indirect_args_buffer ||
        !graphics_pipeline ||
        !graphics_srb ||
        !is_valid_indexed_draw(mesh, key.index_offset, key.index_count)) {
      return false;
    }

    context_->SetPipelineState(graphics_pipeline);
    context_->CommitShaderResources(graphics_srb,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (!bind_forward_instance_buffer(mesh,
                                      visible_instance_buffer,
                                      bound_mesh_vb,
                                      bound_instance_vb)) {
      return false;
    }
    Diligent::IBuffer* index_buffer = mesh.index_buffer.RawPtr();
    if (index_buffer != bound_index_buffer) {
      context_->SetIndexBuffer(mesh.index_buffer,
                               0,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      bound_index_buffer = index_buffer;
    }

    Diligent::DrawIndexedIndirectAttribs indirect{};
    indirect.IndexType = Diligent::VT_UINT32;
    indirect.pAttribsBuffer = indirect_args_buffer;
    indirect.DrawArgsOffset = 0u;
    indirect.DrawArgsStride =
        static_cast<Diligent::Uint32>(sizeof(InstancedIndexedIndirectArgs));
    indirect.DrawCount = 1u;
    indirect.Flags = kHotPathDrawFlags;
    indirect.AttribsBufferStateTransitionMode =
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context_->DrawIndexedIndirect(indirect);
    instancing_stats_.gpu_indirect_draws += 1u;
    return true;
  };

  auto classify_gpu_lod_record =
      [&](rendering::InstanceId record_id,
          InstancedRecord& record,
          InstanceSetRecord& instance_set,
          Diligent::Uint32 instance_count) -> GpuLodClassification* {
    auto [classification_it, inserted] =
        gpu_lod_classifications.emplace(record_id, GpuLodClassification{});
    auto& classification = classification_it->second;
    if (!inserted) {
      return &classification;
    }

    classification.bucket_count =
        static_cast<uint32_t>(record.lods.size()) + 1u;
    if (use_custom_shader_override ||
        instance_set.dynamic ||
        record.lods.empty() ||
        classification.bucket_count > kInstancedGpuLodBucketCapacity ||
        instance_count == 0u ||
        !instance_set.instance_buffer ||
        !instance_set.instance_srv ||
        !ensureInstancedGpuLodCullingResources(instance_set.gpu_layout)) {
      return &classification;
    }
    const size_t layout_index = instanceGpuLayoutIndex(instance_set.gpu_layout);
    auto& culling_pso = instanced_gpu_lod_culling_psos_[layout_index];
    auto& culling_srb = instanced_gpu_lod_culling_srbs_[layout_index];

    const auto batch_set_it = gpu_lod_batch_sets.find(record_id);
    if (batch_set_it == gpu_lod_batch_sets.end() || batch_set_it->second.invalid) {
      return &classification;
    }
    const auto& batch_by_bucket = batch_set_it->second.batches;

    for (uint32_t bucket_index = 0u;
         bucket_index < classification.bucket_count;
         ++bucket_index) {
      const ForwardBatch* bucket_batch = batch_by_bucket[bucket_index];
      if (!bucket_batch ||
          !bucket_batch->key.indexed ||
          bucket_batch->key.gpu_layout != instance_set.gpu_layout ||
          bucket_batch->persistent_instance_count != instance_count) {
        return &classification;
      }
      auto mesh_it = meshes_.find(bucket_batch->key.mesh);
      if (mesh_it == meshes_.end() ||
          !mesh_it->second.vertex_buffer ||
          !is_valid_indexed_draw(mesh_it->second,
                                 bucket_batch->key.index_offset,
                                 bucket_batch->key.index_count)) {
        return &classification;
      }

      GpuLodBucketDraw& bucket = classification.buckets[bucket_index];
      bucket.batch = bucket_batch;
      if (bucket_index == 0u) {
        if (!ensureInstancedGpuCullingRecordBuffers(record, instance_set)) {
          return &classification;
        }
        bucket.visible_instance_buffer = record.gpu_culled_instance_buffer.RawPtr();
        bucket.visible_instance_uav = record.gpu_culled_instance_uav.RawPtr();
        bucket.indirect_args_buffer = record.gpu_culling_indirect_args_buffer.RawPtr();
        bucket.indirect_args_uav = record.gpu_culling_indirect_args_uav.RawPtr();
      } else {
        auto& lod = record.lods[bucket_index - 1u];
        if (!ensureInstancedGpuCullingOutputBuffers(
                instance_set.gpu_layout,
                instance_count,
                lod.gpu_culled_instance_buffer,
                lod.gpu_culled_instance_uav,
                lod.gpu_culled_instance_buffer_capacity_bytes,
                lod.gpu_culled_layout,
                lod.gpu_culling_indirect_args_buffer,
                lod.gpu_culling_indirect_args_uav)) {
          return &classification;
        }
        bucket.visible_instance_buffer = lod.gpu_culled_instance_buffer.RawPtr();
        bucket.visible_instance_uav = lod.gpu_culled_instance_uav.RawPtr();
        bucket.indirect_args_buffer = lod.gpu_culling_indirect_args_buffer.RawPtr();
        bucket.indirect_args_uav = lod.gpu_culling_indirect_args_uav.RawPtr();
      }
      if (!bucket.visible_instance_buffer ||
          !bucket.visible_instance_uav ||
          !bucket.indirect_args_buffer ||
          !bucket.indirect_args_uav) {
        return &classification;
      }

      InstancedIndexedIndirectArgs args{};
      args.num_indices = bucket_batch->key.index_count;
      args.num_instances = 0u;
      args.first_index_location = bucket_batch->key.index_offset;
      args.base_vertex = 0u;
      args.first_instance_location = 0u;
      context_->UpdateBuffer(bucket.indirect_args_buffer,
                             0,
                             sizeof(args),
                             &args,
                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    const ForwardBatch* base_batch = classification.buckets[0].batch;
    if (!base_batch) {
      return &classification;
    }
    auto base_mesh_it = meshes_.find(base_batch->key.mesh);
    if (base_mesh_it == meshes_.end()) {
      return &classification;
    }
    const MeshRecord& base_mesh = base_mesh_it->second;
    glm::vec3 combined_bounds_center = base_mesh.bounds_center;
    float combined_bounds_radius = base_mesh.bounds_radius;
    bool combined_bounds_valid = base_mesh.bounds_radius > 0.0f;
    for (const auto& lod : record.lods) {
      glm::vec3 lod_center = lod.bounds_center;
      float lod_radius = lod.bounds_radius;
      bool lod_valid = lod.bounds_valid;
      if (!lod_valid) {
        const auto lod_mesh_it = meshes_.find(lod.mesh);
        if (lod_mesh_it != meshes_.end()) {
          lod_center = lod_mesh_it->second.bounds_center;
          lod_radius = lod_mesh_it->second.bounds_radius;
          lod_valid = lod_radius > 0.0f;
        }
      }
      if (lod_valid) {
        mergeBoundingSphere(combined_bounds_center,
                            combined_bounds_radius,
                            combined_bounds_valid,
                            lod_center,
                            lod_radius);
      }
    }

    InstancedGpuCullingConstants culling_constants{};
    std::memcpy(culling_constants.view_proj,
                base_constants.mvp,
                sizeof(culling_constants.view_proj));
    copyMat4(culling_constants.batch_transform, record.batch_transform);
    culling_constants.mesh_bounds[0] = combined_bounds_center.x;
    culling_constants.mesh_bounds[1] = combined_bounds_center.y;
    culling_constants.mesh_bounds[2] = combined_bounds_center.z;
    culling_constants.mesh_bounds[3] = combined_bounds_radius;
    culling_constants.lod_reference_bounds[0] = base_mesh.bounds_center.x;
    culling_constants.lod_reference_bounds[1] = base_mesh.bounds_center.y;
    culling_constants.lod_reference_bounds[2] = base_mesh.bounds_center.z;
    culling_constants.lod_reference_bounds[3] = base_mesh.bounds_radius;
    culling_constants.camera_position[0] = base_constants.camera_pos[0];
    culling_constants.camera_position[1] = base_constants.camera_pos[1];
    culling_constants.camera_position[2] = base_constants.camera_pos[2];
    culling_constants.camera_position[3] = 1.0f;
    float previous_start = 0.0f;
    for (size_t lod_index = 0; lod_index < 3u; ++lod_index) {
      float start_distance = std::numeric_limits<float>::max();
      if (lod_index < record.lods.size()) {
        start_distance = std::max(record.lods[lod_index].start_distance, previous_start);
        previous_start = start_distance;
      }
      culling_constants.distance_params[lod_index] = start_distance;
    }
    culling_constants.distance_params[3] = 0.0f;
    culling_constants.params[0] = instance_count;
    culling_constants.params[1] = classification.bucket_count;
    culling_constants.params[2] = 0u;
    culling_constants.params[3] = is_gl ? 1u : 0u;
    {
      Diligent::MapHelper<InstancedGpuCullingConstants> mapped(
          context_,
          instanced_gpu_culling_cb_,
          Diligent::MAP_WRITE,
          Diligent::MAP_FLAG_DISCARD);
      auto* mapped_constants = getMappedData(mapped);
      if (mapped_constants == nullptr) {
        return &classification;
      }
      *mapped_constants = culling_constants;
    }

    instanced_gpu_lod_culling_source_vars_[layout_index]->Set(
        instance_set.instance_srv,
        Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    for (uint32_t i = 0u; i < kInstancedGpuLodBucketCapacity; ++i) {
      const uint32_t source_bucket =
          std::min(i, classification.bucket_count - 1u);
      const GpuLodBucketDraw& bucket = classification.buckets[source_bucket];
      instanced_gpu_lod_culling_visible_vars_[layout_index][i]->Set(
          bucket.visible_instance_uav,
          Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
      instanced_gpu_lod_culling_args_vars_[layout_index][i]->Set(
          bucket.indirect_args_uav,
          Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }

    context_->SetPipelineState(culling_pso);
    context_->CommitShaderResources(culling_srb,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::DispatchComputeAttribs dispatch{};
    dispatch.ThreadGroupCountX = (instance_count + 63u) / 64u;
    dispatch.ThreadGroupCountY = 1u;
    dispatch.ThreadGroupCountZ = 1u;
    context_->DispatchCompute(dispatch);
    instancing_stats_.gpu_culling_batches += 1u;
    instancing_stats_.gpu_culling_dispatches += 1u;
    instancing_stats_.gpu_culling_candidate_instances += instance_count;
    instancing_stats_.lod_bucket_count += classification.bucket_count;
    instancing_stats_.lod_culling_dispatches += 1u;
    instancing_stats_.lod_candidate_instances += instance_count;

    std::array<Diligent::StateTransitionDesc, kInstancedGpuLodBucketCapacity>
        visible_transitions{};
    for (uint32_t i = 0u; i < classification.bucket_count; ++i) {
      visible_transitions[i].pResource = classification.buckets[i].visible_instance_buffer;
      visible_transitions[i].OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
      visible_transitions[i].NewState = Diligent::RESOURCE_STATE_VERTEX_BUFFER;
      visible_transitions[i].Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
    }
    context_->TransitionResourceStates(classification.bucket_count,
                                       visible_transitions.data());
    classification.ready = true;
    return &classification;
  };

  static thread_local std::vector<InstanceGpuData> frame_packed_instances;
  static thread_local std::vector<Diligent::Uint32> opaque_batch_first_instances;
  static thread_local std::vector<Diligent::Uint32> deformed_first_instances;
  frame_packed_instances.clear();
  opaque_batch_first_instances.assign(state.opaque_batches.size(), 0u);
  deformed_first_instances.assign(state.deformed_opaque_draws.size(), 0u);

  constexpr size_t kMaxSharedInstanceCount =
      static_cast<size_t>(std::numeric_limits<Diligent::Uint32>::max()) /
      sizeof(InstanceGpuData);
  size_t shared_instance_count = state.deformed_opaque_draws.size();
  bool shared_instance_count_valid = shared_instance_count <= kMaxSharedInstanceCount;
  for (const ForwardBatch& batch : state.opaque_batches) {
    if (!shared_instance_count_valid ||
        batch.instances.size() > kMaxSharedInstanceCount - shared_instance_count) {
      shared_instance_count_valid = false;
      break;
    }
    shared_instance_count += batch.instances.size();
  }

  bool frame_packed_instances_ready = false;
  if (shared_instance_count_valid && shared_instance_count > 0u) {
    frame_packed_instances.reserve(shared_instance_count);
    for (size_t batch_index = 0u; batch_index < state.opaque_batches.size(); ++batch_index) {
      opaque_batch_first_instances[batch_index] =
          static_cast<Diligent::Uint32>(frame_packed_instances.size());
      for (const rendering::InstanceData& instance :
           state.opaque_batches[batch_index].instances) {
        frame_packed_instances.push_back(packInstanceData(instance.transform, instance.params));
      }
    }
    for (size_t draw_index = 0u;
         draw_index < state.deformed_opaque_draws.size();
         ++draw_index) {
      deformed_first_instances[draw_index] =
          static_cast<Diligent::Uint32>(frame_packed_instances.size());
      const DeformedForwardDraw& draw = state.deformed_opaque_draws[draw_index];
      frame_packed_instances.push_back(packInstanceData(draw.transform, draw.params));
    }
    if (ensure_instance_buffer(frame_packed_instances.size()) &&
        uploadInstanceData(context_,
                           instance_vb_,
                           frame_packed_instances.data(),
                           frame_packed_instances.size())) {
      instancing_stats_.instance_buffer_updates += 1u;
      instancing_stats_.instance_upload_bytes += static_cast<uint64_t>(
          frame_packed_instances.size() * sizeof(InstanceGpuData));
      frame_packed_instances_ready = true;
    }
  }

  const Diligent::Viewport viewport = buildViewport(render_width, render_height);
  Diligent::Uint32 draw_count = 0;
  const auto& adapter_info = device_->GetAdapterInfo();
  const bool disable_depth_prepass_for_driver =
      device_->GetDeviceInfo().IsVulkanDevice() &&
      (adapter_info.Vendor == Diligent::ADAPTER_VENDOR_NVIDIA ||
       adapter_info.Vendor == Diligent::ADAPTER_VENDOR_INTEL);
  const bool disable_depth_prepass_for_env = [] {
    const char* value = std::getenv("KARMA_DISABLE_DEPTH_PREPASS");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
  }();
  const bool force_depth_prepass_for_env = [] {
    const char* value = std::getenv("KARMA_FORCE_DEPTH_PREPASS");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
  }();
  bool has_custom_opaque_material = false;
  bool has_masked_opaque_material = false;
  for (size_t batch_index = 0u;
       batch_index < state.opaque_batches.size();
       ++batch_index) {
    const ForwardBatch& batch = state.opaque_batches[batch_index];
    if (MaterialRecord* mat = lookup_material(batch.key.material)) {
      if (materialUsesCustomForwardPipeline(*mat)) {
        has_custom_opaque_material = true;
      }
      if (mat->desc.alpha_mode == rendering::MaterialDesc::AlphaMode::Masked) {
        has_masked_opaque_material = true;
      }
      if (has_custom_opaque_material && has_masked_opaque_material) {
        break;
      }
    }
  }
  if (!has_custom_opaque_material) {
    for (const auto& draw : state.deformed_opaque_draws) {
      if (MaterialRecord* mat = lookup_material(draw.key.material);
          mat && materialUsesCustomForwardPipeline(*mat)) {
        has_custom_opaque_material = true;
        break;
      }
    }
  }
  const bool has_depth_prepass_draws =
      !state.deformed_opaque_draws.empty() ||
      std::any_of(state.opaque_batches.begin(),
                  state.opaque_batches.end(),
                  [](const ForwardBatch& batch) { return !batch.instances.empty(); });
  const bool depth_prepass_candidate =
      active_dsv && has_depth_prepass_draws && state.opaque_batches.size() > 1 &&
      (!disable_depth_prepass_for_driver || force_depth_prepass_for_env) &&
      !disable_depth_prepass_for_env &&
      !has_masked_opaque_material &&
      !use_custom_shader_override &&
      !has_custom_opaque_material;
  if (depth_prepass_candidate) {
    ensureForwardPipeline(ForwardPipelineVariant::DepthPrepass);
  }
  const bool run_depth_prepass = depth_prepass_candidate && depth_prepass_pipeline_state_;

  if (run_depth_prepass) {
    context_->SetRenderTargets(0,
                               nullptr,
                               active_dsv,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->SetPipelineState(depth_prepass_pipeline_state_);
    bool depth_prepass_ready = true;
    {
      Diligent::MapHelper<DrawConstants> mapped(
          context_, constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
      auto* mapped_constants = getMappedData(mapped);
      if (mapped_constants == nullptr) {
        depth_prepass_ready = false;
      } else {
        *mapped_constants = base_constants;
      }
    }

    Diligent::IBuffer* depth_bound_mesh_vb = nullptr;
    Diligent::IBuffer* depth_bound_instance_vb = nullptr;
    Diligent::IBuffer* depth_bound_index_buffer = nullptr;
    if (depth_prepass_ready) {
      bool default_deformation_bound = false;
      for (size_t batch_index = 0u;
           batch_index < state.opaque_batches.size();
           ++batch_index) {
        const ForwardBatch& batch = state.opaque_batches[batch_index];
        if (batch.instances.empty()) {
          continue;
        }
        auto mesh_it = meshes_.find(batch.key.mesh);
        if (mesh_it == meshes_.end()) {
          continue;
        }
        const auto& mesh = mesh_it->second;
        if (!mesh.vertex_buffer) {
          continue;
        }
        {
          DrawConstants depth_constants = base_constants;
          depth_constants.instance_params[0] = 0.0f;
          depth_constants.instance_params[1] =
              batch.key.render_mode == rendering::LodRenderMode::UprightBillboard
                  ? 1.0f
                  : 0.0f;
          Diligent::MapHelper<DrawConstants> mapped(
              context_, constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
          auto* mapped_constants = getMappedData(mapped);
          if (mapped_constants == nullptr) {
            continue;
          }
          *mapped_constants = depth_constants;
        }
        if (depth_prepass_srb_ && !default_deformation_bound) {
          if (!bindDeformationResources(depth_prepass_srb_,
                                        mesh,
                                        rendering::kInvalidDeformation)) {
            continue;
          }
          context_->CommitShaderResources(depth_prepass_srb_,
                                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
          default_deformation_bound = true;
        }

        Diligent::Uint32 first_instance = 0u;
        if (frame_packed_instances_ready) {
          if (!bind_forward_instance_buffer(mesh,
                                            instance_vb_.RawPtr(),
                                            depth_bound_mesh_vb,
                                            depth_bound_instance_vb)) {
            continue;
          }
          first_instance = opaque_batch_first_instances[batch_index];
        } else {
          pack_instances(batch.instances);
          if (!bind_forward_geometry(mesh,
                                     fallback_packed_instances.data(),
                                     fallback_packed_instances.size(),
                                     depth_bound_mesh_vb,
                                     depth_bound_instance_vb)) {
            continue;
          }
        }
        draw_forward_batch(mesh,
                           batch.key,
                           static_cast<Diligent::Uint32>(batch.instances.size()),
                           first_instance,
                           depth_bound_index_buffer,
                           "opaque-depth-prepass");
      }
      for (size_t draw_index = 0u;
           draw_index < state.deformed_opaque_draws.size();
           ++draw_index) {
        const DeformedForwardDraw& draw = state.deformed_opaque_draws[draw_index];
        auto mesh_it = meshes_.find(draw.key.mesh);
        if (mesh_it == meshes_.end()) {
          continue;
        }
        const auto& mesh = mesh_it->second;
        if (!mesh.vertex_buffer) {
          continue;
        }
        {
          Diligent::MapHelper<DrawConstants> mapped(
              context_, constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
          auto* mapped_constants = getMappedData(mapped);
          if (mapped_constants == nullptr) {
            continue;
          }
          *mapped_constants = base_constants;
        }
        if (depth_prepass_srb_) {
          if (!bindDeformationResources(depth_prepass_srb_, mesh, draw.deformation)) {
            continue;
          }
          context_->CommitShaderResources(depth_prepass_srb_,
                                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        } else if (!updateDeformationConstants(mesh, draw.deformation)) {
          continue;
        }
        Diligent::Uint32 first_instance = 0u;
        if (frame_packed_instances_ready) {
          if (!bind_forward_instance_buffer(mesh,
                                            instance_vb_.RawPtr(),
                                            depth_bound_mesh_vb,
                                            depth_bound_instance_vb)) {
            continue;
          }
          first_instance = deformed_first_instances[draw_index];
        } else {
          const InstanceGpuData packed_transform =
              packInstanceData(draw.transform, draw.params);
          if (!bind_forward_geometry(mesh,
                                     &packed_transform,
                                     1u,
                                     depth_bound_mesh_vb,
                                     depth_bound_instance_vb)) {
            continue;
          }
        }
        draw_forward_batch(mesh,
                           draw.key,
                           1u,
                           first_instance,
                           depth_bound_index_buffer,
                           "deformed-depth-prepass");
      }
    }

    context_->SetRenderTargets(1,
                               &active_rtv,
                               active_dsv,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
    context_->SetViewports(1,
                           &viewport,
                           static_cast<Diligent::Uint32>(render_width),
                           static_cast<Diligent::Uint32>(render_height));
  }

  context_->SetPipelineState(active_forward_pipeline);
  Diligent::IPipelineState* bound_forward_pipeline = nullptr;
  Diligent::IBuffer* bound_mesh_vb = nullptr;
  Diligent::IBuffer* bound_instance_vb = nullptr;
  Diligent::IBuffer* bound_index_buffer = nullptr;
  Diligent::IShaderResourceBinding* bound_forward_srb = nullptr;
  rendering::MaterialId last_constants_material = rendering::kInvalidMaterial;
  rendering::MeshId last_constants_mesh = rendering::kInvalidMesh;
  rendering::InstanceGpuLayout last_constants_layout = rendering::InstanceGpuLayout::Matrix4x4Params;
  rendering::LodRenderMode last_constants_render_mode =
      rendering::LodRenderMode::Mesh;
  glm::mat4 last_constants_batch_transform{1.0f};
  {
    Diligent::MapHelper<DrawConstants> mapped(
        context_, constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
    auto* mapped_constants = getMappedData(mapped);
    if (mapped_constants == nullptr) {
      return 0;
    }
    *mapped_constants = base_constants;
  }
  if (use_custom_shader_override && camera_override_srb_) {
    context_->CommitShaderResources(camera_override_srb_,
                                    Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    bound_forward_srb = camera_override_srb_;
  }
  for (size_t batch_index = 0u;
       batch_index < state.opaque_batches.size();
       ++batch_index) {
    const ForwardBatch& batch = state.opaque_batches[batch_index];
    if (batch.instances.empty() && batch.instanced_record == rendering::kInvalidInstance) {
      continue;
    }
    auto mesh_it = meshes_.find(batch.key.mesh);
    if (mesh_it == meshes_.end()) {
      continue;
    }
    const auto& mesh = mesh_it->second;
    if (!mesh.vertex_buffer) {
      continue;
    }

    MaterialRecord* mat = lookup_material(batch.key.material);
    bool custom_pipeline = false;
    Diligent::IPipelineState* pipeline =
        resolve_forward_pipeline(mat, batch.key.gpu_layout, custom_pipeline);
    if (!pipeline) {
      continue;
    }
    if (pipeline != bound_forward_pipeline) {
      context_->SetPipelineState(pipeline);
      bound_forward_pipeline = pipeline;
      bound_forward_srb = nullptr;
      bound_mesh_vb = nullptr;
      bound_instance_vb = nullptr;
      bound_index_buffer = nullptr;
      if (use_custom_shader_override && camera_override_srb_) {
        context_->CommitShaderResources(camera_override_srb_,
                                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_forward_srb = camera_override_srb_;
      }
    }
    glm::mat4 batch_transform{1.0f};
    if (batch.instanced_record != rendering::kInvalidInstance) {
      const auto record_it = instanced_records_.find(batch.instanced_record);
      if (record_it == instanced_records_.end()) {
        continue;
      }
      batch_transform = record_it->second.batch_transform;
    }
    if (!update_forward_material_constants(batch.key.material,
                                           batch.key.mesh,
                                           mesh,
                                           mat,
                                           batch.key.gpu_layout,
                                           batch.key.render_mode,
                                           batch_transform,
                                           last_constants_material,
                                           last_constants_mesh,
                                           last_constants_layout,
                                           last_constants_render_mode,
                                           last_constants_batch_transform)) {
      continue;
    }

    Diligent::IShaderResourceBinding* active_forward_srb = nullptr;
    if (!use_custom_shader_override) {
      active_forward_srb =
          resolve_forward_srb(mat, custom_pipeline, batch.key.gpu_layout);
      if (active_forward_srb &&
          !bindDeformationResources(active_forward_srb, mesh, rendering::kInvalidDeformation)) {
        continue;
      }
      if (active_forward_srb) {
        context_->CommitShaderResources(active_forward_srb,
                                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_forward_srb = active_forward_srb;
      }
    } else if (!updateDeformationConstants(mesh, rendering::kInvalidDeformation)) {
      continue;
    }

    Diligent::Uint32 instance_count = 0u;
    Diligent::Uint32 first_instance = 0u;
    if (batch.instanced_record != rendering::kInvalidInstance) {
      auto record_it = instanced_records_.find(batch.instanced_record);
      if (record_it == instanced_records_.end()) {
        continue;
      }
      auto& record = record_it->second;
      auto source_it = instance_sets_.find(record.instance_set);
      if (source_it == instance_sets_.end() ||
          !ensureInstanceSetBuffer(source_it->second)) {
        continue;
      }
      auto& instance_set = source_it->second;
      instance_count = batch.persistent_instance_count;
      const bool lod_batch = batch.instanced_lod_index != UINT32_MAX;
      if (lod_batch && batch.instanced_lod_index >= record.lods.size()) {
        continue;
      }
      uint32_t lod_bucket_index = 0u;
      if (!lod_bucket_index_for_batch(batch, record, lod_bucket_index)) {
        continue;
      }
      if (!record.lods.empty()) {
        GpuLodClassification* classification =
            classify_gpu_lod_record(batch.instanced_record,
                                      record,
                                      instance_set,
                                      instance_count);
        if (classification && classification->ready &&
            lod_bucket_index < classification->bucket_count) {
          const GpuLodBucketDraw& bucket =
              classification->buckets[lod_bucket_index];
          if (bucket.batch == &batch &&
              draw_preclassified_gpu_lod_batch(
                  mesh,
                  batch.key,
                  pipeline,
                  active_forward_srb,
                  bucket.visible_instance_buffer,
                  bucket.indirect_args_buffer,
                  bound_mesh_vb,
                  bound_instance_vb,
                  bound_index_buffer)) {
            draw_count += 1;
            instancing_stats_.drawn_batches += 1u;
            if (!lod_batch) {
              instancing_stats_.drawn_instances += instance_count;
            }
            instancing_stats_.draw_calls += 1u;
            instancing_stats_.lod_indirect_draws += 1u;
            continue;
          }
        }
      }
      float distance_min = 0.0f;
      float distance_max = std::numeric_limits<float>::max();
      Diligent::IBuffer* visible_instance_buffer = nullptr;
      Diligent::IBufferView* visible_instance_uav = nullptr;
      Diligent::IBuffer* indirect_args_buffer = nullptr;
      Diligent::IBufferView* indirect_args_uav = nullptr;
      bool gpu_lod_bucket_ready = false;
      if (!instance_set.dynamic) {
        if (lod_batch) {
          auto& lod = record.lods[batch.instanced_lod_index];
          distance_min = lod.start_distance;
          const size_t next_lod_index =
              static_cast<size_t>(batch.instanced_lod_index) + 1u;
          distance_max = next_lod_index < record.lods.size()
                             ? record.lods[next_lod_index].start_distance
                             : std::numeric_limits<float>::max();
          gpu_lod_bucket_ready = ensureInstancedGpuCullingOutputBuffers(
              instance_set.gpu_layout,
              instance_set.instanceCount(),
              lod.gpu_culled_instance_buffer,
              lod.gpu_culled_instance_uav,
              lod.gpu_culled_instance_buffer_capacity_bytes,
              lod.gpu_culled_layout,
              lod.gpu_culling_indirect_args_buffer,
              lod.gpu_culling_indirect_args_uav);
          visible_instance_buffer = lod.gpu_culled_instance_buffer.RawPtr();
          visible_instance_uav = lod.gpu_culled_instance_uav.RawPtr();
          indirect_args_buffer = lod.gpu_culling_indirect_args_buffer.RawPtr();
          indirect_args_uav = lod.gpu_culling_indirect_args_uav.RawPtr();
        } else if (ensureInstancedGpuCullingRecordBuffers(record, instance_set)) {
          if (!record.lods.empty()) {
            distance_max = record.lods.front().start_distance;
          }
          gpu_lod_bucket_ready = true;
          visible_instance_buffer = record.gpu_culled_instance_buffer.RawPtr();
          visible_instance_uav = record.gpu_culled_instance_uav.RawPtr();
          indirect_args_buffer = record.gpu_culling_indirect_args_buffer.RawPtr();
          indirect_args_uav = record.gpu_culling_indirect_args_uav.RawPtr();
        }
      }
      if (!record.lods.empty()) {
        instancing_stats_.lod_bucket_count += 1u;
      }
      if (gpu_lod_bucket_ready &&
          draw_gpu_culled_forward_batch(record,
                                        instance_set,
                                        mesh,
                                        batch.key,
                                        instance_count,
                                        pipeline,
                                        active_forward_srb,
                                        visible_instance_buffer,
                                        visible_instance_uav,
                                        indirect_args_buffer,
                                        indirect_args_uav,
                                        distance_min,
                                        distance_max,
                                        bound_mesh_vb,
                                        bound_instance_vb,
                                        bound_index_buffer)) {
        draw_count += 1;
        instancing_stats_.drawn_batches += 1u;
        if (!lod_batch) {
          instancing_stats_.drawn_instances += instance_count;
        }
        instancing_stats_.draw_calls += 1u;
        if (!record.lods.empty()) {
          instancing_stats_.lod_culling_dispatches += 1u;
          instancing_stats_.lod_candidate_instances += instance_count;
          instancing_stats_.lod_indirect_draws += 1u;
        }
        continue;
      }
      if (lod_batch) {
        instancing_stats_.lod_fallbacks += 1u;
        continue;
      }
      if (!bind_forward_instance_buffer(mesh,
                                        instance_set.instance_buffer.RawPtr(),
                                        bound_mesh_vb,
                                        bound_instance_vb)) {
        continue;
      }
    } else {
      instance_count = static_cast<Diligent::Uint32>(batch.instances.size());
      if (frame_packed_instances_ready) {
        if (!bind_forward_instance_buffer(mesh,
                                          instance_vb_.RawPtr(),
                                          bound_mesh_vb,
                                          bound_instance_vb)) {
          continue;
        }
        first_instance = opaque_batch_first_instances[batch_index];
      } else {
        pack_instances(batch.instances);
        if (!bind_forward_geometry(mesh,
                                   fallback_packed_instances.data(),
                                   fallback_packed_instances.size(),
                                   bound_mesh_vb,
                                   bound_instance_vb)) {
          continue;
        }
      }
    }
    if (draw_forward_batch(mesh,
                           batch.key,
                           instance_count,
                           first_instance,
                           bound_index_buffer,
                           "opaque-forward")) {
      draw_count += 1;
      if (batch.instanced_record != rendering::kInvalidInstance) {
        instancing_stats_.drawn_batches += 1u;
        instancing_stats_.drawn_instances += instance_count;
        instancing_stats_.draw_calls += 1u;
      }
    }
  }

  for (size_t draw_index = 0u;
       draw_index < state.deformed_opaque_draws.size();
       ++draw_index) {
    const DeformedForwardDraw& draw = state.deformed_opaque_draws[draw_index];
    auto mesh_it = meshes_.find(draw.key.mesh);
    if (mesh_it == meshes_.end()) {
      continue;
    }
    const auto& mesh = mesh_it->second;
    if (!mesh.vertex_buffer) {
      continue;
    }

    MaterialRecord* mat = lookup_material(draw.key.material);
    bool custom_pipeline = false;
    Diligent::IPipelineState* pipeline =
        resolve_forward_pipeline(mat,
                                 rendering::InstanceGpuLayout::Matrix4x4Params,
                                 custom_pipeline);
    if (!pipeline) {
      continue;
    }
    if (pipeline != bound_forward_pipeline) {
      context_->SetPipelineState(pipeline);
      bound_forward_pipeline = pipeline;
      bound_forward_srb = nullptr;
      bound_mesh_vb = nullptr;
      bound_instance_vb = nullptr;
      bound_index_buffer = nullptr;
      if (use_custom_shader_override && camera_override_srb_) {
        context_->CommitShaderResources(camera_override_srb_,
                                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_forward_srb = camera_override_srb_;
      }
    }
    if (!update_forward_material_constants(draw.key.material,
                                           draw.key.mesh,
                                           mesh,
                                 mat,
                                 rendering::InstanceGpuLayout::Matrix4x4Params,
                                 rendering::LodRenderMode::Mesh,
                                 glm::mat4{1.0f},
                                 last_constants_material,
                                 last_constants_mesh,
                                 last_constants_layout,
                                 last_constants_render_mode,
                                 last_constants_batch_transform)) {
      continue;
    }

    if (!use_custom_shader_override) {
      Diligent::IShaderResourceBinding* srb =
          resolve_forward_srb(mat,
                              custom_pipeline,
                              rendering::InstanceGpuLayout::Matrix4x4Params);
      if (srb && !bindDeformationResources(srb, mesh, draw.deformation)) {
        continue;
      }
      if (srb) {
        context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_forward_srb = srb;
      }
    } else if (!updateDeformationConstants(mesh, draw.deformation)) {
      continue;
    }

    Diligent::Uint32 first_instance = 0u;
    if (frame_packed_instances_ready) {
      if (!bind_forward_instance_buffer(mesh,
                                        instance_vb_.RawPtr(),
                                        bound_mesh_vb,
                                        bound_instance_vb)) {
        continue;
      }
      first_instance = deformed_first_instances[draw_index];
    } else {
      const InstanceGpuData packed_transform = packInstanceData(draw.transform, draw.params);
      if (!bind_forward_geometry(mesh,
                                 &packed_transform,
                                 1u,
                                 bound_mesh_vb,
                                 bound_instance_vb)) {
        continue;
      }
    }
    if (draw_forward_batch(mesh,
                           draw.key,
                           1u,
                           first_instance,
                           bound_index_buffer,
                           "deformed-forward")) {
      draw_count += 1;
    }
  }

  return draw_count;
}

Diligent::Uint32 DiligentBackend::renderTransparentForwardDraws(
    const std::vector<TransparentForwardDraw>& draws,
    const DrawConstants& base_constants,
    Diligent::IPipelineState* active_forward_pipeline,
    bool use_custom_shader_override,
    Diligent::ITextureView* active_rtv,
    Diligent::ITextureView* active_dsv,
    Diligent::ITextureView* particle_dsv,
    int render_width,
    int render_height,
    Diligent::ITextureView* scene_color_sample_srv,
    Diligent::ITextureView* scene_depth_sample_srv) {
  if (draws.empty() || !active_forward_pipeline || !constants_) {
    return 0;
  }
  const bool draw_debug = [] {
    const char* value = std::getenv("KARMA_DRAW_DEBUG");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
  }();

  auto ensure_instance_buffer = [&](size_t instance_count) {
    if (instance_count == 0) {
      return false;
    }
    constexpr size_t kMaxInstanceCount =
        static_cast<size_t>(std::numeric_limits<Diligent::Uint32>::max()) /
        sizeof(InstanceGpuData);
    if (instance_count > kMaxInstanceCount) {
      return false;
    }
    if (instance_vb_ && instance_vb_capacity_ >= instance_count) {
      return true;
    }
    const size_t grown_capacity =
        instance_vb_capacity_ > 0u && instance_vb_capacity_ <= kMaxInstanceCount / 2u
            ? instance_vb_capacity_ * 2u
            : kMaxInstanceCount;
    const size_t new_capacity = std::min(
        kMaxInstanceCount,
        std::max(instance_count,
                 instance_vb_capacity_ > 0u ? grown_capacity : static_cast<size_t>(128u)));
    Diligent::BufferDesc ib_desc{};
    ib_desc.Name = "Karma Instance Buffer";
    ib_desc.Usage = Diligent::USAGE_DYNAMIC;
    ib_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    ib_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    ib_desc.Size = static_cast<Diligent::Uint32>(new_capacity * sizeof(InstanceGpuData));
    instance_vb_.Release();
    device_->CreateBuffer(ib_desc, nullptr, &instance_vb_);
    if (!instance_vb_) {
      return false;
    }
    instance_vb_capacity_ = new_capacity;
    return true;
  };

  auto is_valid_indexed_draw = [&](const MeshRecord& mesh,
                                   Diligent::Uint32 first_index,
                                   Diligent::Uint32 index_count) {
    if (!mesh.index_buffer || mesh.index_count == 0 || index_count == 0) {
      return false;
    }
    const uint64_t first = static_cast<uint64_t>(first_index);
    const uint64_t count = static_cast<uint64_t>(index_count);
    const uint64_t total = static_cast<uint64_t>(mesh.index_count);
    if (first >= total) {
      return false;
    }
    return first + count <= total;
  };

  auto lookup_material = [&](rendering::MaterialId material_id) -> MaterialRecord* {
    if (material_id == rendering::kInvalidMaterial) {
      return nullptr;
    }
    auto mat_it = materials_.find(material_id);
    return mat_it != materials_.end() ? &mat_it->second : nullptr;
  };

  auto update_forward_material_constants = [&](rendering::MaterialId material_id,
                                               rendering::MeshId mesh_id,
                                               const MeshRecord& mesh,
                                               const MaterialRecord* mat,
                                               rendering::LodRenderMode render_mode,
                                               TransparentForwardDraw::SceneSampleMode scene_sample_mode,
                                               const rendering::VolumeDrawParams& volume_params,
                                               bool has_volume_params,
                                               rendering::MaterialId& last_constants_material,
                                               rendering::MeshId& last_constants_mesh,
                                               rendering::LodRenderMode& last_constants_render_mode,
                                               TransparentForwardDraw::SceneSampleMode&
                                                   last_constants_scene_sample_mode) -> bool {
    if (!has_volume_params &&
        material_id == last_constants_material &&
        render_mode == last_constants_render_mode &&
        scene_sample_mode == last_constants_scene_sample_mode &&
        (material_id != rendering::kInvalidMaterial || mesh_id == last_constants_mesh)) {
      return true;
    }
    DrawConstants constants = base_constants;
    if (has_volume_params) {
      constants.volume_params0[0] = volume_params.center.x;
      constants.volume_params0[1] = volume_params.center.y;
      constants.volume_params0[2] = volume_params.center.z;
      constants.volume_params0[3] = volume_params.radius;
      constants.volume_params1[0] = volume_params.axis_x.x;
      constants.volume_params1[1] = volume_params.axis_x.y;
      constants.volume_params1[2] = volume_params.axis_x.z;
      constants.volume_params1[3] = volume_params.capsule_half_length;
      constants.volume_params2[0] = volume_params.axis_y.x;
      constants.volume_params2[1] = volume_params.axis_y.y;
      constants.volume_params2[2] = volume_params.axis_y.z;
      constants.volume_params2[3] = static_cast<float>(volume_params.shape);
      constants.volume_params3[0] = volume_params.axis_z.x;
      constants.volume_params3[1] = volume_params.axis_z.y;
      constants.volume_params3[2] = volume_params.axis_z.z;
      constants.volume_params3[3] = static_cast<float>(volume_params.slot);
      constants.volume_params4[0] = volume_params.overlay_depth;
      constants.volume_params4[1] = static_cast<float>(static_cast<uint32_t>(scene_sample_mode));
      constants.volume_params4[2] = constants.camera_clip_params[0];
      constants.volume_params4[3] = volume_params.surface_double_sided ? 1.0f : 0.0f;
    }
    constants.instance_params[0] = 0.0f;
    constants.instance_params[1] =
        render_mode == rendering::LodRenderMode::UprightBillboard ? 1.0f : 0.0f;
    constants.instance_params[2] = 0.0f;
    constants.instance_params[3] = 0.0f;
    glm::vec4 base_color = mat ? mat->base_color_factor : mesh.base_color;
    if (!mat && base_color == glm::vec4(1.0f)) {
      base_color = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    }
    constants.base_color_factor[0] = base_color.r;
    constants.base_color_factor[1] = base_color.g;
    constants.base_color_factor[2] = base_color.b;
    constants.base_color_factor[3] = base_color.a;
    const glm::vec3 emissive = mat ? mat->emissive_factor : glm::vec3(0.0f);
    constants.emissive_factor[0] = emissive.x;
    constants.emissive_factor[1] = emissive.y;
    constants.emissive_factor[2] = emissive.z;
    constants.emissive_factor[3] = 1.0f;
    constants.pbr_params[0] = mat ? mat->metallic_factor : 1.0f;
    constants.pbr_params[1] = mat ? mat->roughness_factor : 1.0f;
    constants.pbr_params[2] = mat ? mat->occlusion_strength : 1.0f;
    constants.pbr_params[3] = mat ? mat->normal_scale : 1.0f;
    constants.material_params0[0] =
        mat ? static_cast<float>(static_cast<uint32_t>(mat->shading_model)) : 0.0f;
    constants.material_params0[1] = mat ? mat->shell_fresnel_power : 5.0f;
    constants.material_params0[2] = mat ? mat->shell_fresnel_strength : 1.0f;
    constants.material_params0[3] = mat ? mat->shell_refraction_strength : 0.08f;
    if (mat && (mat->shading_model == MaterialPipelineKind::WaveVolume ||
                mat->shading_model == MaterialPipelineKind::ScreenWave)) {
      constants.material_params1[0] = mat->wave_tint_strength;
      constants.material_params1[1] = mat->wave_distortion_strength;
      constants.material_params1[2] = mat->wave_edge_strength;
      constants.material_params1[3] = mat->wave_noise_strength;
    } else if (mat &&
               mat->shading_model == MaterialPipelineKind::VolumetricSolid) {
      constants.material_params0[1] = static_cast<float>(mat->volume_shape);
      constants.material_params0[2] = mat->volume_anisotropy;
      constants.material_params0[3] = mat->volume_absorption;
      constants.material_params1[0] = mat->volume_center.x;
      constants.material_params1[1] = mat->volume_center.y;
      constants.material_params1[2] = mat->volume_center.z;
      constants.material_params1[3] = mat->volume_radius;
      constants.material_params2[0] = mat->volume_axis_x.x;
      constants.material_params2[1] = mat->volume_axis_x.y;
      constants.material_params2[2] = mat->volume_axis_x.z;
      constants.material_params2[3] = mat->volume_capsule_half_length;
      constants.material_params3[0] = mat->volume_axis_y.x;
      constants.material_params3[1] = mat->volume_axis_y.y;
      constants.material_params3[2] = mat->volume_axis_y.z;
      constants.material_params3[3] = mat->volume_density;
      constants.material_params4[0] = mat->volume_axis_z.x;
      constants.material_params4[1] = mat->volume_axis_z.y;
      constants.material_params4[2] = mat->volume_axis_z.z;
      constants.material_params4[3] = mat->volume_scattering;
      constants.material_params5[0] = mat->volume_distortion_strength;
      constants.material_params5[1] = mat->volume_noise_strength;
      constants.material_params5[2] = 0.0f;
      constants.material_params5[3] = 0.0f;
    } else if (!mat || mat->shading_model == MaterialPipelineKind::Standard ||
               mat->shading_model == MaterialPipelineKind::Foliage) {
      constexpr bool kAlphaToCoverageActive = false;
      const bool alpha_to_coverage_requested = mat && mat->desc.alpha_to_coverage;
      constants.material_params1[0] = mat ? mat->desc.alpha_softness : 0.0f;
      constants.material_params1[1] = (mat && mat->desc.alpha_dither) ? 1.0f : 0.0f;
      constants.material_params1[2] = alpha_to_coverage_requested ? 1.0f : 0.0f;
      constants.material_params1[3] =
          (alpha_to_coverage_requested && kAlphaToCoverageActive) ? 1.0f : 0.0f;
    } else {
      constants.material_params1[0] = mat ? mat->shell_interior_strength : 0.4f;
      constants.material_params1[1] = mat ? mat->shell_highlight_strength : 1.0f;
      constants.material_params1[2] = mat ? mat->shell_alpha_boost : 0.0f;
      constants.material_params1[3] = mat ? mat->shell_swirl_strength : 0.0f;
    }
    if (mat && mat->shading_model == MaterialPipelineKind::SphereHalo) {
      constants.material_params2[0] = mat->screen_center_x;
      constants.material_params2[1] = mat->screen_center_y;
      constants.material_params2[2] = mat->screen_radius_x;
      constants.material_params2[3] = mat->screen_radius_y;
    } else if (mat && mat->shading_model == MaterialPipelineKind::ScreenWave) {
      constants.material_params2[0] = mat->screen_center_x;
      constants.material_params2[1] = mat->screen_center_y;
      constants.material_params2[2] = mat->screen_radius_x;
      constants.material_params2[3] = mat->screen_radius_y;
    } else if (mat &&
               mat->shading_model == MaterialPipelineKind::VolumetricSolid) {
      // Volumetric solids pack params 2-5 above with shape axes and optical data.
    } else {
      constants.material_params2[0] = (mat && mat->analytic_sphere_normals) ? 1.0f : 0.0f;
      constants.material_params2[1] = mat ? mat->shell_body_strength : 1.0f;
      constants.material_params2[2] = (mat && mat->desc.unlit) ? 1.0f : 0.0f;
      constants.material_params2[3] =
          (mat && mat->desc.alpha_mode == rendering::MaterialDesc::AlphaMode::Masked)
              ? mat->desc.alpha_cutoff
              : -1.0f;
    }
    if (!mat || mat->shading_model == MaterialPipelineKind::Standard ||
        mat->shading_model == MaterialPipelineKind::Foliage) {
      constants.material_params3[0] = mat ? mat->clearcoat_factor : 0.0f;
      constants.material_params3[1] = mat ? mat->clearcoat_roughness_factor : 0.0f;
      constants.material_params3[2] = mat ? mat->sheen_roughness_factor : 0.0f;
      constants.material_params3[3] = mat ? mat->anisotropy_factor : 0.0f;
      const glm::vec3 sheen_color = mat ? mat->sheen_color_factor : glm::vec3(0.0f);
      constants.material_params4[0] = sheen_color.r;
      constants.material_params4[1] = sheen_color.g;
      constants.material_params4[2] = sheen_color.b;
      constants.material_params4[3] = mat ? mat->transmission_factor : 0.0f;
      constants.material_params5[0] =
          (mat && std::isfinite(mat->ior)) ? std::max(mat->ior, 1.0f) : 1.5f;
      constants.material_params5[1] = mat ? mat->thickness_factor : 0.0f;
      constants.material_params5[2] =
          (mat && std::isfinite(mat->attenuation_distance)) ? mat->attenuation_distance : 0.0f;
      constants.material_params5[3] =
          (mat && mat->desc.normal_map_convention ==
                      rendering::MaterialDesc::NormalMapConvention::DirectX)
              ? -1.0f
              : 1.0f;
      const glm::vec3 attenuation_color = mat ? mat->attenuation_color : glm::vec3(1.0f);
      constants.material_params6[0] = attenuation_color.r;
      constants.material_params6[1] = attenuation_color.g;
      constants.material_params6[2] = attenuation_color.b;
      constants.material_params6[3] =
          static_cast<float>(static_cast<uint32_t>(scene_sample_mode));
      const glm::vec3 specular_color = mat ? mat->specular_color_factor : glm::vec3(1.0f);
      constants.material_params7[0] = specular_color.r;
      constants.material_params7[1] = specular_color.g;
      constants.material_params7[2] = specular_color.b;
      constants.material_params7[3] = mat ? mat->specular_factor : 1.0f;
      const bool lightmap_ready =
          mat && mat->lightmap_enabled && mat->lightmap_srv &&
          mat->lightmap_srv.RawPtr() != default_lightmap_.RawPtr();
      constants.lightmap_params[0] = lightmap_ready ? 1.0f : 0.0f;
      constants.lightmap_params[1] = mat ? mat->lightmap_intensity : 1.0f;
      constants.lightmap_params[2] = 0.0f;
      constants.lightmap_params[3] = 0.0f;
      const glm::vec4 lightmap_uv =
          mat ? mat->lightmap_uv_scale_offset : glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
      constants.lightmap_uv_scale_offset[0] = lightmap_uv.x;
      constants.lightmap_uv_scale_offset[1] = lightmap_uv.y;
      constants.lightmap_uv_scale_offset[2] = lightmap_uv.z;
      constants.lightmap_uv_scale_offset[3] = lightmap_uv.w;
      constants.lightmap_mixed_mask[0] = mat ? mat->lightmap_mixed_mask_low : 0u;
      constants.lightmap_mixed_mask[1] = mat ? mat->lightmap_mixed_mask_high : 0u;
      constants.lightmap_mixed_mask[2] = 0u;
      constants.lightmap_mixed_mask[3] = 0u;
      if (mat) {
        for (size_t slot = 0; slot < MaterialRecord::kTextureCoordSlotCount; ++slot) {
          constants.texcoord_row0[slot][0] = mat->texcoord_row0[slot].x;
          constants.texcoord_row0[slot][1] = mat->texcoord_row0[slot].y;
          constants.texcoord_row0[slot][2] = mat->texcoord_row0[slot].z;
          constants.texcoord_row0[slot][3] = mat->texcoord_row0[slot].w;
          constants.texcoord_row1[slot][0] = mat->texcoord_row1[slot].x;
          constants.texcoord_row1[slot][1] = mat->texcoord_row1[slot].y;
          constants.texcoord_row1[slot][2] = mat->texcoord_row1[slot].z;
          constants.texcoord_row1[slot][3] = mat->texcoord_row1[slot].w;
        }
      } else {
        for (size_t slot = 0; slot < MaterialRecord::kTextureCoordSlotCount; ++slot) {
          constants.texcoord_row0[slot][0] = 1.0f;
          constants.texcoord_row0[slot][1] = 0.0f;
          constants.texcoord_row0[slot][2] = 0.0f;
          constants.texcoord_row0[slot][3] = 0.0f;
          constants.texcoord_row1[slot][0] = 0.0f;
          constants.texcoord_row1[slot][1] = 1.0f;
          constants.texcoord_row1[slot][2] = 0.0f;
          constants.texcoord_row1[slot][3] = 0.0f;
        }
      }
    }
    if (mat && materialUsesCustomForwardPipeline(*mat)) {
      auto copy_custom_param = [](const glm::vec4& value, float out[4]) {
        out[0] = value.x;
        out[1] = value.y;
        out[2] = value.z;
        out[3] = value.w;
      };
      if (mat->custom_material_param_enabled[0]) {
        copy_custom_param(mat->custom_material_params[0], constants.material_params0);
      }
      if (mat->custom_material_param_enabled[1]) {
        copy_custom_param(mat->custom_material_params[1], constants.material_params1);
      }
      if (mat->custom_material_param_enabled[2]) {
        copy_custom_param(mat->custom_material_params[2], constants.material_params2);
      }
      if (mat->custom_material_param_enabled[3]) {
        copy_custom_param(mat->custom_material_params[3], constants.material_params3);
      }
      if (mat->custom_material_param_enabled[4]) {
        copy_custom_param(mat->custom_material_params[4], constants.material_params4);
      }
      if (mat->custom_material_param_enabled[5]) {
        copy_custom_param(mat->custom_material_params[5], constants.material_params5);
      }
      if (mat->custom_material_param_enabled[6]) {
        copy_custom_param(mat->custom_material_params[6], constants.material_params6);
      }
    }
    {
      Diligent::MapHelper<DrawConstants> mapped(
          context_, constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
      auto* mapped_constants = getMappedData(mapped);
      if (mapped_constants == nullptr) {
        return false;
      }
      *mapped_constants = constants;
    }
    last_constants_material = has_volume_params ? rendering::kInvalidMaterial : material_id;
    last_constants_mesh = has_volume_params ? rendering::kInvalidMesh : mesh_id;
    last_constants_render_mode =
        has_volume_params ? rendering::LodRenderMode::Mesh : render_mode;
    last_constants_scene_sample_mode =
        has_volume_params ? TransparentForwardDraw::SceneSampleMode::None : scene_sample_mode;
    return true;
  };

  auto resolve_forward_pipeline =
      [&](MaterialRecord* mat,
          ForwardPipelineVariant& variant,
          bool& custom_pipeline) -> Diligent::IPipelineState* {
    custom_pipeline = false;
    if (use_custom_shader_override) {
      variant = ForwardPipelineVariant::Transparent;
      return active_forward_pipeline;
    }
    const bool additive = mat && mat->blend_mode == rendering::MaterialDesc::BlendMode::Additive;
    const bool double_sided = mat && mat->desc.double_sided;
    if (additive && double_sided) {
      variant = ForwardPipelineVariant::AdditiveDoubleSided;
    } else if (additive) {
      variant = ForwardPipelineVariant::Additive;
    } else if (double_sided) {
      variant = ForwardPipelineVariant::TransparentDoubleSided;
    } else {
      variant = ForwardPipelineVariant::Transparent;
    }

    if (mat && materialUsesCustomForwardPipeline(*mat)) {
      if (Diligent::IPipelineState* custom = ensureCustomForwardPipeline(*mat, variant)) {
        custom_pipeline = true;
        return custom;
      }
    }

    if (Diligent::IPipelineState* pipeline = ensureForwardPipeline(variant)) {
      return pipeline;
    }
    return active_forward_pipeline;
  };

  auto resolve_forward_srb = [&](MaterialRecord* mat,
                                 ForwardPipelineVariant variant,
                                 bool custom_pipeline) -> Diligent::IShaderResourceBinding* {
    if (use_custom_shader_override) {
      return camera_override_srb_;
    }
    if (mat) {
      if (Diligent::IShaderResourceBinding* srb =
              ensureMaterialForwardSrb(*mat, variant, custom_pipeline)) {
        return srb;
      }
    }

    if (editorWireframeViewEnabled()) {
      const size_t slot =
          forwardPipelineVariantIndex(variant) * kInstanceGpuLayoutCount +
          instanceGpuLayoutIndex(rendering::InstanceGpuLayout::Matrix4x4Params);
      auto& wireframe_srb = editor_wireframe_default_material_srbs_[slot];
      return wireframe_srb ? wireframe_srb.RawPtr() : shader_resources_.RawPtr();
    }

    switch (variant) {
      case ForwardPipelineVariant::AdditiveDoubleSided:
        if (additive_double_sided_default_material_srb_) {
          return additive_double_sided_default_material_srb_;
        }
        break;
      case ForwardPipelineVariant::Additive:
        if (additive_default_material_srb_) {
          return additive_default_material_srb_;
        }
        break;
      case ForwardPipelineVariant::TransparentDoubleSided:
        if (transparent_double_sided_default_material_srb_) {
          return transparent_double_sided_default_material_srb_;
        }
        break;
      case ForwardPipelineVariant::Transparent:
        if (transparent_default_material_srb_) {
          return transparent_default_material_srb_;
        }
        break;
      case ForwardPipelineVariant::Opaque:
        if (default_material_srb_) {
          return default_material_srb_;
        }
        break;
      case ForwardPipelineVariant::OpaqueDoubleSided:
        if (opaque_double_sided_default_material_srb_) {
          return opaque_double_sided_default_material_srb_;
        }
        break;
      case ForwardPipelineVariant::DepthPrepass:
        break;
    }
    return default_material_srb_ ? default_material_srb_ : shader_resources_;
  };

  auto bind_forward_instance_buffer =
      [&](const MeshRecord& mesh,
          Diligent::IBuffer*& bound_mesh_vb,
          Diligent::IBuffer*& bound_instance_vb) -> bool {
    Diligent::IBuffer* mesh_vb = mesh.vertex_buffer.RawPtr();
    Diligent::IBuffer* instance_vb = instance_vb_.RawPtr();
    if (mesh_vb != bound_mesh_vb || instance_vb != bound_instance_vb) {
      Diligent::IBuffer* vbs[] = {mesh.vertex_buffer, instance_vb_};
      Diligent::Uint64 offsets[] = {0, 0};
      context_->SetVertexBuffers(0,
                                 2,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
      bound_mesh_vb = mesh_vb;
      bound_instance_vb = instance_vb;
    }
    return true;
  };

  auto bind_forward_geometry =
      [&](const MeshRecord& mesh,
          const InstanceGpuData* instances,
          size_t instance_count,
          Diligent::IBuffer*& bound_mesh_vb,
          Diligent::IBuffer*& bound_instance_vb) -> bool {
    if (!ensure_instance_buffer(instance_count) ||
        !uploadInstanceData(context_, instance_vb_, instances, instance_count)) {
      return false;
    }
    instancing_stats_.instance_buffer_updates += 1u;
    instancing_stats_.instance_upload_bytes +=
        static_cast<uint64_t>(instance_count * sizeof(InstanceGpuData));
    return bind_forward_instance_buffer(mesh, bound_mesh_vb, bound_instance_vb);
  };

  auto draw_forward_batch = [&](const MeshRecord& mesh,
                                const ForwardBatchKey& key,
                                Diligent::Uint32 instance_count,
                                Diligent::Uint32 first_instance,
                                Diligent::IBuffer*& bound_index_buffer) {
    auto log_draw = [&](const char* draw_kind) {
      if (!draw_debug) {
        return;
      }
      std::fprintf(stderr,
                   "[Karma][DrawDebug] pass=transparent-forward kind=%s mesh=%u material=%u "
                   "indexed=%s deformed=%s vertices=%u mesh_indices=%u "
                   "first_index=%u draw_indices=%u instances=%u vb=%s ib=%s\n",
                   draw_kind,
                   key.mesh,
                   key.material,
                   key.indexed ? "true" : "false",
                   key.deformed ? "true" : "false",
                   mesh.vertex_count,
                   mesh.index_count,
                   key.index_offset,
                   key.index_count,
                   instance_count,
                   mesh.vertex_buffer ? "yes" : "no",
                   mesh.index_buffer ? "yes" : "no");
      std::fflush(stderr);
    };
    if (key.indexed) {
      if (!is_valid_indexed_draw(mesh, key.index_offset, key.index_count)) {
        log_draw("invalid-indexed-skip");
        return false;
      }
      Diligent::IBuffer* index_buffer = mesh.index_buffer.RawPtr();
      if (index_buffer != bound_index_buffer) {
        context_->SetIndexBuffer(mesh.index_buffer,
                                 0,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_index_buffer = index_buffer;
      }
      Diligent::DrawIndexedAttribs indexed{};
      indexed.IndexType = Diligent::VT_UINT32;
      indexed.NumIndices = key.index_count;
      indexed.FirstIndexLocation = key.index_offset;
      indexed.NumInstances = instance_count;
      indexed.FirstInstanceLocation = first_instance;
      indexed.Flags = kHotPathDrawFlags;
      log_draw("indexed");
      context_->DrawIndexed(indexed);
      return true;
    }

    Diligent::DrawAttribs draw_attrs{};
    draw_attrs.NumVertices = mesh.vertex_count;
    draw_attrs.NumInstances = instance_count;
    draw_attrs.FirstInstanceLocation = first_instance;
    draw_attrs.Flags = kHotPathDrawFlags;
    log_draw("non-indexed");
    context_->Draw(draw_attrs);
    return true;
  };

  static thread_local std::vector<InstanceGpuData> transparent_packed_instances;
  transparent_packed_instances.clear();
  bool transparent_packed_instances_ready = false;
  constexpr size_t kMaxTransparentInstanceCount =
      static_cast<size_t>(std::numeric_limits<Diligent::Uint32>::max()) /
      sizeof(InstanceGpuData);
  if (draws.size() <= kMaxTransparentInstanceCount) {
    transparent_packed_instances.reserve(draws.size());
    for (const TransparentForwardDraw& draw : draws) {
      transparent_packed_instances.push_back(packInstanceData(draw.transform, draw.params));
    }
    if (ensure_instance_buffer(transparent_packed_instances.size()) &&
        uploadInstanceData(context_,
                           instance_vb_,
                           transparent_packed_instances.data(),
                           transparent_packed_instances.size())) {
      instancing_stats_.instance_buffer_updates += 1u;
      instancing_stats_.instance_upload_bytes += static_cast<uint64_t>(
          transparent_packed_instances.size() * sizeof(InstanceGpuData));
      transparent_packed_instances_ready = true;
    }
  }

  ensureParticleFallbackDepthResource();
  const Diligent::Viewport viewport = buildViewport(render_width, render_height);
  Diligent::ITextureView* transparent_dsv = particle_dsv ? particle_dsv : active_dsv;
  context_->SetRenderTargets(1,
                             &active_rtv,
                             transparent_dsv,
                             Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context_->SetViewports(1,
                         &viewport,
                         static_cast<Diligent::Uint32>(render_width),
                         static_cast<Diligent::Uint32>(render_height));

  Diligent::IPipelineState* bound_forward_pipeline = nullptr;
  Diligent::IShaderResourceBinding* bound_forward_srb = nullptr;
  Diligent::IBuffer* bound_mesh_vb = nullptr;
  Diligent::IBuffer* bound_instance_vb = nullptr;
  Diligent::IBuffer* bound_index_buffer = nullptr;
  Diligent::ITextureView* bound_scene_color = nullptr;
  Diligent::ITextureView* bound_scene_depth = nullptr;
  rendering::MaterialId last_constants_material = rendering::kInvalidMaterial;
  rendering::MeshId last_constants_mesh = rendering::kInvalidMesh;
  rendering::LodRenderMode last_constants_render_mode =
      rendering::LodRenderMode::Mesh;
  auto last_constants_scene_sample_mode = TransparentForwardDraw::SceneSampleMode::None;
  Diligent::Uint32 draw_count = 0;
  for (size_t draw_index = 0u; draw_index < draws.size(); ++draw_index) {
    const TransparentForwardDraw& draw = draws[draw_index];
    auto mesh_it = meshes_.find(draw.key.mesh);
    if (mesh_it == meshes_.end()) {
      continue;
    }
    const auto& mesh = mesh_it->second;
    if (!mesh.vertex_buffer) {
      continue;
    }

    MaterialRecord* mat = lookup_material(draw.key.material);
    ForwardPipelineVariant pipeline_variant = ForwardPipelineVariant::Transparent;
    bool custom_pipeline = false;
    Diligent::IPipelineState* pipeline =
        resolve_forward_pipeline(mat, pipeline_variant, custom_pipeline);
    if (!pipeline) {
      continue;
    }
    if (pipeline != bound_forward_pipeline) {
      context_->SetPipelineState(pipeline);
      bound_forward_pipeline = pipeline;
      bound_forward_srb = nullptr;
      bound_mesh_vb = nullptr;
      bound_instance_vb = nullptr;
      bound_index_buffer = nullptr;
    }

    if (!update_forward_material_constants(draw.key.material,
                                           draw.key.mesh,
                                           mesh,
                                           mat,
                                           draw.key.render_mode,
                                           draw.scene_sample_mode,
                                           draw.volume_params,
                                           draw.has_volume_params,
                                           last_constants_material,
                                           last_constants_mesh,
                                           last_constants_render_mode,
                                           last_constants_scene_sample_mode)) {
      continue;
    }

    Diligent::IShaderResourceBinding* srb =
        resolve_forward_srb(mat, pipeline_variant, custom_pipeline);
    if (srb &&
        (draw.scene_sample_mode != TransparentForwardDraw::SceneSampleMode::None ||
         (mat &&
          (mat->shading_model == MaterialPipelineKind::WaveVolume ||
           mat->shading_model == MaterialPipelineKind::VolumetricSolid ||
           (mat->shading_model == MaterialPipelineKind::Standard &&
            mat->transmission_factor > 0.001f))))) {
      Diligent::ITextureView* desired_scene_color =
          scene_color_sample_srv ? scene_color_sample_srv : default_base_color_;
      Diligent::ITextureView* desired_scene_depth =
          scene_depth_sample_srv ? scene_depth_sample_srv : particle_fallback_depth_srv_;
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneColor")) {
        if (desired_scene_color != bound_scene_color || srb != bound_forward_srb) {
          var->Set(desired_scene_color);
          bound_scene_color = desired_scene_color;
          bound_forward_srb = nullptr;
        }
      }
      if (auto* var = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SceneDepth")) {
        if (desired_scene_depth != bound_scene_depth || srb != bound_forward_srb) {
          var->Set(desired_scene_depth);
          bound_scene_depth = desired_scene_depth;
          bound_forward_srb = nullptr;
        }
      }
    }
    const rendering::DeformationId deformation =
        draw.key.deformed ? draw.deformation : rendering::kInvalidDeformation;
    if (srb && !bindDeformationResources(srb, mesh, deformation)) {
      continue;
    }
    if (srb) {
      context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      bound_forward_srb = srb;
    } else if (!srb && !updateDeformationConstants(mesh, deformation)) {
      continue;
    }

    Diligent::Uint32 first_instance = 0u;
    if (transparent_packed_instances_ready) {
      if (!bind_forward_instance_buffer(mesh, bound_mesh_vb, bound_instance_vb)) {
        continue;
      }
      first_instance = static_cast<Diligent::Uint32>(draw_index);
    } else {
      const InstanceGpuData packed_transform = packInstanceData(draw.transform, draw.params);
      if (!bind_forward_geometry(mesh,
                                 &packed_transform,
                                 1u,
                                 bound_mesh_vb,
                                 bound_instance_vb)) {
        continue;
      }
    }
    if (draw_forward_batch(mesh, draw.key, 1u, first_instance, bound_index_buffer)) {
      draw_count += 1;
    }
  }

  return draw_count;
}

bool DiligentBackend::forwardDrawsRequireSceneColorCopy(
    const std::vector<TransparentForwardDraw>& draws) const {
  for (const auto& draw : draws) {
    if (draw.key.material == rendering::kInvalidMaterial) {
      continue;
    }
    auto mat_it = materials_.find(draw.key.material);
    if (mat_it == materials_.end()) {
      continue;
    }
    const auto& mat = mat_it->second;
    if (draw.scene_sample_mode != TransparentForwardDraw::SceneSampleMode::None) {
      return true;
    }
    if (mat.shading_model == MaterialPipelineKind::WaveVolume ||
        mat.shading_model == MaterialPipelineKind::VolumetricSolid) {
      return true;
    }
  }
  return false;
}

}  // namespace karma::rendering::backend
