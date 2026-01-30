#include "karma/renderer/backends/diligent/backend.hpp"

#include "backend_internal.h"

#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/SwapChain.h>
#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

namespace karma::renderer_backend {

namespace {
struct alignas(16) LineConstants {
  float view_proj[16];
};

static constexpr const char* kLineVS = R"(
cbuffer Constants
{
    row_major float4x4 g_ViewProj;
};

struct VSInput
{
    float4 pos : ATTRIB0;
    float4 col : ATTRIB1;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
};

PSInput main(VSInput input)
{
    PSInput output;
    output.pos = mul(input.pos, g_ViewProj);
    output.col = input.col;
    return output;
}
)";

static constexpr const char* kLinePS = R"(
struct PSInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.col;
}
)";

glm::mat4 buildLightView(const renderer::DirectionalLightData& light) {
  glm::vec3 dir = light.direction;
  if (glm::length(dir) < 1e-4f) {
    dir = glm::vec3(0.3f, -1.0f, 0.2f);
  }
  const glm::vec3 z = glm::normalize(dir);
  glm::vec3 x;
  const float min_cmp = std::min({std::abs(z.x), std::abs(z.y), std::abs(z.z)});
  if (min_cmp == std::abs(z.x)) {
    x = glm::vec3(1.0f, 0.0f, 0.0f);
  } else if (min_cmp == std::abs(z.y)) {
    x = glm::vec3(0.0f, 1.0f, 0.0f);
  } else {
    x = glm::vec3(0.0f, 0.0f, 1.0f);
  }
  glm::vec3 y = glm::normalize(glm::cross(z, x));
  x = glm::normalize(glm::cross(y, z));

  glm::mat4 view(1.0f);
  view[0][0] = x.x;
  view[1][0] = x.y;
  view[2][0] = x.z;
  view[0][1] = y.x;
  view[1][1] = y.y;
  view[2][1] = y.z;
  view[0][2] = z.x;
  view[1][2] = z.y;
  view[2][2] = z.z;
  return view;
}

float maxScaleComponent(const glm::mat4& m) {
  const glm::vec3 x{m[0][0], m[0][1], m[0][2]};
  const glm::vec3 y{m[1][0], m[1][1], m[1][2]};
  const glm::vec3 z{m[2][0], m[2][1], m[2][2]};
  return std::max({glm::length(x), glm::length(y), glm::length(z)});
}

}  // namespace

void DiligentBackend::beginFrame(const renderer::FrameInfo& frame) {
  if (isValidSize(frame.width, frame.height) &&
      (frame.width != current_width_ || frame.height != current_height_)) {
    resize(frame.width, frame.height);
  }
}

void DiligentBackend::endFrame() {
  if (swap_chain_) {
    swap_chain_->Present();
  }
  if (!line_vertices_depth_.empty()) {
    line_vertices_depth_.clear();
  }
  if (!line_vertices_no_depth_.empty()) {
    line_vertices_no_depth_.clear();
  }
}

void DiligentBackend::resize(int width, int height) {
  if (!isValidSize(width, height)) {
    return;
  }

  current_width_ = width;
  current_height_ = height;
  if (swap_chain_) {
    swap_chain_->Resize(static_cast<Diligent::Uint32>(width),
                        static_cast<Diligent::Uint32>(height));
  }
}

void DiligentBackend::submit(const renderer::DrawItem& item) {
  if (item.instance == renderer::kInvalidInstance) {
    return;
  }

  if (meshes_.find(item.mesh) == meshes_.end()) {
    spdlog::warn("Karma: Diligent submit missing mesh id={}", item.mesh);
    return;
  }

  auto& record = instances_[item.instance];
  record.layer = item.layer;
  record.mesh = item.mesh;
  record.material = item.material;
  record.transform = item.transform;
  record.visible = item.visible;
  record.shadow_visible = item.shadow_visible;
}

void DiligentBackend::drawLine(const math::Vec3& start, const math::Vec3& end,
                               const math::Color& color, bool depth_test, float thickness) {
  if (!warned_line_thickness_ && thickness != 1.0f) {
    spdlog::warn("Karma: Line thickness {} requested; only 1.0 is supported right now.", thickness);
    warned_line_thickness_ = true;
  }
  LineVertex a{};
  a.position[0] = start.x;
  a.position[1] = start.y;
  a.position[2] = start.z;
  a.position[3] = 1.0f;
  a.color[0] = color.r;
  a.color[1] = color.g;
  a.color[2] = color.b;
  a.color[3] = color.a;

  LineVertex b{};
  b.position[0] = end.x;
  b.position[1] = end.y;
  b.position[2] = end.z;
  b.position[3] = 1.0f;
  b.color[0] = color.r;
  b.color[1] = color.g;
  b.color[2] = color.b;
  b.color[3] = color.a;

  auto& bucket = depth_test ? line_vertices_depth_ : line_vertices_no_depth_;
  bucket.push_back(a);
  bucket.push_back(b);
}

void DiligentBackend::ensureLineResources() {
  if (line_pipeline_state_depth_ && line_pipeline_state_no_depth_) {
    return;
  }
  if (!device_) {
    return;
  }

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.CompileFlags = Diligent::SHADER_COMPILE_FLAGS{};

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  shader_ci.Desc.Name = "Karma Line VS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kLineVS;
  device_->CreateShader(shader_ci, &vs);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  shader_ci.Desc.Name = "Karma Line PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kLinePS;
  device_->CreateShader(shader_ci, &ps);

  if (!vs || !ps) {
    spdlog::error("Karma: Failed to create line shaders.");
    return;
  }

  Diligent::LayoutElement layout[] = {
      Diligent::LayoutElement{0, 0, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(LineVertex, position)),
                              static_cast<Diligent::Uint32>(sizeof(LineVertex))},
      Diligent::LayoutElement{1, 0, 4, Diligent::VT_FLOAT32, false,
                              static_cast<Diligent::Uint32>(offsetof(LineVertex, color)),
                              static_cast<Diligent::Uint32>(sizeof(LineVertex))}
  };

  Diligent::ShaderResourceVariableDesc vars[] = {
      {Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC}
  };

  auto create_pipeline = [&](const char* name, bool depth_test,
                             Diligent::RefCntAutoPtr<Diligent::IPipelineState>& out_pso,
                             Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& out_srb) {
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
    graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_LINE_LIST;
    graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    if (depth_test) {
      graphics.RasterizerDesc.DepthBias = -1;
    }
    graphics.DepthStencilDesc.DepthEnable = depth_test;
    graphics.DepthStencilDesc.DepthWriteEnable = false;
    graphics.BlendDesc.RenderTargets[0].RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

    graphics.InputLayout.LayoutElements = layout;
    graphics.InputLayout.NumElements =
        static_cast<Diligent::Uint32>(sizeof(layout) / sizeof(layout[0]));

    pso.PSODesc.ResourceLayout.Variables = vars;
    pso.PSODesc.ResourceLayout.NumVariables =
        static_cast<Diligent::Uint32>(sizeof(vars) / sizeof(vars[0]));

    device_->CreateGraphicsPipelineState(pso, &out_pso);
    if (!out_pso) {
      spdlog::error("Karma: Failed to create {} pipeline state.", name);
      return false;
    }
    if (auto* var =
            out_pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants")) {
      var->Set(line_cb_);
    }
    out_pso->CreateShaderResourceBinding(&out_srb, true);
    return true;
  };

  Diligent::BufferDesc cb_desc{};
  cb_desc.Name = "Karma Line Constants";
  cb_desc.Usage = Diligent::USAGE_DYNAMIC;
  cb_desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
  cb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
  cb_desc.Size = sizeof(LineConstants);
  device_->CreateBuffer(cb_desc, nullptr, &line_cb_);

  if (!line_cb_) {
    spdlog::error("Karma: Failed to create line constants buffer.");
    return;
  }

  create_pipeline("Karma Line Pipeline (Depth)", true, line_pipeline_state_depth_, line_srb_depth_);
  create_pipeline("Karma Line Pipeline (NoDepth)", false, line_pipeline_state_no_depth_, line_srb_no_depth_);

  if (!line_vb_) {
    line_vb_size_ = 1024;
    Diligent::BufferDesc vb_desc{};
    vb_desc.Name = "Karma Line VB";
    vb_desc.Usage = Diligent::USAGE_DYNAMIC;
    vb_desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    vb_desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    vb_desc.Size = static_cast<Diligent::Uint32>(line_vb_size_ * sizeof(LineVertex));
    device_->CreateBuffer(vb_desc, nullptr, &line_vb_);
    if (!line_vb_) {
      spdlog::error("Karma: Failed to create line vertex buffer.");
      line_vb_size_ = 0;
    }
  }
}

void DiligentBackend::renderLayer(renderer::LayerId layer, renderer::RenderTargetId /*target*/) {
  if (!context_ || !swap_chain_) {
    return;
  }

  if (!camera_active_) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    clearFrame(black, true);
    return;
  }

  clearFrame(clear_color_, true);

  if (!pipeline_state_ || !shader_resources_ || !constants_) {
    if (!warned_no_draws_) {
      spdlog::warn("Karma: Diligent backend missing pipeline/resources; skipping draw.");
      warned_no_draws_ = true;
    }
    return;
  }

  const float aspect = (current_height_ > 0)
                           ? static_cast<float>(current_width_) / static_cast<float>(current_height_)
                           : camera_.aspect;
  glm::mat4 projection(1.0f);
  if (camera_.perspective) {
    projection = glm::perspective(glm::radians(camera_.fov_y_degrees),
                                  aspect,
                                  camera_.near_clip,
                                  camera_.far_clip);
  } else {
    projection = glm::ortho(camera_.ortho_left,
                            camera_.ortho_right,
                            camera_.ortho_bottom,
                            camera_.ortho_top,
                            camera_.near_clip,
                            camera_.far_clip);
  }
  glm::mat4 depth_fix(1.0f);
  depth_fix[2][2] = 0.5f;
  depth_fix[3][2] = 0.5f;
  const glm::mat3 cam_basis = glm::mat3_cast(camera_.rotation);
  const glm::vec3 forward = cam_basis * glm::vec3(0.0f, 0.0f, -1.0f);
  const glm::vec3 up = cam_basis * glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::mat4 view = glm::lookAt(camera_.position, camera_.position + forward, up);

  const glm::mat4 light_view = buildLightView(directional_light_);
  glm::vec3 light_min{std::numeric_limits<float>::max()};
  glm::vec3 light_max{std::numeric_limits<float>::lowest()};
  bool has_bounds = false;
  if (directional_light_.shadow_extent > 0.0f) {
    const glm::vec3 center_ls =
        glm::vec3(light_view * glm::vec4(directional_light_.position, 1.0f));
    const glm::vec3 extent{directional_light_.shadow_extent};
    light_min = center_ls - extent;
    light_max = center_ls + extent;
    has_bounds = true;
  }
  if (directional_light_.shadow_extent <= 0.0f) {
    for (const auto& entry : instances_) {
      const auto& instance = entry.second;
      if (instance.layer != layer || !instance.shadow_visible) {
        continue;
      }
      auto mesh_it = meshes_.find(instance.mesh);
      if (mesh_it == meshes_.end()) {
        continue;
      }
      const auto& mesh = mesh_it->second;
      const glm::vec3 world_center =
          glm::vec3(instance.transform * glm::vec4(mesh.bounds_center, 1.0f));
      const float radius = mesh.bounds_radius * maxScaleComponent(instance.transform);
      const glm::vec3 center_ls = glm::vec3(light_view * glm::vec4(world_center, 1.0f));
      const glm::vec3 extents{radius};
      light_min = glm::min(light_min, center_ls - extents);
      light_max = glm::max(light_max, center_ls + extents);
      has_bounds = true;
    }
  }
  if (!has_bounds) {
    light_min = glm::vec3(-50.0f, -50.0f, -50.0f);
    light_max = glm::vec3(50.0f, 50.0f, 50.0f);
  }

  const glm::vec3 extent = light_max - light_min;
  const bool is_gl = device_->GetDeviceInfo().IsGLDevice();
  const float scale_x = (extent.x > 0.0f) ? (2.0f / extent.x) : 1.0f;
  const float scale_y = (extent.y > 0.0f) ? (2.0f / extent.y) : 1.0f;
  const float scale_z = (extent.z > 0.0f) ? ((is_gl ? 2.0f : 1.0f) / extent.z) : 1.0f;
  const float bias_x = -light_min.x * scale_x - 1.0f;
  const float bias_y = -light_min.y * scale_y - 1.0f;
  const float bias_z = -light_min.z * scale_z + (is_gl ? -1.0f : 0.0f);

  const glm::mat4 scale_mat = glm::scale(glm::mat4(1.0f), glm::vec3(scale_x, scale_y, scale_z));
  const glm::mat4 bias_mat = glm::translate(glm::mat4(1.0f), glm::vec3(bias_x, bias_y, bias_z));
  const glm::mat4 shadow_proj = bias_mat * scale_mat;
  const glm::mat4 light_view_proj = shadow_proj * light_view;

  const auto& ndc = device_->GetDeviceInfo().GetNDCAttribs();
  const glm::mat4 uv_scale = glm::scale(glm::mat4(1.0f),
                                        glm::vec3(0.5f, ndc.YtoVScale, ndc.ZtoDepthScale));
  const glm::mat4 uv_bias = glm::translate(glm::mat4(1.0f),
                                           glm::vec3(0.5f, 0.5f, ndc.GetZtoDepthBias()));
  const glm::mat4 shadow_uv_proj = uv_bias * uv_scale * light_view_proj;

  if (shadow_pipeline_state_ && shadow_map_dsv_) {
    Diligent::Uint32 shadow_draws = 0;
    Diligent::Viewport shadow_viewport{};
    shadow_viewport.TopLeftX = 0.0f;
    shadow_viewport.TopLeftY = 0.0f;
    shadow_viewport.Width = static_cast<float>(shadow_map_size_);
    shadow_viewport.Height = static_cast<float>(shadow_map_size_);
    shadow_viewport.MinDepth = 0.0f;
    shadow_viewport.MaxDepth = 1.0f;
    context_->SetViewports(1, &shadow_viewport,
                           static_cast<Diligent::Uint32>(shadow_map_size_),
                           static_cast<Diligent::Uint32>(shadow_map_size_));
    context_->SetRenderTargets(0, nullptr, shadow_map_dsv_,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->ClearDepthStencil(shadow_map_dsv_,
                                Diligent::CLEAR_DEPTH_FLAG,
                                1.0f,
                                0,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context_->SetPipelineState(shadow_pipeline_state_);
    if (shadow_srb_) {
      context_->CommitShaderResources(shadow_srb_,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    for (const auto& entry : instances_) {
      const auto& instance = entry.second;
      if (instance.layer != layer || !instance.shadow_visible) {
        continue;
      }
      auto mesh_it = meshes_.find(instance.mesh);
      if (mesh_it == meshes_.end()) {
        continue;
      }
      const auto& mesh = mesh_it->second;
      if (!mesh.vertex_buffer) {
        continue;
      }

      const glm::mat4 shadow_mvp = light_view_proj * instance.transform;
      DrawConstants constants{};
      copyMat4(constants.mvp, shadow_mvp);
      copyMat4(constants.model, instance.transform);
      copyMat4(constants.light_view_proj, light_view_proj);
      copyMat4(constants.shadow_uv_proj, shadow_uv_proj);
      constants.shadow_params[0] = 0.0f;
      float fixed_bias = shadow_bias_;
      if (shadow_map_size_ >= 2048) {
        fixed_bias = 0.0025f;
      } else if (shadow_map_size_ >= 1024) {
        fixed_bias = 0.005f;
      } else {
        fixed_bias = 0.0075f;
      }
      constants.shadow_params[1] = fixed_bias;
      constants.shadow_params[2] = static_cast<float>(shadow_pcf_radius_);
      constants.shadow_params[3] = shadow_debug_ ? -static_cast<float>(shadow_map_size_)
                                                 : (shadow_map_size_ > 0
                                                        ? 1.0f / static_cast<float>(shadow_map_size_)
                                                        : 0.0f);

      {
        Diligent::MapHelper<DrawConstants> mapped(context_, constants_, Diligent::MAP_WRITE,
                                                  Diligent::MAP_FLAG_DISCARD);
        *mapped = constants;
      }

      Diligent::IBuffer* vbs[] = {mesh.vertex_buffer};
      Diligent::Uint64 offsets[] = {0};
      context_->SetVertexBuffers(0,
                                 1,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
      if (mesh.index_buffer && mesh.index_count > 0) {
        context_->SetIndexBuffer(mesh.index_buffer,
                                 0,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      }

      auto draw_shadow = [&](Diligent::Uint32 index_offset, Diligent::Uint32 index_count) {
        if (mesh.index_buffer && index_count > 0) {
          Diligent::DrawIndexedAttribs indexed{};
          indexed.IndexType = Diligent::VT_UINT32;
          indexed.NumIndices = index_count;
          indexed.FirstIndexLocation = index_offset;
          indexed.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
          context_->DrawIndexed(indexed);
        } else {
          Diligent::DrawAttribs draw_attrs{};
          draw_attrs.NumVertices = mesh.vertex_count;
          draw_attrs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
          context_->Draw(draw_attrs);
        }
        shadow_draws += 1;
      };

      if (!mesh.submeshes.empty()) {
        for (const auto& submesh : mesh.submeshes) {
          draw_shadow(submesh.index_offset, submesh.index_count);
        }
      } else {
        draw_shadow(0, mesh.index_count);
      }
    }
    if (shadow_map_tex_) {
      Diligent::StateTransitionDesc barrier{};
      barrier.pResource = shadow_map_tex_;
      barrier.OldState = Diligent::RESOURCE_STATE_DEPTH_WRITE;
      barrier.NewState = Diligent::RESOURCE_STATE_SHADER_RESOURCE;
      barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
      context_->TransitionResourceStates(1, &barrier);
    }
  }

  auto* rtv = swap_chain_->GetCurrentBackBufferRTV();
  auto* dsv = swap_chain_->GetDepthBufferDSV();
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);

  Diligent::Viewport viewport{};
  viewport.TopLeftX = 0.0f;
  viewport.TopLeftY = 0.0f;
  viewport.Width = static_cast<float>(current_width_);
  viewport.Height = static_cast<float>(current_height_);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  context_->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(current_width_),
                         static_cast<Diligent::Uint32>(current_height_));

  context_->SetPipelineState(pipeline_state_);

  static bool logged_frame = false;
  if (!logged_frame) {
    spdlog::info("Karma: Diligent render layer {} viewport={}x{} aspect={}",
                 layer,
                 current_width_,
                 current_height_,
                 aspect);
    spdlog::info("Karma: Camera pos=({}, {}, {}) fov={} near={} far={}",
                 camera_.position.x,
                 camera_.position.y,
                 camera_.position.z,
                 camera_.fov_y_degrees,
                 camera_.near_clip,
                 camera_.far_clip);
    logged_frame = true;
  }

  Diligent::Uint32 draw_count = 0;
  Diligent::Uint32 skipped_hidden = 0;
  Diligent::Uint32 skipped_missing_vb = 0;
  Diligent::Uint32 skipped_missing_mesh = 0;
  Diligent::Uint32 skipped_layer = 0;
  for (const auto& entry : instances_) {
    const auto& instance = entry.second;
    if (instance.layer != layer) {
      skipped_layer += 1;
      continue;
    }
    if (!instance.visible) {
      skipped_hidden += 1;
      continue;
    }
    auto mesh_it = meshes_.find(instance.mesh);
    if (mesh_it == meshes_.end()) {
      skipped_missing_mesh += 1;
      continue;
    }

    const auto& mesh = mesh_it->second;
    if (!mesh.vertex_buffer) {
      skipped_missing_vb += 1;
      continue;
    }

    const glm::mat4 mvp = depth_fix * projection * view * instance.transform;
    DrawConstants constants{};
    copyMat4(constants.mvp, mvp);
    copyMat4(constants.model, instance.transform);
    copyMat4(constants.light_view_proj, light_view_proj);
    copyMat4(constants.shadow_uv_proj, shadow_uv_proj);
    const bool shadow_ready = shadow_pipeline_state_ && shadow_map_srv_ && shadow_map_dsv_ &&
                              shadow_sampler_;
    constants.shadow_params[0] = shadow_ready ? 1.0f : 0.0f;
    if (!shadow_ready) {
      spdlog::warn("Karma: Shadow not ready (pipeline={} dsv={} srv={} sampler={})",
                   shadow_pipeline_state_ ? 1 : 0,
                   shadow_map_dsv_ ? 1 : 0,
                   shadow_map_srv_ ? 1 : 0,
                   shadow_sampler_ ? 1 : 0);
    }
    float fixed_bias = shadow_bias_;
    if (shadow_map_size_ >= 2048) {
      fixed_bias = 0.0025f;
    } else if (shadow_map_size_ >= 1024) {
      fixed_bias = 0.005f;
    } else {
      fixed_bias = 0.0075f;
    }
    constants.shadow_params[1] = fixed_bias;
    constants.shadow_params[2] = static_cast<float>(shadow_pcf_radius_);
    constants.shadow_params[3] = shadow_debug_ ? -static_cast<float>(shadow_map_size_)
                                               : (shadow_map_size_ > 0
                                                      ? 1.0f / static_cast<float>(shadow_map_size_)
                                                      : 0.0f);

    glm::vec3 light_dir = directional_light_.direction;
    if (glm::length(light_dir) < 1e-4f) {
      light_dir = glm::vec3(0.3f, 1.0f, 0.2f);
    }
    light_dir = glm::normalize(light_dir);
    constants.light_dir[0] = light_dir.x;
    constants.light_dir[1] = light_dir.y;
    constants.light_dir[2] = light_dir.z;
    constants.light_dir[3] = 0.0f;
    constants.light_color[0] = directional_light_.color.r * directional_light_.intensity;
    constants.light_color[1] = directional_light_.color.g * directional_light_.intensity;
    constants.light_color[2] = directional_light_.color.b * directional_light_.intensity;
    constants.light_color[3] = 1.0f;
    constants.camera_pos[0] = camera_.position.x;
    constants.camera_pos[1] = camera_.position.y;
    constants.camera_pos[2] = camera_.position.z;
    constants.camera_pos[3] = 1.0f;

    Diligent::IBuffer* vbs[] = {mesh.vertex_buffer};
    Diligent::Uint64 offsets[] = {0};
    context_->SetVertexBuffers(0,
                               1,
                               vbs,
                               offsets,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                               Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

    if (mesh.index_buffer && mesh.index_count > 0) {
      context_->SetIndexBuffer(mesh.index_buffer,
                               0,
                               Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    auto draw_with_material = [&](renderer::MaterialId material,
                                  Diligent::Uint32 index_offset,
                                  Diligent::Uint32 index_count) {
      const MaterialRecord* mat = nullptr;
      if (material != renderer::kInvalidMaterial) {
        auto mat_it = materials_.find(material);
        if (mat_it != materials_.end()) {
          mat = &mat_it->second;
        }
      }

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
      constants.env_params[0] = environment_intensity_;
      constants.env_params[1] = 0.0f;
      constants.env_params[2] = 0.0f;
      constants.env_params[3] = 0.0f;

      {
        Diligent::MapHelper<DrawConstants> mapped(context_, constants_, Diligent::MAP_WRITE,
                                                  Diligent::MAP_FLAG_DISCARD);
        *mapped = constants;
      }

      Diligent::IShaderResourceBinding* srb = shader_resources_;
      if (mat && mat->srb) {
        srb = mat->srb;
      } else if (default_material_srb_) {
        srb = default_material_srb_;
      }
      if (srb) {
        context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
      }

      if (mesh.index_buffer && index_count > 0) {
        Diligent::DrawIndexedAttribs indexed{};
        indexed.IndexType = Diligent::VT_UINT32;
        indexed.NumIndices = index_count;
        indexed.FirstIndexLocation = index_offset;
        indexed.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
        context_->DrawIndexed(indexed);
      } else {
        Diligent::DrawAttribs draw_attrs{};
        draw_attrs.NumVertices = mesh.vertex_count;
        draw_attrs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
        context_->Draw(draw_attrs);
      }
      draw_count += 1;
    };

    if (!mesh.submeshes.empty()) {
      for (const auto& submesh : mesh.submeshes) {
        const renderer::MaterialId mat_id =
            (instance.material != renderer::kInvalidMaterial) ? instance.material : submesh.material;
        draw_with_material(mat_id, submesh.index_offset, submesh.index_count);
      }
    } else {
      draw_with_material(instance.material, 0, mesh.index_count);
    }
  }

  auto draw_lines = [&](const std::vector<LineVertex>& lines,
                        Diligent::RefCntAutoPtr<Diligent::IPipelineState>& pso,
                        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& srb) {
    if (lines.empty()) {
      return;
    }
    if (pso && line_cb_ && line_vb_) {
      if (lines.size() > line_vb_size_) {
        spdlog::warn("Karma: Line draw skipped ({} vertices > capacity {}).",
                     lines.size(), line_vb_size_);
      } else {
        auto* rtv = swap_chain_->GetCurrentBackBufferRTV();
        auto* dsv = swap_chain_->GetDepthBufferDSV();
        context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Diligent::Viewport viewport{};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(current_width_);
        viewport.Height = static_cast<float>(current_height_);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context_->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(current_width_),
                               static_cast<Diligent::Uint32>(current_height_));

        {
          Diligent::MapHelper<LineVertex> vb_map(context_, line_vb_, Diligent::MAP_WRITE,
                                                 Diligent::MAP_FLAG_DISCARD);
          std::memcpy(vb_map, lines.data(), lines.size() * sizeof(LineVertex));
        }

        const glm::mat4 view_proj = depth_fix * projection * view;
        LineConstants constants{};
        copyMat4(constants.view_proj, view_proj);
        {
          Diligent::MapHelper<LineConstants> cb_map(context_, line_cb_, Diligent::MAP_WRITE,
                                                    Diligent::MAP_FLAG_DISCARD);
          *cb_map = constants;
        }

        context_->SetPipelineState(pso);
        if (srb) {
          context_->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        Diligent::IBuffer* vbs[] = {line_vb_};
        Diligent::Uint64 offsets[] = {0};
        context_->SetVertexBuffers(0, 1, vbs, offsets,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                   Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

        Diligent::DrawAttribs draw{};
        draw.NumVertices = static_cast<Diligent::Uint32>(lines.size());
        draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
        context_->Draw(draw);
      }
    }
  };

  draw_lines(line_vertices_depth_, line_pipeline_state_depth_, line_srb_depth_);
  draw_lines(line_vertices_no_depth_, line_pipeline_state_no_depth_, line_srb_no_depth_);

  if (!warned_no_draws_) {
    spdlog::info("Karma: Diligent drew {} instance(s) this frame (instances={}).", draw_count,
                 instances_.size());
    spdlog::info("Karma: Diligent skipped: hidden={} missing_vb={} missing_mesh={} layer={}",
                 skipped_hidden,
                 skipped_missing_vb,
                 skipped_missing_mesh,
                 skipped_layer);
    warned_no_draws_ = true;
  }
}

unsigned int DiligentBackend::getRenderTargetTextureId(renderer::RenderTargetId /*target*/) const {
  return 0u;
}

void DiligentBackend::setCamera(const renderer::CameraData& camera) {
  camera_ = camera;
}

void DiligentBackend::setCameraActive(bool active) {
  camera_active_ = active;
}

void DiligentBackend::setDirectionalLight(const renderer::DirectionalLightData& light) {
  directional_light_ = light;
}

void DiligentBackend::setEnvironmentMap(const std::filesystem::path& path, float intensity) {
  environment_intensity_ = intensity;
  environment_map_ = path;
  if (!device_) {
    return;
  }

  if (path.empty()) {
    env_srv_ = default_env_;
  } else {
    env_srv_ = loadTextureFromFile(path, true, "environment");
    if (!env_srv_) {
      spdlog::warn("Karma: Failed to load environment map '{}'", path.string());
      env_srv_ = default_env_;
    }
  }

  if (pipeline_state_) {
    if (auto* var = pipeline_state_->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_EnvTex")) {
      var->Set(env_srv_);
    }
  }
}

void DiligentBackend::setAnisotropy(bool enabled, int level) {
  anisotropy_enabled_ = enabled;
  anisotropy_level_ = std::max(1, level);

  if (!device_) {
    return;
  }

  Diligent::SamplerDesc sampler_color{};
  sampler_color.MinFilter = enabled ? Diligent::FILTER_TYPE_ANISOTROPIC : Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MagFilter = enabled ? Diligent::FILTER_TYPE_ANISOTROPIC : Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MipFilter = enabled ? Diligent::FILTER_TYPE_ANISOTROPIC : Diligent::FILTER_TYPE_LINEAR;
  sampler_color.MaxAnisotropy = static_cast<Diligent::Uint8>(std::clamp(anisotropy_level_, 1, 16));
  sampler_color.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_color.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_color.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
  device_->CreateSampler(sampler_color, &sampler_color_);

  Diligent::SamplerDesc sampler_data{};
  sampler_data.MinFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_data.MagFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_data.MipFilter = Diligent::FILTER_TYPE_LINEAR;
  sampler_data.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_data.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
  sampler_data.AddressW = Diligent::TEXTURE_ADDRESS_WRAP;
  device_->CreateSampler(sampler_data, &sampler_data_);

  for (auto& entry : materials_) {
    if (entry.second.srb) {
      if (auto* var = entry.second.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
        var->Set(sampler_color_);
      }
      if (auto* var = entry.second.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
        var->Set(sampler_data_);
      }
    }
  }
  if (default_material_srb_) {
    if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerColor")) {
      var->Set(sampler_color_);
    }
    if (auto* var = default_material_srb_->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_SamplerData")) {
      var->Set(sampler_data_);
    }
  }
}

void DiligentBackend::setGenerateMips(bool enabled) {
  generate_mips_enabled_ = enabled;
}

void DiligentBackend::setShadowSettings(float bias, int map_size, int pcf_radius) {
  shadow_bias_ = std::max(0.0f, bias);
  shadow_pcf_radius_ = std::clamp(pcf_radius, 0, 4);
  const int clamped_size = std::max(256, map_size);
  if (clamped_size != shadow_map_size_) {
    shadow_map_size_ = clamped_size;
    recreateShadowMap();
  }
}

void DiligentBackend::clearFrame(const float* color, bool clear_depth) {
  if (!context_ || !swap_chain_) {
    return;
  }

  auto* rtv = swap_chain_->GetCurrentBackBufferRTV();
  auto* dsv = swap_chain_->GetDepthBufferDSV();
  context_->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  context_->ClearRenderTarget(rtv, color, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  if (clear_depth && dsv) {
    context_->ClearDepthStencil(dsv,
                                Diligent::CLEAR_DEPTH_FLAG,
                                1.0f,
                                0,
                                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  }
}

}  // namespace karma::renderer_backend
