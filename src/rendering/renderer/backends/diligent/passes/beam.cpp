#include "../backend.hpp"

#include "../backend_internal.h"
#include "pass_shared.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <tuple>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace karma::rendering::backend {

namespace {

static constexpr const char* kBeamVS = R"(
cbuffer Constants
{
    row_major float4x4 g_ViewProj;
};

struct VSInput
{
    float3 pos : ATTRIB0;
    float2 uv : ATTRIB1;
    float4 col : ATTRIB2;
    float4 params : ATTRIB3;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float4 params : TEXCOORD1;
};

PSInput main(VSInput input)
{
    PSInput output;
    output.pos = mul(float4(input.pos, 1.0), g_ViewProj);
    output.uv = input.uv;
    output.col = input.col;
    output.params = input.params;
    return output;
}
)";

static constexpr const char* kBeamPS = R"(
Texture2D g_Texture;
SamplerState g_Texture_sampler;

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float4 params : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 texel = g_Texture.Sample(g_Texture_sampler, input.uv);
    float4 color = texel * input.col;
    float softness = saturate(input.params.x);
    if (softness > 1.0e-4)
    {
        float edge = min(input.uv.y, 1.0 - input.uv.y);
        color.a *= smoothstep(0.0, softness, edge);
    }
    return color;
}
)";

template <typename T, bool KeepStrongReferences = false>
T* mappedData(Diligent::MapHelper<T, KeepStrongReferences>& map) {
  return static_cast<T*>(map);
}

glm::vec3 toBeamGlm(const math::Vec3& value) {
  return {value.x, value.y, value.z};
}

glm::quat toBeamGlm(const math::Quat& value) {
  return {value.w, value.x, value.y, value.z};
}

glm::vec4 toBeamGlm(const math::Color& value) {
  return {value.r, value.g, value.b, value.a};
}

float saturate(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

bool finiteVec3(const glm::vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

glm::vec3 safeNormalize(const glm::vec3& value, const glm::vec3& fallback) {
  if (!finiteVec3(value)) {
    return fallback;
  }
  const float len2 = glm::dot(value, value);
  if (!std::isfinite(len2) || len2 <= 1.0e-8f) {
    return fallback;
  }
  return value * (1.0f / std::sqrt(len2));
}

glm::vec3 transformPoint(const rendering::ParticleBeamGpuDesc& beam,
                         const math::Vec3& point) {
  const glm::vec3 position = toBeamGlm(beam.position);
  const glm::quat rotation = toBeamGlm(beam.rotation);
  const glm::vec3 scale = toBeamGlm(beam.scale);
  return position + rotation * (toBeamGlm(point) * scale);
}

}  // namespace

void DiligentBackend::ensureParticleBeamResources() {
  if (particle_beam_pipeline_additive_depth_ &&
      particle_beam_pipeline_additive_no_depth_ &&
      particle_beam_pipeline_alpha_depth_ &&
      particle_beam_pipeline_alpha_no_depth_ &&
      particle_beam_vb_ &&
      particle_beam_cb_) {
    return;
  }
  if (!device_) {
    return;
  }

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  shader_ci.Desc.Name = "Karma Particle Beam VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kBeamVS;
  vs = device_with_cache_.CreateShader(shader_ci);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  shader_ci.Desc.Name = "Karma Particle Beam PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kBeamPS;
  ps = device_with_cache_.CreateShader(shader_ci);

  if (!vs || !ps) {
    return;
  }

  if (!particle_beam_cb_) {
    Diligent::BufferDesc cb_desc{};
    cb_desc.Name = "Karma Particle Beam Constants";
    cb_desc.Usage = Diligent::USAGE_DYNAMIC;
    cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    cb_desc.Size = sizeof(ParticleBeamConstants);
    const auto cb_start = core::SteadyClock::now();
    device_->CreateBuffer(cb_desc, nullptr, &particle_beam_cb_);
    recordResourceCreation("particle_beam",
                           "constants buffer",
                           cb_start,
                           core::SteadyClock::now());
  }
  if (!particle_beam_cb_) {
    return;
  }

  Diligent::LayoutElement layout[] = {
      Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(
                                  offsetof(ParticleBeamVertex, position)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleBeamVertex))},
      Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(
                                  offsetof(ParticleBeamVertex, uv)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleBeamVertex))},
      Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(
                                  offsetof(ParticleBeamVertex, color)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleBeamVertex))},
      Diligent::LayoutElement{3, 0, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(
                                  offsetof(ParticleBeamVertex, params)),
                              static_cast<Diligent::Uint32>(sizeof(ParticleBeamVertex))},
  };

  static const Diligent::ShaderResourceVariableDesc kBeamVars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_PIXEL, "g_Texture", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
  };
  static const Diligent::SamplerDesc kBeamSamplerDesc{
      Diligent::FILTER_TYPE_LINEAR,
      Diligent::FILTER_TYPE_LINEAR,
      Diligent::FILTER_TYPE_LINEAR,
      Diligent::TEXTURE_ADDRESS_WRAP,
      Diligent::TEXTURE_ADDRESS_CLAMP,
      Diligent::TEXTURE_ADDRESS_CLAMP};
  static const Diligent::ImmutableSamplerDesc kBeamSamplers[] = {
      {Diligent::SHADER_TYPE_PIXEL, "g_Texture_sampler", kBeamSamplerDesc},
  };

  auto create_pipeline =
      [&](const char* name,
          bool depth_test,
          rendering::ParticleBlendMode blend_mode,
          Diligent::RefCntAutoPtr<Diligent::IPipelineState>& out_pso,
          Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& out_srb,
          Diligent::IShaderResourceVariable*& out_texture_var) {
    if (out_pso && out_srb) {
      return true;
    }

    Diligent::GraphicsPipelineStateCreateInfo pso{};
    pso.PSODesc.Name = name;
    pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    pso.pVS = vs;
    pso.pPS = ps;

    auto& graphics = pso.GraphicsPipeline;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = swap_chain_ ? swap_chain_->GetDesc().ColorBufferFormat
                                         : Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;
    graphics.DSVFormat = swap_chain_ ? swap_chain_->GetDesc().DepthBufferFormat
                                     : Diligent::TEX_FORMAT_D32_FLOAT;
    graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    graphics.DepthStencilDesc.DepthEnable = depth_test;
    graphics.DepthStencilDesc.DepthWriteEnable = false;

    auto& blend = graphics.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    const bool alpha = blend_mode == rendering::ParticleBlendMode::Alpha;
    blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = alpha ? Diligent::BLEND_FACTOR_INV_SRC_ALPHA : Diligent::BLEND_FACTOR_ONE;
    blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
    blend.SrcBlendAlpha = alpha ? Diligent::BLEND_FACTOR_SRC_ALPHA : Diligent::BLEND_FACTOR_ONE;
    blend.DestBlendAlpha = alpha ? Diligent::BLEND_FACTOR_INV_SRC_ALPHA : Diligent::BLEND_FACTOR_ONE;
    blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
    blend.RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

    graphics.InputLayout.LayoutElements = layout;
    graphics.InputLayout.NumElements =
        static_cast<Diligent::Uint32>(sizeof(layout) / sizeof(layout[0]));

    pso.PSODesc.ResourceLayout.Variables = kBeamVars;
    pso.PSODesc.ResourceLayout.NumVariables =
        static_cast<Diligent::Uint32>(sizeof(kBeamVars) / sizeof(kBeamVars[0]));
    pso.PSODesc.ResourceLayout.ImmutableSamplers = kBeamSamplers;
    pso.PSODesc.ResourceLayout.NumImmutableSamplers =
        static_cast<Diligent::Uint32>(sizeof(kBeamSamplers) / sizeof(kBeamSamplers[0]));

    const auto pso_start = core::SteadyClock::now();
    out_pso = device_with_cache_.CreateGraphicsPipelineState(pso);
    recordPipelineCreation("particle_beam", name, pso_start, core::SteadyClock::now());
    if (!out_pso) {
      return false;
    }
    if (auto* var =
            out_pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
      var->Set(particle_beam_cb_);
    }
    const auto srb_start = core::SteadyClock::now();
    out_pso->CreateShaderResourceBinding(&out_srb, true);
    recordResourceCreation("particle_beam", name, srb_start, core::SteadyClock::now());
    if (out_srb) {
      out_texture_var =
          out_srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture");
      if (out_texture_var && default_base_color_) {
        out_texture_var->Set(default_base_color_);
      }
    }
    return out_srb != nullptr;
  };

  create_pipeline("Karma Particle Beam Additive (Depth)",
                  true,
                  rendering::ParticleBlendMode::Additive,
                  particle_beam_pipeline_additive_depth_,
                  particle_beam_srb_additive_depth_,
                  particle_beam_texture_var_additive_depth_);
  create_pipeline("Karma Particle Beam Additive (NoDepth)",
                  false,
                  rendering::ParticleBlendMode::Additive,
                  particle_beam_pipeline_additive_no_depth_,
                  particle_beam_srb_additive_no_depth_,
                  particle_beam_texture_var_additive_no_depth_);
  create_pipeline("Karma Particle Beam Alpha (Depth)",
                  true,
                  rendering::ParticleBlendMode::Alpha,
                  particle_beam_pipeline_alpha_depth_,
                  particle_beam_srb_alpha_depth_,
                  particle_beam_texture_var_alpha_depth_);
  create_pipeline("Karma Particle Beam Alpha (NoDepth)",
                  false,
                  rendering::ParticleBlendMode::Alpha,
                  particle_beam_pipeline_alpha_no_depth_,
                  particle_beam_srb_alpha_no_depth_,
                  particle_beam_texture_var_alpha_no_depth_);

  if (!particle_beam_vb_) {
    particle_beam_vb_size_ = 1024u;
    Diligent::BufferDesc vb_desc{};
    vb_desc.Name = "Karma Particle Beam VB";
    vb_desc.Usage = Diligent::USAGE_DYNAMIC;
    vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    vb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    vb_desc.Size =
        static_cast<Diligent::Uint32>(particle_beam_vb_size_ * sizeof(ParticleBeamVertex));
    const auto vb_start = core::SteadyClock::now();
    device_->CreateBuffer(vb_desc, nullptr, &particle_beam_vb_);
    recordResourceCreation("particle_beam",
                           "vertex buffer",
                           vb_start,
                           core::SteadyClock::now());
    if (!particle_beam_vb_) {
      particle_beam_vb_size_ = 0u;
    }
  }
}

uint32_t DiligentBackend::renderParticleBeams(rendering::LayerId layer,
                                             const ParticlePassContext& context) {
  if (particle_beam_submissions_.empty() || !device_ || !context_ || !context.active_rtv) {
    return 0u;
  }
  ensureParticleBeamResources();
  if (!particle_beam_vb_ || !particle_beam_cb_) {
    return 0u;
  }

  using GroupKey = std::tuple<bool, rendering::ParticleBlendMode, rendering::TextureId>;
  std::vector<GroupKey> groups;
  groups.reserve(particle_beam_submissions_.size());
  for (const ParticleBeamSubmission& submission : particle_beam_submissions_) {
    const auto& beam = submission.desc;
    if (beam.layer != layer ||
        !beam.enabled ||
        !beam.visible ||
        beam.local_path_points.size() < 2u ||
        beam.blend_mode == rendering::ParticleBlendMode::Distortion ||
        beam.start_width <= 0.0f ||
        beam.end_width <= 0.0f) {
      continue;
    }
    const GroupKey key{beam.depth_test, beam.blend_mode, beam.texture};
    if (std::find(groups.begin(), groups.end(), key) == groups.end()) {
      groups.push_back(key);
    }
  }
  if (groups.empty()) {
    return 0u;
  }

  Diligent::ITextureView* rtv = context.active_rtv;
  Diligent::ITextureView* dsv = context.particle_dsv ? context.particle_dsv : context.active_dsv;
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::Viewport viewport{};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(std::max(context.render_width, 1));
  viewport.Height = static_cast<float>(std::max(context.render_height, 1));
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->SetViewports(1,
                         &viewport,
                         static_cast<Diligent::Uint32>(std::max(context.render_width, 1)),
                         static_cast<Diligent::Uint32>(std::max(context.render_height, 1)));

  ParticleBeamConstants constants{};
  copyMat4(constants.view_proj, context.view_proj);
  {
    Diligent::MapHelper<ParticleBeamConstants> cb_map(context_,
                                                      particle_beam_cb_,
                                                      Diligent::MAP_WRITE,
                                                      Diligent::MAP_FLAG_DISCARD);
    auto* mapped = mappedData(cb_map);
    if (mapped == nullptr) {
      return 0u;
    }
    *mapped = constants;
  }

  uint32_t draw_calls = 0u;
  std::vector<ParticleBeamVertex> vertices;
  auto resolve_beam_texture = [&](rendering::TextureId texture) {
    if (texture != rendering::kInvalidTexture) {
      const auto it = textures_.find(texture);
      if (it != textures_.end() && it->second.srv) {
        return it->second.srv.RawPtr();
      }
    }
    return default_base_color_.RawPtr();
  };

  for (const GroupKey& group : groups) {
    const bool depth_test = std::get<0>(group);
    const rendering::ParticleBlendMode blend_mode = std::get<1>(group);
    const rendering::TextureId texture_id = std::get<2>(group);
    vertices.clear();

    for (const ParticleBeamSubmission& submission : particle_beam_submissions_) {
      const auto& beam = submission.desc;
      if (beam.layer != layer ||
          !beam.enabled ||
          !beam.visible ||
          beam.depth_test != depth_test ||
          beam.blend_mode != blend_mode ||
          beam.texture != texture_id ||
          beam.local_path_points.size() < 2u ||
          beam.start_width <= 0.0f ||
          beam.end_width <= 0.0f) {
        continue;
      }

      std::vector<glm::vec3> points;
      points.reserve(beam.local_path_points.size());
      for (const math::Vec3& point : beam.local_path_points) {
        const glm::vec3 world_point = transformPoint(beam, point);
        if (finiteVec3(world_point)) {
          points.push_back(world_point);
        }
      }
      if (points.size() < 2u) {
        continue;
      }

      std::vector<float> distances(points.size(), 0.0f);
      for (std::size_t i = 1; i < points.size(); ++i) {
        const float segment_len = glm::length(points[i] - points[i - 1]);
        distances[i] = distances[i - 1] + (std::isfinite(segment_len) ? segment_len : 0.0f);
      }
      const float total_length = std::max(distances.back(), 1.0e-4f);
      const float edge_softness = std::clamp(beam.edge_softness, 0.0f, 0.49f);
      const float uv_repeat = std::max(beam.uv_repeat, 0.0f);
      const float uv_scroll = submission.elapsed_seconds * beam.uv_scroll_speed;
      const glm::vec4 start_color = toBeamGlm(beam.start_color);
      const glm::vec4 end_color = toBeamGlm(beam.end_color);
      const glm::vec3 fallback_side =
          safeNormalize(context.camera_right, glm::vec3(1.0f, 0.0f, 0.0f));

      auto push_vertex = [&](const glm::vec3& pos,
                             float u,
                             float v,
                             const glm::vec4& color) {
        ParticleBeamVertex vertex{};
        vertex.position[0] = pos.x;
        vertex.position[1] = pos.y;
        vertex.position[2] = pos.z;
        vertex.uv[0] = u;
        vertex.uv[1] = v;
        vertex.color[0] = color.r;
        vertex.color[1] = color.g;
        vertex.color[2] = color.b;
        vertex.color[3] = color.a;
        vertex.params[0] = edge_softness;
        vertices.push_back(vertex);
      };

      std::vector<glm::vec3> segment_sides;
      segment_sides.reserve(points.size() - 1u);
      glm::vec3 previous_side = fallback_side;
      for (std::size_t i = 1; i < points.size(); ++i) {
        const glm::vec3 segment = points[i] - points[i - 1];
        const glm::vec3 dir = safeNormalize(segment, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 side = safeNormalize(glm::cross(dir, context.camera_forward),
                                       previous_side);
        if (!segment_sides.empty() && glm::dot(side, segment_sides.back()) < 0.0f) {
          side = -side;
        }
        segment_sides.push_back(side);
        previous_side = side;
      }

      std::vector<glm::vec3> offsets(points.size(), glm::vec3(0.0f));
      for (std::size_t i = 0; i < points.size(); ++i) {
        glm::vec3 side = segment_sides.front();
        float side_scale = 1.0f;
        if (i == 0u) {
          side = segment_sides.front();
        } else if (i + 1u == points.size()) {
          side = segment_sides.back();
        } else {
          const glm::vec3 previous = segment_sides[i - 1u];
          const glm::vec3 next = segment_sides[i];
          side = safeNormalize(previous + next, next);
          const float miter_dot = std::abs(glm::dot(side, next));
          side_scale = std::clamp(1.0f / std::max(miter_dot, 0.35f), 1.0f, 2.25f);
        }

        const float t = saturate(distances[i] / total_length);
        const float width = std::max(beam.start_width +
                                         (beam.end_width - beam.start_width) * t,
                                     0.0f);
        offsets[i] = side * (width * 0.5f * side_scale);
      }

      for (std::size_t i = 1; i < points.size(); ++i) {
        if (distances[i] <= distances[i - 1]) {
          continue;
        }

        const float t0 = saturate(distances[i - 1] / total_length);
        const float t1 = saturate(distances[i] / total_length);
        const glm::vec4 color0 = start_color + (end_color - start_color) * t0;
        const glm::vec4 color1 = start_color + (end_color - start_color) * t1;
        const float u0 = t0 * uv_repeat + uv_scroll;
        const float u1 = t1 * uv_repeat + uv_scroll;
        const glm::vec3 a = points[i - 1] - offsets[i - 1];
        const glm::vec3 b = points[i - 1] + offsets[i - 1];
        const glm::vec3 c = points[i] + offsets[i];
        const glm::vec3 d = points[i] - offsets[i];

        push_vertex(a, u0, 0.0f, color0);
        push_vertex(b, u0, 1.0f, color0);
        push_vertex(c, u1, 1.0f, color1);
        push_vertex(a, u0, 0.0f, color0);
        push_vertex(c, u1, 1.0f, color1);
        push_vertex(d, u1, 0.0f, color1);
      }
    }

    if (vertices.empty()) {
      continue;
    }

    if (vertices.size() > particle_beam_vb_size_) {
      const size_t new_capacity =
          std::max(vertices.size(),
                   particle_beam_vb_size_ > 0u ? particle_beam_vb_size_ * 2u
                                                : static_cast<size_t>(1024u));
      Diligent::BufferDesc vb_desc{};
      vb_desc.Name = "Karma Particle Beam VB";
      vb_desc.Usage = Diligent::USAGE_DYNAMIC;
      vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
      vb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
      vb_desc.Size =
          static_cast<Diligent::Uint32>(new_capacity * sizeof(ParticleBeamVertex));
      particle_beam_vb_.Release();
      const auto vb_start = core::SteadyClock::now();
      device_->CreateBuffer(vb_desc, nullptr, &particle_beam_vb_);
      recordResourceCreation("particle_beam",
                             "dynamic vertex buffer resize",
                             vb_start,
                             core::SteadyClock::now());
      if (!particle_beam_vb_) {
        particle_beam_vb_size_ = 0u;
        return draw_calls;
      }
      particle_beam_vb_size_ = new_capacity;
    }

    {
      Diligent::MapHelper<ParticleBeamVertex> vb_map(context_,
                                                     particle_beam_vb_,
                                                     Diligent::MAP_WRITE,
                                                     Diligent::MAP_FLAG_DISCARD);
      auto* mapped = mappedData(vb_map);
      if (mapped == nullptr) {
        continue;
      }
      std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(ParticleBeamVertex));
    }

    Diligent::IPipelineState* pso = nullptr;
    Diligent::IShaderResourceBinding* srb = nullptr;
    Diligent::IShaderResourceVariable* texture_var = nullptr;
    if (blend_mode == rendering::ParticleBlendMode::Alpha) {
      pso = depth_test ? particle_beam_pipeline_alpha_depth_.RawPtr()
                       : particle_beam_pipeline_alpha_no_depth_.RawPtr();
      srb = depth_test ? particle_beam_srb_alpha_depth_.RawPtr()
                       : particle_beam_srb_alpha_no_depth_.RawPtr();
      texture_var = depth_test ? particle_beam_texture_var_alpha_depth_
                               : particle_beam_texture_var_alpha_no_depth_;
    } else {
      pso = depth_test ? particle_beam_pipeline_additive_depth_.RawPtr()
                       : particle_beam_pipeline_additive_no_depth_.RawPtr();
      srb = depth_test ? particle_beam_srb_additive_depth_.RawPtr()
                       : particle_beam_srb_additive_no_depth_.RawPtr();
      texture_var = depth_test ? particle_beam_texture_var_additive_depth_
                               : particle_beam_texture_var_additive_no_depth_;
    }
    if (pso == nullptr) {
      continue;
    }

    Diligent::ITextureView* texture = resolve_beam_texture(texture_id);
    if (texture_var && texture) {
      texture_var->Set(texture, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }

    context_->SetPipelineState(pso);
    if (srb != nullptr) {
      context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    Diligent::IBuffer* vbs[] = {particle_beam_vb_};
    Diligent::Uint64 offsets[] = {0};
    context_->SetVertexBuffers(0,
                               1,
                               vbs,
                               offsets,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                               Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

    Diligent::DrawAttribs draw{};
    draw.NumVertices = static_cast<Diligent::Uint32>(std::min<std::size_t>(
        vertices.size(),
        static_cast<std::size_t>(std::numeric_limits<Diligent::Uint32>::max())));
    draw.Flags = Diligent::DRAW_FLAG_NONE;
    context_->Draw(draw);
    draw_calls += 1u;
    particle_pass_stats_.beam_draw_calls += 1u;
  }

  return draw_calls;
}

}  // namespace karma::rendering::backend
