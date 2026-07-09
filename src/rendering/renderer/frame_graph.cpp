#include "karma/rendering.h"

#include <algorithm>
#include <string>

namespace karma::rendering {

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
