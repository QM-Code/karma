#include "karma/rendering.h"

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>

namespace karma::rendering {

namespace {

bool colorsEqual(const Color& lhs, const Color& rhs) {
  return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

bool parameterValuesEqual(const MaterialParameterValue& lhs,
                          const MaterialParameterValue& rhs) {
  if (lhs.index() != rhs.index()) {
    return false;
  }
  return std::visit(
      [&rhs](const auto& lhs_value) {
        using Value = std::decay_t<decltype(lhs_value)>;
        const Value* rhs_value = std::get_if<Value>(&rhs);
        if (rhs_value == nullptr) {
          return false;
        }
        if constexpr (std::is_same_v<Value, Color>) {
          return colorsEqual(lhs_value, *rhs_value);
        } else {
          return lhs_value == *rhs_value;
        }
      },
      lhs);
}

bool parameterMapsEqual(
    const std::unordered_map<std::string, MaterialParameterValue>& lhs,
    const std::unordered_map<std::string, MaterialParameterValue>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (const auto& [name, value] : lhs) {
    const auto it = rhs.find(name);
    if (it == rhs.end() || !parameterValuesEqual(value, it->second)) {
      return false;
    }
  }
  return true;
}

bool pipelineDescsEqual(const MaterialPipelineDesc& lhs,
                        const MaterialPipelineDesc& rhs) {
  return lhs.name == rhs.name &&
         lhs.vertex_shader_path == rhs.vertex_shader_path &&
         lhs.fragment_shader_path == rhs.fragment_shader_path &&
         lhs.vertex_entry_point == rhs.vertex_entry_point &&
         lhs.fragment_entry_point == rhs.fragment_entry_point &&
         lhs.defines == rhs.defines;
}

bool resourcesEqual(const FrameGraphResourceDesc& lhs,
                    const FrameGraphResourceDesc& rhs) {
  return lhs.name == rhs.name && lhs.kind == rhs.kind &&
         lhs.size_mode == rhs.size_mode && lhs.format == rhs.format &&
         lhs.width_scale == rhs.width_scale && lhs.height_scale == rhs.height_scale &&
         lhs.width == rhs.width && lhs.height == rhs.height &&
         lhs.history_count == rhs.history_count;
}

bool passesEqual(const FrameGraphPassDesc& lhs, const FrameGraphPassDesc& rhs) {
  return lhs.name == rhs.name && lhs.kind == rhs.kind &&
         lhs.builtin_pass == rhs.builtin_pass &&
         lhs.shader_pass_key == rhs.shader_pass_key &&
         lhs.render_tags == rhs.render_tags && lhs.inputs == rhs.inputs &&
         lhs.outputs == rhs.outputs && parameterMapsEqual(lhs.params, rhs.params) &&
         lhs.enabled == rhs.enabled && lhs.clear == rhs.clear &&
         lhs.clear_depth == rhs.clear_depth &&
         colorsEqual(lhs.clear_color, rhs.clear_color);
}

bool shaderPassAssetsEqual(const ShaderPassAssetDesc& lhs,
                           const ShaderPassAssetDesc& rhs) {
  return lhs.shader_pass_key == rhs.shader_pass_key &&
         pipelineDescsEqual(lhs.pipeline, rhs.pipeline) &&
         parameterMapsEqual(lhs.params, rhs.params) && lhs.textures == rhs.textures &&
         lhs.texture_handles == rhs.texture_handles && lhs.fullscreen == rhs.fullscreen &&
         lhs.depth_test == rhs.depth_test && lhs.depth_write == rhs.depth_write &&
         lhs.blend_enabled == rhs.blend_enabled && lhs.blend_mode == rhs.blend_mode &&
         lhs.shader_pass_asset_path == rhs.shader_pass_asset_path;
}

template <typename T, typename Equal>
bool vectorsEqual(const std::vector<T>& lhs,
                  const std::vector<T>& rhs,
                  Equal equal) {
  return lhs.size() == rhs.size() &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin(), std::move(equal));
}

}  // namespace

bool frameGraphsEquivalent(const FrameGraphDesc& lhs, const FrameGraphDesc& rhs) {
  return lhs.frame_graph_key == rhs.frame_graph_key &&
         lhs.output_resource == rhs.output_resource && lhs.enabled == rhs.enabled &&
         vectorsEqual(lhs.resources, rhs.resources, resourcesEqual) &&
         vectorsEqual(lhs.passes, rhs.passes, passesEqual) &&
         vectorsEqual(lhs.shader_pass_assets,
                      rhs.shader_pass_assets,
                      shaderPassAssetsEqual);
}

FrameGraphDesc frameGraphFromPostProcessSettings(const PostProcessSettings& settings,
                                                 std::string frame_graph_key) {
  const PostProcessSettings clamped = clampPostProcessSettings(settings);
  FrameGraphDesc graph = defaultFrameGraphDesc();
  graph.frame_graph_key = std::move(frame_graph_key);
  graph.enabled = clamped.enabled;

  FrameGraphPassDesc pass{};
  pass.name = "post_process";
  pass.kind = FrameGraphPassKind::Builtin;
  pass.builtin_pass = "post_process";
  pass.inputs["source"] = std::string(kFrameGraphCameraColor);
  pass.inputs["depth"] = std::string(kFrameGraphCameraDepth);
  pass.outputs["target"] = std::string(kFrameGraphCameraColor);
  pass.params["enabled"] = clamped.enabled;
  pass.params["bloom_enabled"] = clamped.bloom_enabled;
  pass.params["bloom_threshold"] = clamped.bloom_threshold;
  pass.params["bloom_intensity"] = clamped.bloom_intensity;
  pass.params["bloom_radius"] = clamped.bloom_radius;
  pass.params["tone_mapping_enabled"] = clamped.tone_mapping_enabled;
  pass.params["tone_exposure"] = clamped.tone_exposure;
  pass.params["tone_contrast"] = clamped.tone_contrast;
  pass.params["tone_saturation"] = clamped.tone_saturation;
  pass.params["ssao_enabled"] = clamped.ssao_enabled;
  pass.params["ssao_radius"] = clamped.ssao_radius;
  pass.params["ssao_intensity"] = clamped.ssao_intensity;
  pass.params["ssao_power"] = clamped.ssao_power;
  pass.params["screen_space_reflections_enabled"] =
      clamped.screen_space_reflections_enabled;
  pass.params["ssr_intensity"] = clamped.ssr_intensity;
  pass.params["ssr_max_roughness"] = clamped.ssr_max_roughness;
  pass.params["ssr_thickness"] = clamped.ssr_thickness;
  pass.params["temporal_antialiasing_enabled"] =
      clamped.temporal_antialiasing_enabled;
  pass.params["taa_feedback"] = clamped.taa_feedback;
  pass.params["taa_sharpening"] = clamped.taa_sharpening;
  pass.params["depth_of_field_enabled"] = clamped.depth_of_field_enabled;
  pass.params["dof_focus_depth"] = clamped.dof_focus_depth;
  pass.params["dof_focus_range"] = clamped.dof_focus_range;
  pass.params["dof_intensity"] = clamped.dof_intensity;

  auto lines = std::find_if(graph.passes.begin(),
                            graph.passes.end(),
                            [](const FrameGraphPassDesc& existing) {
    return existing.builtin_pass == "lines";
  });
  graph.passes.insert(lines, std::move(pass));
  return graph;
}

}  // namespace karma::rendering
