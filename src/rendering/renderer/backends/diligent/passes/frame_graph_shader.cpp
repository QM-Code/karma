#include "../backend.hpp"
#include "../backend_internal.h"

#include <Graphics/GraphicsEngine/interface/Buffer.h>
#include <Graphics/GraphicsEngine/interface/BufferView.h>
#include <Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <Graphics/GraphicsEngine/interface/GraphicsTypes.h>
#include <Graphics/GraphicsEngine/interface/PipelineState.h>
#include <Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <Graphics/GraphicsEngine/interface/Sampler.h>
#include <Graphics/GraphicsEngine/interface/Shader.h>
#include <Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <Graphics/GraphicsEngine/interface/Texture.h>
#include <Graphics/GraphicsTools/interface/MapHelper.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include <spdlog/spdlog.h>

namespace karma::rendering::backend {
namespace {

struct GraphTextureBinding {
  Diligent::ITexture* texture = nullptr;
  Diligent::ITextureView* srv = nullptr;
  Diligent::ITextureView* rtv = nullptr;
  Diligent::ITextureView* dsv = nullptr;
  int width = 0;
  int height = 0;
  Diligent::TEXTURE_FORMAT format = Diligent::TEX_FORMAT_UNKNOWN;
};

struct alignas(16) MaskInstanceGpuData {
  float col0[4];
  float col1[4];
  float col2[4];
  float col3[4];
  float params[4];
};

Diligent::TEXTURE_FORMAT toGraphTextureFormat(rendering::TextureFormat format) {
  switch (format) {
    case rendering::TextureFormat::R8:
      return Diligent::TEX_FORMAT_R8_UNORM;
    case rendering::TextureFormat::BC7_RGBA_UNORM:
      return Diligent::TEX_FORMAT_BC7_UNORM;
    case rendering::TextureFormat::BC7_RGBA_UNORM_SRGB:
      return Diligent::TEX_FORMAT_BC7_UNORM_SRGB;
    case rendering::TextureFormat::RGB8:
    case rendering::TextureFormat::RGBA8:
    case rendering::TextureFormat::KTX2_BASIS_UASTC:
    default:
      return Diligent::TEX_FORMAT_RGBA8_UNORM;
  }
}

Diligent::TEXTURE_FORMAT graphDepthTextureFormat(rendering::TextureFormat format) {
  switch (format) {
    case rendering::TextureFormat::R8:
    case rendering::TextureFormat::RGB8:
    case rendering::TextureFormat::RGBA8:
    case rendering::TextureFormat::BC7_RGBA_UNORM:
    case rendering::TextureFormat::BC7_RGBA_UNORM_SRGB:
    case rendering::TextureFormat::KTX2_BASIS_UASTC:
    default:
      return Diligent::TEX_FORMAT_D32_FLOAT;
  }
}

Diligent::TEXTURE_FORMAT resolveDepthSrvFormat(Diligent::TEXTURE_FORMAT depth_format) {
  switch (depth_format) {
    case Diligent::TEX_FORMAT_D32_FLOAT:
      return Diligent::TEX_FORMAT_R32_FLOAT;
    case Diligent::TEX_FORMAT_D24_UNORM_S8_UINT:
      return Diligent::TEX_FORMAT_R24_UNORM_X8_TYPELESS;
    case Diligent::TEX_FORMAT_D32_FLOAT_S8X24_UINT:
      return Diligent::TEX_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:
      return Diligent::TEX_FORMAT_UNKNOWN;
  }
}

std::string readTextFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

std::string graphRuntimeKey(const rendering::FrameGraphDesc& graph,
                            rendering::RenderTargetId target,
                            std::string_view local_key) {
  std::string key = graph.frame_graph_key.empty() ? std::string("default")
                                                  : graph.frame_graph_key;
  key.push_back('#');
  key.append(std::to_string(target));
  key.push_back('#');
  key.append(local_key);
  return key;
}

std::string sceneMaskPipelineCacheKey(Diligent::TEXTURE_FORMAT color_format,
                                      Diligent::TEXTURE_FORMAT depth_format,
                                      rendering::InstanceGpuLayout layout) {
  std::string key = "scene_mask#";
  key.append(std::to_string(static_cast<int>(color_format)));
  key.push_back('#');
  key.append(std::to_string(static_cast<int>(depth_format)));
  key.push_back('#');
  key.append(std::to_string(static_cast<int>(layout)));
  return key;
}

int resourceWidth(const rendering::FrameGraphResourceDesc& resource, int camera_width) {
  if (resource.size_mode == rendering::FrameGraphResourceSizeMode::Absolute) {
    return static_cast<int>(std::max(resource.width, 1u));
  }
  return std::max(1, static_cast<int>(static_cast<float>(camera_width) *
                                      resource.width_scale));
}

int resourceHeight(const rendering::FrameGraphResourceDesc& resource, int camera_height) {
  if (resource.size_mode == rendering::FrameGraphResourceSizeMode::Absolute) {
    return static_cast<int>(std::max(resource.height, 1u));
  }
  return std::max(1, static_cast<int>(static_cast<float>(camera_height) *
                                      resource.height_scale));
}

std::string shaderVariableNameForSlot(std::string_view slot) {
  if (slot == "source") {
    return "g_Source";
  }
  if (slot == "depth") {
    return "g_Depth";
  }
  if (slot == "history") {
    return "g_History";
  }
  if (slot.rfind("g_", 0) == 0 || slot.rfind("g", 0) == 0) {
    return std::string(slot);
  }
  std::string name = "g_";
  name.append(slot);
  return name;
}

bool tagsMatch(const std::vector<std::string>& wanted,
               const std::vector<std::string>& available) {
  if (wanted.empty() || available.empty()) {
    return false;
  }
  for (const std::string& tag : wanted) {
    if (std::find(available.begin(), available.end(), tag) != available.end()) {
      return true;
    }
  }
  return false;
}

void packMaskInstance(const rendering::InstanceData& instance,
                      MaskInstanceGpuData& packed) {
  const float* ptr = glm::value_ptr(instance.transform);
  std::memcpy(packed.col0, ptr, sizeof(packed.col0));
  std::memcpy(packed.col1, ptr + 4, sizeof(packed.col1));
  std::memcpy(packed.col2, ptr + 8, sizeof(packed.col2));
  std::memcpy(packed.col3, ptr + 12, sizeof(packed.col3));
  const float* params = glm::value_ptr(instance.params);
  std::memcpy(packed.params, params, sizeof(packed.params));
}

const rendering::ShaderPassAssetDesc* findShaderPassAsset(
    const rendering::FrameGraphDesc& graph,
    std::string_view key) {
  for (const rendering::ShaderPassAssetDesc& asset : graph.shader_pass_assets) {
    if (asset.shader_pass_key == key) {
      return &asset;
    }
  }
  return nullptr;
}

std::string pipelineCacheKey(const rendering::ShaderPassAssetDesc& asset,
                             const rendering::FrameGraphPassDesc& pass,
                             Diligent::TEXTURE_FORMAT color_format) {
  std::string key = asset.shader_pass_key;
  key.push_back('#');
  key.append(pass.name);
  key.push_back('#');
  key.append(asset.pipeline.vertex_shader_path.string());
  key.push_back('#');
  key.append(asset.pipeline.fragment_shader_path.string());
  key.push_back('#');
  key.append(std::to_string(static_cast<int>(color_format)));
  key.push_back('#');
  key.append(asset.depth_test ? "dt" : "dT");
  key.append(asset.depth_write ? "dw" : "dW");
  key.append(asset.blend_enabled ? "b" : "B");
  for (const auto& [slot, resource] : pass.inputs) {
    key.push_back('#');
    key.append(slot);
    key.push_back('=');
    key.append(resource);
  }
  return key;
}

void appendShaderMacros(const std::vector<std::string>& defines,
                        std::vector<std::string>& macro_names,
                        std::vector<std::string>& macro_values,
                        std::vector<Diligent::ShaderMacro>& macros) {
  macro_names.reserve(defines.size());
  macro_values.reserve(defines.size());
  macros.reserve(defines.size());
  for (const std::string& define : defines) {
    if (define.empty()) {
      continue;
    }
    const size_t equals = define.find('=');
    if (equals == std::string::npos) {
      macro_names.push_back(define);
      macro_values.push_back("1");
    } else if (equals > 0) {
      macro_names.push_back(define.substr(0, equals));
      macro_values.push_back(define.substr(equals + 1));
    }
  }
  for (size_t i = 0; i < macro_names.size(); ++i) {
    macros.push_back(Diligent::ShaderMacro{macro_names[i].c_str(),
                                           macro_values[i].c_str()});
  }
}

void recordGraphShaderTiming(rendering::RendererFrameTimingStats& stats,
                             const std::string& pass_name,
                             float ms) {
  for (rendering::RendererGraphPassTiming& timing : stats.graph_pass_timings) {
    if (timing.name == pass_name) {
      timing.ms += ms;
      return;
    }
  }
  stats.graph_pass_timings.push_back(
      rendering::RendererGraphPassTiming{pass_name, ms});
}

}  // namespace

bool DiligentBackend::ensureFrameGraphShaderPassPipeline(
    const rendering::ShaderPassAssetDesc& asset,
    const rendering::FrameGraphPassDesc& pass,
    Diligent::TEXTURE_FORMAT color_format,
    FrameGraphShaderPassResources& out_pass) {
  if (!device_ || color_format == Diligent::TEX_FORMAT_UNKNOWN || !asset.fullscreen) {
    return false;
  }

  const std::string cache_key = pipelineCacheKey(asset, pass, color_format);
  if (out_pass.pso && out_pass.srb && out_pass.cache_key == cache_key &&
      out_pass.rtv_format == color_format) {
    return true;
  }

  out_pass = {};
  out_pass.cache_key = cache_key;
  out_pass.rtv_format = color_format;

  const std::string vertex_source = readTextFile(asset.pipeline.vertex_shader_path);
  const std::string fragment_source = readTextFile(asset.pipeline.fragment_shader_path);
  if (vertex_source.empty() || fragment_source.empty()) {
    spdlog::error("Frame graph shader pass '{}' has unreadable shader source",
                  asset.shader_pass_key);
    return false;
  }

  std::vector<std::string> macro_names;
  std::vector<std::string> macro_values;
  std::vector<Diligent::ShaderMacro> macros;
  appendShaderMacros(asset.pipeline.defines, macro_names, macro_values, macros);
  Diligent::ShaderMacroArray macro_array{
      macros.empty() ? nullptr : macros.data(),
      static_cast<Diligent::Uint32>(macros.size()),
  };

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.Macros = macro_array;

  Diligent::RefCntAutoPtr<Diligent::IShader> vs;
  const std::string vs_name = "FrameGraph " + pass.name + " VS";
  shader_ci.Desc.Name = vs_name.c_str();
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
  shader_ci.EntryPoint = asset.pipeline.vertex_entry_point.c_str();
  shader_ci.Source = vertex_source.c_str();
  vs = device_with_cache_.CreateShader(shader_ci);

  Diligent::RefCntAutoPtr<Diligent::IShader> ps;
  const std::string ps_name = "FrameGraph " + pass.name + " PS";
  shader_ci.Desc.Name = ps_name.c_str();
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = asset.pipeline.fragment_entry_point.c_str();
  shader_ci.Source = fragment_source.c_str();
  ps = device_with_cache_.CreateShader(shader_ci);
  if (!vs || !ps) {
    spdlog::error("Frame graph shader pass '{}' failed shader compilation",
                  asset.shader_pass_key);
    return false;
  }

  std::vector<std::string> variable_names;
  std::vector<Diligent::ShaderResourceVariableDesc> variables;
  std::unordered_set<std::string> declared_variables;
  variable_names.reserve(pass.inputs.size() + asset.textures.size() + 1u);
  variables.reserve(pass.inputs.size() + asset.textures.size() + 1u);
  auto add_texture_variable = [&](std::string variable_name) {
    if (variable_name.empty() || !declared_variables.insert(variable_name).second) {
      return;
    }
    variable_names.push_back(std::move(variable_name));
    variables.push_back({Diligent::SHADER_TYPE_PIXEL,
                         variable_names.back().c_str(),
                         Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC});
  };
  for (const auto& [slot, resource_name] : pass.inputs) {
    (void)resource_name;
    add_texture_variable(shaderVariableNameForSlot(slot));
  }
  for (const auto& [alias, texture_key] : asset.textures) {
    (void)texture_key;
    add_texture_variable(alias);
  }
  variable_names.push_back("g_Sampler");
  variables.push_back({Diligent::SHADER_TYPE_PIXEL,
                       variable_names.back().c_str(),
                       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});

  Diligent::GraphicsPipelineStateCreateInfo pso{};
  const std::string pipeline_name = "FrameGraph " + pass.name;
  pso.PSODesc.Name = pipeline_name.c_str();
  pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
  pso.PSODesc.ResourceLayout.Variables = variables.data();
  pso.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(variables.size());
  pso.pVS = vs;
  pso.pPS = ps;

  auto& graphics = pso.GraphicsPipeline;
  graphics.NumRenderTargets = 1;
  graphics.RTVFormats[0] = color_format;
  graphics.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
  graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
  graphics.DepthStencilDesc.DepthEnable = asset.depth_test;
  graphics.DepthStencilDesc.DepthWriteEnable = asset.depth_write;
  graphics.BlendDesc.RenderTargets[0].RenderTargetWriteMask =
      Diligent::COLOR_MASK_ALL;
  if (asset.blend_enabled) {
    const bool additive =
        asset.blend_mode == rendering::MaterialDesc::BlendMode::Additive;
    auto& blend = graphics.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = additive ? Diligent::BLEND_FACTOR_ONE
                               : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
    blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    blend.DestBlendAlpha = additive ? Diligent::BLEND_FACTOR_ONE
                                    : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
  }

  const auto pso_start = core::SteadyClock::now();
  out_pass.pso = createGraphicsPipelineState(pso);
  recordPipelineCreation("frame_graph", pass.name.c_str(), pso_start,
                         core::SteadyClock::now());
  if (!out_pass.pso) {
    spdlog::error("Frame graph shader pass '{}' failed pipeline creation",
                  asset.shader_pass_key);
    return false;
  }

  const auto srb_start = core::SteadyClock::now();
  out_pass.pso->CreateShaderResourceBinding(&out_pass.srb, true);
  recordResourceCreation("frame_graph", pass.name.c_str(), srb_start,
                         core::SteadyClock::now());
  if (!out_pass.srb) {
    out_pass = {};
    return false;
  }

  for (const auto& [slot, resource_name] : pass.inputs) {
    (void)resource_name;
    const std::string variable_name = shaderVariableNameForSlot(slot);
    if (Diligent::IShaderResourceVariable* variable =
            out_pass.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                            variable_name.c_str())) {
      out_pass.texture_vars[slot] = variable;
    } else {
      spdlog::error("Frame graph shader pass '{}' missing input variable '{}'",
                    asset.shader_pass_key,
                    variable_name);
      out_pass = {};
      return false;
    }
  }
  for (const auto& [alias, texture_key] : asset.textures) {
    (void)texture_key;
    if (Diligent::IShaderResourceVariable* variable =
            out_pass.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL,
                                            alias.c_str())) {
      out_pass.texture_vars["texture:" + alias] = variable;
    } else {
      spdlog::error("Frame graph shader pass '{}' missing texture variable '{}'",
                    asset.shader_pass_key,
                    alias);
      out_pass = {};
      return false;
    }
  }
  out_pass.sampler_var =
      out_pass.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Sampler");
  if (out_pass.sampler_var && sampler_color_) {
    out_pass.sampler_var->Set(sampler_color_);
  }
  return true;
}

bool DiligentBackend::ensureFrameGraphSceneMaskPipeline(
    Diligent::TEXTURE_FORMAT color_format,
    Diligent::TEXTURE_FORMAT depth_format,
    rendering::InstanceGpuLayout layout,
    FrameGraphSceneMaskResources& out_pass) {
  if (!device_ || !forward_vs_ || color_format == Diligent::TEX_FORMAT_UNKNOWN) {
    return false;
  }

  const std::string cache_key = sceneMaskPipelineCacheKey(color_format, depth_format, layout);
  if (out_pass.pso && out_pass.srb && out_pass.cache_key == cache_key &&
      out_pass.rtv_format == color_format &&
      out_pass.dsv_format == depth_format &&
      out_pass.layout == layout) {
    return true;
  }

  static constexpr const char* kMaskPixelShader = R"(
float4 main() : SV_TARGET
{
    return float4(1.0, 1.0, 1.0, 1.0);
}
)";

  Diligent::ShaderCreateInfo shader_ci{};
  shader_ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
  shader_ci.Desc.Name = "FrameGraph Scene Mask PS";
  shader_ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
  shader_ci.EntryPoint = "main";
  shader_ci.Source = kMaskPixelShader;
  Diligent::RefCntAutoPtr<Diligent::IShader> ps =
      device_with_cache_.CreateShader(shader_ci);
  if (!ps) {
    spdlog::error("Frame graph scene mask failed shader compilation");
    out_pass = {};
    return false;
  }

  const Diligent::Uint32 instance_stride =
      static_cast<Diligent::Uint32>(rendering::instanceGpuLayoutStride(layout));
  const Diligent::Uint32 model_col1_offset =
      static_cast<Diligent::Uint32>(sizeof(float) * 4);
  const Diligent::Uint32 model_col2_offset =
      layout == rendering::InstanceGpuLayout::PositionYawScaleParams
          ? 0u
          : static_cast<Diligent::Uint32>(sizeof(float) * 8);
  const Diligent::Uint32 model_col3_offset =
      layout == rendering::InstanceGpuLayout::PositionYawScaleParams
          ? static_cast<Diligent::Uint32>(sizeof(float) * 4)
          : static_cast<Diligent::Uint32>(sizeof(float) * 12);
  const Diligent::Uint32 params_offset =
      layout == rendering::InstanceGpuLayout::PositionYawScaleParams
          ? static_cast<Diligent::Uint32>(sizeof(float) * 8)
          : static_cast<Diligent::Uint32>(sizeof(float) * 16);

  std::array<Diligent::LayoutElement, 12> layout_elems{{
      Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{3, 0, 2, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{10, 0, 2, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{8, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{9, 0, 4, Diligent::VT_FLOAT32, false},
      Diligent::LayoutElement{4,
                              1,
                              4,
                              Diligent::VT_FLOAT32,
                              false,
                              0u,
                              instance_stride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{5,
                              1,
                              4,
                              Diligent::VT_FLOAT32,
                              false,
                              model_col1_offset,
                              instance_stride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{6,
                              1,
                              4,
                              Diligent::VT_FLOAT32,
                              false,
                              model_col2_offset,
                              instance_stride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{7,
                              1,
                              4,
                              Diligent::VT_FLOAT32,
                              false,
                              model_col3_offset,
                              instance_stride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
      Diligent::LayoutElement{11,
                              1,
                              4,
                              Diligent::VT_FLOAT32,
                              false,
                              params_offset,
                              instance_stride,
                              Diligent::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
  }};

  Diligent::ShaderResourceVariableDesc variables[] = {
      {Diligent::SHADER_TYPE_VERTEX,
       "Constants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_VERTEX,
       "DeformationConstants",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
      {Diligent::SHADER_TYPE_VERTEX,
       "g_DeformationMatrices",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_VERTEX,
       "g_MorphWeights",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
      {Diligent::SHADER_TYPE_VERTEX,
       "g_MorphTargetDeltas",
       Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
  };

  Diligent::GraphicsPipelineStateCreateInfo pso{};
  const std::string pipeline_name =
      "FrameGraph Scene Mask " + std::to_string(static_cast<int>(layout));
  pso.PSODesc.Name = pipeline_name.c_str();
  pso.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
  pso.PSODesc.ResourceLayout.Variables = variables;
  pso.PSODesc.ResourceLayout.NumVariables =
      static_cast<Diligent::Uint32>(sizeof(variables) / sizeof(variables[0]));
  pso.pVS = forward_vs_;
  pso.pPS = ps;

  auto& graphics = pso.GraphicsPipeline;
  graphics.NumRenderTargets = 1;
  graphics.RTVFormats[0] = color_format;
  graphics.DSVFormat = depth_format;
  graphics.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  graphics.InputLayout.LayoutElements = layout_elems.data();
  graphics.InputLayout.NumElements = static_cast<Diligent::Uint32>(layout_elems.size());
  graphics.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
  graphics.RasterizerDesc.FrontCounterClockwise = true;
  graphics.DepthStencilDesc.DepthEnable = depth_format != Diligent::TEX_FORMAT_UNKNOWN;
  graphics.DepthStencilDesc.DepthWriteEnable = depth_format != Diligent::TEX_FORMAT_UNKNOWN;
  graphics.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
  graphics.BlendDesc.RenderTargets[0].RenderTargetWriteMask = Diligent::COLOR_MASK_ALL;

  out_pass = {};
  out_pass.cache_key = cache_key;
  out_pass.rtv_format = color_format;
  out_pass.dsv_format = depth_format;
  out_pass.layout = layout;

  const auto pso_start = core::SteadyClock::now();
  out_pass.pso = createGraphicsPipelineState(pso);
  recordPipelineCreation("frame_graph", "scene_mask", pso_start, core::SteadyClock::now());
  if (!out_pass.pso) {
    spdlog::error("Frame graph scene mask failed pipeline creation");
    out_pass = {};
    return false;
  }
  bindForwardPipelineStaticResources(out_pass.pso.RawPtr());

  const auto srb_start = core::SteadyClock::now();
  out_pass.pso->CreateShaderResourceBinding(&out_pass.srb, true);
  recordResourceCreation("frame_graph", "scene_mask", srb_start, core::SteadyClock::now());
  if (!out_pass.srb) {
    out_pass = {};
    return false;
  }
  return true;
}

bool DiligentBackend::executeFrameGraphScreenPasses(
    const rendering::FrameGraphDesc& graph,
    rendering::LayerId layer,
    Diligent::ITexture* camera_color_texture,
    Diligent::ITextureView* camera_color_srv,
    Diligent::ITextureView* camera_depth_srv,
    Diligent::ITextureView* camera_color_rtv,
    const DrawConstants& base_constants,
    int width,
    int height,
    Diligent::TEXTURE_FORMAT color_format,
    rendering::RenderTargetId target) {
  if (!context_ || !device_ || !camera_color_texture || !camera_color_srv ||
      !camera_color_rtv || width <= 0 || height <= 0 ||
      color_format == Diligent::TEX_FORMAT_UNKNOWN) {
    return false;
  }

  bool has_screen_work = false;
  for (const rendering::FrameGraphPassDesc& pass : graph.passes) {
    if (pass.enabled &&
        (pass.kind == rendering::FrameGraphPassKind::Shader ||
         pass.kind == rendering::FrameGraphPassKind::Copy ||
         pass.kind == rendering::FrameGraphPassKind::SceneMask)) {
      has_screen_work = true;
      break;
    }
  }
  if (!has_screen_work) {
    return true;
  }

  std::unordered_map<std::string, GraphTextureBinding> bindings;
  bindings.emplace(std::string(rendering::kFrameGraphCameraColor),
                   GraphTextureBinding{camera_color_texture,
                                       camera_color_srv,
                                       camera_color_rtv,
                                       nullptr,
                                       width,
                                       height,
                                       color_format});
  bindings.emplace(std::string(rendering::kFrameGraphCameraDepth),
                   GraphTextureBinding{nullptr,
                                       camera_depth_srv,
                                       nullptr,
                                       nullptr,
                                       width,
                                       height,
                                       Diligent::TEX_FORMAT_UNKNOWN});

  auto ensure_color_texture = [&](const rendering::FrameGraphResourceDesc& resource)
      -> GraphTextureBinding {
    if (resource.kind != rendering::FrameGraphResourceKind::ColorTexture) {
      return {};
    }
    const int resource_width = resourceWidth(resource, width);
    const int resource_height = resourceHeight(resource, height);
    const Diligent::TEXTURE_FORMAT resource_format =
        toGraphTextureFormat(resource.format);
    std::string key = graphRuntimeKey(graph, target, resource.name);
    key.push_back('#');
    key.append(std::to_string(resource_width));
    key.push_back('x');
    key.append(std::to_string(resource_height));
    key.push_back('#');
    key.append(std::to_string(static_cast<int>(resource_format)));

    PostProcessTexture& texture = frame_graph_color_textures_[key];
    if (!texture.texture || !texture.srv || !texture.rtv ||
        texture.width != resource_width || texture.height != resource_height) {
      texture = {};
      Diligent::TextureDesc desc{};
      const std::string texture_name = "FrameGraph " + resource.name;
      desc.Name = texture_name.c_str();
      desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
      desc.Width = static_cast<Diligent::Uint32>(resource_width);
      desc.Height = static_cast<Diligent::Uint32>(resource_height);
      desc.MipLevels = 1;
      desc.Format = resource_format;
      desc.BindFlags = Diligent::BIND_RENDER_TARGET |
                       Diligent::BIND_SHADER_RESOURCE;
      const auto create_start = core::SteadyClock::now();
      device_->CreateTexture(desc, nullptr, &texture.texture);
      recordResourceCreation("frame_graph", resource.name.c_str(), create_start,
                             core::SteadyClock::now());
      if (!texture.texture) {
        return {};
      }
      texture.srv =
          texture.texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
      texture.rtv =
          texture.texture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
      texture.width = resource_width;
      texture.height = resource_height;
    }
    return GraphTextureBinding{texture.texture,
                               texture.srv,
                               texture.rtv,
                               nullptr,
                               resource_width,
                               resource_height,
                               resource_format};
  };

  auto ensure_depth_texture = [&](const rendering::FrameGraphResourceDesc& resource)
      -> GraphTextureBinding {
    if (resource.kind != rendering::FrameGraphResourceKind::DepthTexture) {
      return {};
    }
    const int resource_width = resourceWidth(resource, width);
    const int resource_height = resourceHeight(resource, height);
    const Diligent::TEXTURE_FORMAT resource_format =
        graphDepthTextureFormat(resource.format);
    std::string key = graphRuntimeKey(graph, target, resource.name);
    key.push_back('#');
    key.append(std::to_string(resource_width));
    key.push_back('x');
    key.append(std::to_string(resource_height));
    key.push_back('#');
    key.append(std::to_string(static_cast<int>(resource_format)));

    PostProcessTexture& texture = frame_graph_depth_textures_[key];
    if (!texture.texture || !texture.srv || !texture.dsv ||
        texture.width != resource_width || texture.height != resource_height) {
      texture = {};
      Diligent::TextureDesc desc{};
      const std::string texture_name = "FrameGraph " + resource.name;
      desc.Name = texture_name.c_str();
      desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
      desc.Width = static_cast<Diligent::Uint32>(resource_width);
      desc.Height = static_cast<Diligent::Uint32>(resource_height);
      desc.MipLevels = 1;
      desc.Format = resource_format;
      desc.BindFlags = Diligent::BIND_DEPTH_STENCIL |
                       Diligent::BIND_SHADER_RESOURCE;
      const auto create_start = core::SteadyClock::now();
      device_->CreateTexture(desc, nullptr, &texture.texture);
      recordResourceCreation("frame_graph", resource.name.c_str(), create_start,
                             core::SteadyClock::now());
      if (!texture.texture) {
        return {};
      }
      Diligent::TextureViewDesc srv_desc{};
      srv_desc.ViewType = Diligent::TEXTURE_VIEW_SHADER_RESOURCE;
      srv_desc.TextureDim = Diligent::RESOURCE_DIM_TEX_2D;
      srv_desc.Format = resolveDepthSrvFormat(resource_format);
      if (srv_desc.Format != Diligent::TEX_FORMAT_UNKNOWN) {
        texture.texture->CreateView(srv_desc, &texture.srv);
      }
      if (!texture.srv) {
        texture.srv =
            texture.texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
      }
      texture.dsv =
          texture.texture->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
      texture.width = resource_width;
      texture.height = resource_height;
    }
    return GraphTextureBinding{texture.texture,
                               texture.srv,
                               nullptr,
                               texture.dsv,
                               resource_width,
                               resource_height,
                               resource_format};
  };

  for (const rendering::FrameGraphResourceDesc& resource : graph.resources) {
    if (resource.kind == rendering::FrameGraphResourceKind::ColorTexture) {
      bindings[resource.name] = ensure_color_texture(resource);
    } else if (resource.kind == rendering::FrameGraphResourceKind::DepthTexture) {
      bindings[resource.name] = ensure_depth_texture(resource);
    }
  }

  auto binding_for = [&](const std::string& resource_name) -> GraphTextureBinding {
    const auto it = bindings.find(resource_name);
    return it == bindings.end() ? GraphTextureBinding{} : it->second;
  };

  auto ensure_source_copy = [&]() -> GraphTextureBinding {
    if (!frame_graph_source_copy_.texture || !frame_graph_source_copy_.srv ||
        !frame_graph_source_copy_.rtv || frame_graph_source_copy_.width != width ||
        frame_graph_source_copy_.height != height ||
        frame_graph_source_copy_format_ != color_format) {
      frame_graph_source_copy_ = {};
      frame_graph_source_copy_format_ = Diligent::TEX_FORMAT_UNKNOWN;
      Diligent::TextureDesc desc{};
      desc.Name = "FrameGraph Source Copy";
      desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
      desc.Width = static_cast<Diligent::Uint32>(width);
      desc.Height = static_cast<Diligent::Uint32>(height);
      desc.MipLevels = 1;
      desc.Format = color_format;
      desc.BindFlags = Diligent::BIND_RENDER_TARGET |
                       Diligent::BIND_SHADER_RESOURCE;
      const auto create_start = core::SteadyClock::now();
      device_->CreateTexture(desc, nullptr, &frame_graph_source_copy_.texture);
      recordResourceCreation("frame_graph", "source copy", create_start,
                             core::SteadyClock::now());
      if (!frame_graph_source_copy_.texture) {
        return {};
      }
      frame_graph_source_copy_.srv = frame_graph_source_copy_.texture->GetDefaultView(
          Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
      frame_graph_source_copy_.rtv = frame_graph_source_copy_.texture->GetDefaultView(
          Diligent::TEXTURE_VIEW_RENDER_TARGET);
      frame_graph_source_copy_.width = width;
      frame_graph_source_copy_.height = height;
      frame_graph_source_copy_format_ = color_format;
    }
    Diligent::CopyTextureAttribs copy_attribs{
        camera_color_texture,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
        frame_graph_source_copy_.texture,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
    context_->CopyTexture(copy_attribs);
    return GraphTextureBinding{frame_graph_source_copy_.texture,
                               frame_graph_source_copy_.srv,
                               frame_graph_source_copy_.rtv,
                               nullptr,
                               width,
                               height,
                               color_format};
  };

  auto ensure_mask_instance_buffer = [&](size_t instance_count) {
    if (instance_count == 0) {
      return false;
    }
    if (instance_vb_ && instance_vb_capacity_ >= instance_count) {
      return true;
    }
    const size_t new_capacity =
        std::max(instance_count,
                 instance_vb_capacity_ > 0 ? instance_vb_capacity_ * 2 : static_cast<size_t>(128));
    Diligent::BufferDesc desc{};
    desc.Name = "Karma FrameGraph Mask Instance Buffer";
    desc.Usage = Diligent::USAGE_DYNAMIC;
    desc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    desc.Size = static_cast<Diligent::Uint32>(new_capacity * sizeof(MaskInstanceGpuData));
    instance_vb_.Release();
    const auto create_start = core::SteadyClock::now();
    device_->CreateBuffer(desc, nullptr, &instance_vb_);
    recordResourceCreation("frame_graph", "scene_mask instance buffer", create_start,
                           core::SteadyClock::now());
    if (!instance_vb_) {
      instance_vb_capacity_ = 0;
      return false;
    }
    instance_vb_capacity_ = new_capacity;
    return true;
  };

  auto upload_mask_instances = [&](const MaskInstanceGpuData* instances,
                                   size_t instance_count) {
    if (!instances || instance_count == 0 || !ensure_mask_instance_buffer(instance_count)) {
      return false;
    }
    Diligent::MapHelper<MaskInstanceGpuData> mapped(
        context_, instance_vb_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
    auto* data = static_cast<MaskInstanceGpuData*>(mapped);
    if (data == nullptr) {
      return false;
    }
    std::memcpy(data, instances, instance_count * sizeof(MaskInstanceGpuData));
    instancing_stats_.instance_buffer_updates += 1u;
    instancing_stats_.instance_upload_bytes +=
        static_cast<uint64_t>(instance_count * sizeof(MaskInstanceGpuData));
    return true;
  };

  auto update_mask_constants =
      [&](rendering::InstanceGpuLayout layout,
          rendering::InstanceLodRenderMode render_mode) {
    if (!constants_) {
      return false;
    }
    DrawConstants constants = base_constants;
    constants.instance_params[0] =
        layout == rendering::InstanceGpuLayout::PositionYawScaleParams ? 1.0f : 0.0f;
    constants.instance_params[1] =
        render_mode == rendering::InstanceLodRenderMode::UprightBillboard ? 1.0f : 0.0f;
    constants.instance_params[2] = 0.0f;
    constants.instance_params[3] = 0.0f;
    Diligent::MapHelper<DrawConstants> mapped(
        context_, constants_, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
    auto* data = static_cast<DrawConstants*>(mapped);
    if (data == nullptr) {
      return false;
    }
    *data = constants;
    return true;
  };

  auto bind_mask_geometry =
      [&](const MeshRecord& mesh,
          Diligent::IBuffer* instance_buffer,
          Diligent::IBuffer*& bound_mesh_vb,
          Diligent::IBuffer*& bound_instance_vb) {
    if (!mesh.vertex_buffer || !instance_buffer) {
      return false;
    }
    Diligent::IBuffer* mesh_vb = mesh.vertex_buffer.RawPtr();
    if (mesh_vb != bound_mesh_vb || instance_buffer != bound_instance_vb) {
      Diligent::IBuffer* vbs[] = {mesh.vertex_buffer.RawPtr(), instance_buffer};
      Diligent::Uint64 offsets[] = {0, 0};
      context_->SetVertexBuffers(0,
                                 2,
                                 vbs,
                                 offsets,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                 Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
      bound_mesh_vb = mesh_vb;
      bound_instance_vb = instance_buffer;
    }
    return true;
  };

  auto draw_mask_range = [&](const MeshRecord& mesh,
                             Diligent::Uint32 index_offset,
                             Diligent::Uint32 index_count,
                             bool indexed,
                             Diligent::Uint32 instance_count,
                             Diligent::IBuffer*& bound_index_buffer) {
    if (indexed) {
      if (!mesh.index_buffer || mesh.index_count == 0 || index_count == 0 ||
          static_cast<uint64_t>(index_offset) + static_cast<uint64_t>(index_count) >
              static_cast<uint64_t>(mesh.index_count)) {
        return false;
      }
      Diligent::IBuffer* index_buffer = mesh.index_buffer.RawPtr();
      if (index_buffer != bound_index_buffer) {
        context_->SetIndexBuffer(mesh.index_buffer,
                                 0,
                                 Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        bound_index_buffer = index_buffer;
      }
      Diligent::DrawIndexedAttribs indexed_draw{};
      indexed_draw.IndexType = Diligent::VT_UINT32;
      indexed_draw.NumIndices = index_count;
      indexed_draw.FirstIndexLocation = index_offset;
      indexed_draw.NumInstances = instance_count;
      indexed_draw.Flags = Diligent::DRAW_FLAG_NONE;
      context_->DrawIndexed(indexed_draw);
      return true;
    }
    if (mesh.vertex_count == 0) {
      return false;
    }
    Diligent::DrawAttribs draw{};
    draw.NumVertices = mesh.vertex_count;
    draw.NumInstances = instance_count;
    draw.Flags = Diligent::DRAW_FLAG_NONE;
    context_->Draw(draw);
    return true;
  };

  auto draw_mask_mesh = [&](const MeshRecord& mesh,
                            Diligent::Uint32 instance_count,
                            Diligent::IBuffer*& bound_index_buffer) {
    Diligent::Uint32 draws = 0;
    const bool indexed = mesh.index_buffer && mesh.index_count > 0;
    if (!mesh.submeshes.empty()) {
      for (const auto& submesh : mesh.submeshes) {
        if (draw_mask_range(mesh,
                            submesh.index_offset,
                            submesh.index_count,
                            indexed && submesh.index_count > 0,
                            instance_count,
                            bound_index_buffer)) {
          ++draws;
        }
      }
    } else if (draw_mask_range(mesh,
                               0,
                               mesh.index_count,
                               indexed,
                               instance_count,
                               bound_index_buffer)) {
      ++draws;
    }
    return draws;
  };

  for (const rendering::FrameGraphPassDesc& pass : graph.passes) {
    if (!pass.enabled) {
      continue;
    }
    if (pass.kind != rendering::FrameGraphPassKind::Shader &&
        pass.kind != rendering::FrameGraphPassKind::Copy &&
        pass.kind != rendering::FrameGraphPassKind::SceneMask) {
      continue;
    }

    const auto pass_start = core::SteadyClock::now();
    bool pass_ok = true;
    const auto source_it = pass.inputs.find("source");
    const auto target_it = pass.outputs.find("target");
    const std::string source_name =
        source_it == pass.inputs.end() ? std::string{} : source_it->second;
    const std::string target_name =
        target_it == pass.outputs.end() ? std::string{} : target_it->second;
    GraphTextureBinding target_binding = binding_for(target_name);
    if (!target_binding.rtv &&
        (pass.kind == rendering::FrameGraphPassKind::Shader ||
         pass.kind == rendering::FrameGraphPassKind::SceneMask)) {
      pass_ok = false;
    }

    if (pass.kind == rendering::FrameGraphPassKind::SceneMask) {
      const auto depth_output_it = pass.outputs.find("depth");
      const std::string depth_name =
          depth_output_it == pass.outputs.end() ? std::string{} : depth_output_it->second;
      GraphTextureBinding depth_binding = binding_for(depth_name);
      const Diligent::TEXTURE_FORMAT mask_color_format =
          target_binding.format != Diligent::TEX_FORMAT_UNKNOWN
              ? target_binding.format
              : color_format;
      const Diligent::TEXTURE_FORMAT mask_depth_format =
          depth_binding.dsv && depth_binding.format != Diligent::TEX_FORMAT_UNKNOWN
              ? depth_binding.format
              : Diligent::TEX_FORMAT_UNKNOWN;
      if (!target_binding.rtv || mask_color_format == Diligent::TEX_FORMAT_UNKNOWN) {
        pass_ok = false;
      } else {
        Diligent::ITextureView* rtvs[] = {target_binding.rtv};
        context_->SetRenderTargets(1,
                                   rtvs,
                                   depth_binding.dsv,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (pass.clear) {
          const float clear_color[] = {pass.clear_color.r,
                                       pass.clear_color.g,
                                       pass.clear_color.b,
                                       pass.clear_color.a};
          context_->ClearRenderTarget(target_binding.rtv,
                                      clear_color,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        if (pass.clear_depth && depth_binding.dsv) {
          context_->ClearDepthStencil(depth_binding.dsv,
                                      Diligent::CLEAR_DEPTH_FLAG,
                                      1.0f,
                                      0,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }

        const int target_width = target_binding.width > 0 ? target_binding.width : width;
        const int target_height = target_binding.height > 0 ? target_binding.height : height;
        Diligent::Viewport viewport{};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(target_width);
        viewport.Height = static_cast<float>(target_height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context_->SetViewports(1,
                               &viewport,
                               static_cast<Diligent::Uint32>(target_width),
                               static_cast<Diligent::Uint32>(target_height));

        Diligent::IBuffer* bound_mesh_vb = nullptr;
        Diligent::IBuffer* bound_instance_vb = nullptr;
        Diligent::IBuffer* bound_index_buffer = nullptr;

        auto bind_mask_pipeline =
            [&](rendering::InstanceGpuLayout layout,
                rendering::InstanceLodRenderMode render_mode,
                const MeshRecord& mesh,
                rendering::DeformationId deformation) -> FrameGraphSceneMaskResources* {
          const std::string runtime_key =
              sceneMaskPipelineCacheKey(mask_color_format, mask_depth_format, layout);
          FrameGraphSceneMaskResources& runtime =
              frame_graph_scene_mask_passes_[runtime_key];
          if (!ensureFrameGraphSceneMaskPipeline(mask_color_format,
                                                 mask_depth_format,
                                                 layout,
                                                 runtime) ||
              !update_mask_constants(layout, render_mode) ||
              !bindDeformationResources(runtime.srb, mesh, deformation)) {
            return nullptr;
          }
          context_->SetPipelineState(runtime.pso);
          context_->CommitShaderResources(runtime.srb,
                                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
          return &runtime;
        };

        Diligent::Uint32 mask_draws = 0;
        for (const auto& [id, instance] : instances_) {
          (void)id;
          if (instance.layer != layer || !instance.visible ||
              !tagsMatch(pass.render_tags, instance.render_tags)) {
            continue;
          }
          const auto mesh_it = meshes_.find(instance.mesh);
          if (mesh_it == meshes_.end() || !mesh_it->second.vertex_buffer) {
            continue;
          }
          const MeshRecord& mesh = mesh_it->second;
          if (bind_mask_pipeline(rendering::InstanceGpuLayout::Matrix4x4Params,
                                 rendering::InstanceLodRenderMode::Mesh,
                                 mesh,
                                 instance.deformation) == nullptr) {
            pass_ok = false;
            break;
          }
          const rendering::InstanceData submitted_instance{
              .transform = instance.transform,
              .params = instance.params,
          };
          MaskInstanceGpuData packed_instance{};
          packMaskInstance(submitted_instance, packed_instance);
          if (!upload_mask_instances(&packed_instance, 1) ||
              !bind_mask_geometry(mesh,
                                  instance_vb_.RawPtr(),
                                  bound_mesh_vb,
                                  bound_instance_vb)) {
            pass_ok = false;
            break;
          }
          mask_draws += draw_mask_mesh(mesh, 1, bound_index_buffer);
        }

        if (pass_ok) {
          for (auto& [id, record] : instanced_records_) {
            (void)id;
            if (record.layer != layer || !record.visible || record.instanceCount() == 0u ||
                !tagsMatch(pass.render_tags, record.render_tags)) {
              continue;
            }
            const auto mesh_it = meshes_.find(record.mesh);
            if (mesh_it == meshes_.end() || !mesh_it->second.vertex_buffer) {
              continue;
            }
            MeshRecord& mesh = mesh_it->second;
            const Diligent::Uint32 instance_count =
                static_cast<Diligent::Uint32>(std::min<size_t>(
                    record.instanceCount(),
                    static_cast<size_t>(std::numeric_limits<Diligent::Uint32>::max())));
            if (instance_count == 0u ||
                !ensureInstancedRecordBuffer(record) ||
                bind_mask_pipeline(record.gpu_layout,
                                   rendering::InstanceLodRenderMode::Mesh,
                                   mesh,
                                   rendering::kInvalidDeformation) == nullptr ||
                !bind_mask_geometry(mesh,
                                    record.instance_buffer.RawPtr(),
                                    bound_mesh_vb,
                                    bound_instance_vb)) {
              pass_ok = false;
              break;
            }
            mask_draws += draw_mask_mesh(mesh, instance_count, bound_index_buffer);
          }
        }
        instancing_stats_.draw_calls += mask_draws;
      }
    } else if (pass.kind == rendering::FrameGraphPassKind::Copy) {
      GraphTextureBinding source_binding = binding_for(source_name);
      if (!source_binding.texture || !target_binding.texture ||
          source_binding.texture == target_binding.texture) {
        pass_ok = source_binding.texture == target_binding.texture;
      } else {
        Diligent::CopyTextureAttribs copy_attribs{
            source_binding.texture,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
            target_binding.texture,
            Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
        context_->CopyTexture(copy_attribs);
      }
    } else if (pass.kind == rendering::FrameGraphPassKind::Shader) {
      const rendering::ShaderPassAssetDesc* asset =
          findShaderPassAsset(graph, pass.shader_pass_key);
      if (asset == nullptr) {
        spdlog::error("Frame graph pass '{}' references unresolved shader pass '{}'",
                      pass.name,
                      pass.shader_pass_key);
        pass_ok = false;
      } else {
        FrameGraphShaderPassResources& runtime =
            frame_graph_shader_passes_[pipelineCacheKey(*asset, pass, color_format)];
        pass_ok = ensureFrameGraphShaderPassPipeline(*asset, pass, color_format, runtime);
        if (pass_ok) {
          for (const auto& [slot, resource_name] : pass.inputs) {
            GraphTextureBinding input_binding = binding_for(resource_name);
            if (resource_name == target_name && resource_name ==
                                                rendering::kFrameGraphCameraColor) {
              input_binding = ensure_source_copy();
            }
            auto var_it = runtime.texture_vars.find(slot);
            if (var_it != runtime.texture_vars.end() && var_it->second &&
                input_binding.srv) {
              var_it->second->Set(input_binding.srv);
            } else {
              pass_ok = false;
            }
          }
          for (const auto& [alias, texture_key] : asset->textures) {
            (void)texture_key;
            const auto handle_it = asset->texture_handles.find(alias);
            const auto renderer_texture_it =
                handle_it == asset->texture_handles.end()
                    ? textures_.end()
                    : textures_.find(handle_it->second);
            const auto var_it = runtime.texture_vars.find("texture:" + alias);
            if (renderer_texture_it == textures_.end() ||
                !renderer_texture_it->second.srv ||
                var_it == runtime.texture_vars.end() ||
                var_it->second == nullptr) {
              pass_ok = false;
              continue;
            }
            var_it->second->Set(renderer_texture_it->second.srv);
          }
          if (runtime.sampler_var && sampler_color_) {
            runtime.sampler_var->Set(sampler_color_);
          }

          if (!pass_ok) {
            spdlog::error("Frame graph shader pass '{}' failed resource binding",
                          pass.name);
          } else {
            context_->SetPipelineState(runtime.pso);
            context_->CommitShaderResources(
                runtime.srb,
                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            Diligent::ITextureView* rtvs[] = {target_binding.rtv};
            context_->SetRenderTargets(
                1,
                rtvs,
                nullptr,
                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            Diligent::Viewport viewport{};
            const int target_width =
                target_binding.width > 0 ? target_binding.width : width;
            const int target_height =
                target_binding.height > 0 ? target_binding.height : height;
            viewport.TopLeftX = 0.0f;
            viewport.TopLeftY = 0.0f;
            viewport.Width = static_cast<float>(target_width);
            viewport.Height = static_cast<float>(target_height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            context_->SetViewports(1,
                                   &viewport,
                                   static_cast<Diligent::Uint32>(target_width),
                                   static_cast<Diligent::Uint32>(target_height));

            Diligent::DrawAttribs draw{};
            draw.NumVertices = 3;
            draw.Flags = Diligent::DRAW_FLAG_NONE;
            context_->Draw(draw);
          }
        }
      }
    }

    recordGraphShaderTiming(
        current_frame_timing_stats_,
        pass.name.empty() ? pass.shader_pass_key : pass.name,
        static_cast<float>(core::elapsedMilliseconds(pass_start,
                                                     core::SteadyClock::now())));
    if (!pass_ok) {
      return false;
    }
  }

  return true;
}

}  // namespace karma::rendering::backend
