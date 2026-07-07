#include "karma/rendering.h"

#include <algorithm>
#include <string>

namespace karma::rendering {

FrameGraphDesc frameGraphFromPostProcessSettings(const PostProcessSettings& settings,
                                                 std::string frame_graph_key) {
  FrameGraphDesc graph = defaultFrameGraphDesc();
  graph.frame_graph_key = std::move(frame_graph_key);
  graph.enabled = settings.enabled;

  FrameGraphPassDesc pass{};
  pass.name = "post_process";
  pass.kind = FrameGraphPassKind::Builtin;
  pass.builtin_pass = "post_process";
  pass.inputs["source"] = std::string(kFrameGraphCameraColor);
  pass.inputs["depth"] = std::string(kFrameGraphCameraDepth);
  pass.outputs["target"] = std::string(kFrameGraphCameraColor);
  pass.params["enabled"] = settings.enabled;
  pass.params["bloom_enabled"] = settings.bloom_enabled;
  pass.params["bloom_threshold"] = settings.bloom_threshold;
  pass.params["bloom_intensity"] = settings.bloom_intensity;
  pass.params["bloom_radius"] = settings.bloom_radius;
  pass.params["tone_mapping_enabled"] = settings.tone_mapping_enabled;
  pass.params["tone_exposure"] = settings.tone_exposure;
  pass.params["tone_contrast"] = settings.tone_contrast;
  pass.params["tone_saturation"] = settings.tone_saturation;
  pass.params["ssao_enabled"] = settings.ssao_enabled;
  pass.params["ssao_radius"] = settings.ssao_radius;
  pass.params["ssao_intensity"] = settings.ssao_intensity;
  pass.params["ssao_power"] = settings.ssao_power;
  pass.params["screen_space_reflections_enabled"] =
      settings.screen_space_reflections_enabled;
  pass.params["ssr_intensity"] = settings.ssr_intensity;
  pass.params["ssr_max_roughness"] = settings.ssr_max_roughness;
  pass.params["ssr_thickness"] = settings.ssr_thickness;
  pass.params["temporal_antialiasing_enabled"] =
      settings.temporal_antialiasing_enabled;
  pass.params["taa_feedback"] = settings.taa_feedback;
  pass.params["taa_sharpening"] = settings.taa_sharpening;
  pass.params["depth_of_field_enabled"] = settings.depth_of_field_enabled;
  pass.params["dof_focus_depth"] = settings.dof_focus_depth;
  pass.params["dof_focus_range"] = settings.dof_focus_range;
  pass.params["dof_intensity"] = settings.dof_intensity;

  auto lines = std::find_if(graph.passes.begin(),
                            graph.passes.end(),
                            [](const FrameGraphPassDesc& existing) {
    return existing.builtin_pass == "lines";
  });
  graph.passes.insert(lines, std::move(pass));
  return graph;
}

}  // namespace karma::rendering
