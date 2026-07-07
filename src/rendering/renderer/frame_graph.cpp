#include "karma/rendering.h"

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace karma::rendering {

namespace {

enum class GraphResourceClass {
  Any,
  Color,
  Depth,
  Backbuffer,
};

bool isImplicitResource(std::string_view name) {
  return name == kFrameGraphCameraColor ||
         name == kFrameGraphCameraDepth ||
         name == kFrameGraphBackbuffer;
}

bool isKnownBuiltinPass(std::string_view name) {
  return name == "clear" ||
         name == "skybox" ||
         name == "shadows" ||
         name == "opaque" ||
         name == "terrain" ||
         name == "transparent" ||
         name == "particles" ||
         name == "lines" ||
         name == "post_process" ||
         name == "present" ||
         name == "copy" ||
         name == "blit" ||
         name == "final_composite" ||
         name == "bloom" ||
         name == "taa";
}

bool containsKey(const std::vector<std::string>& keys, std::string_view key) {
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool isColorResource(FrameGraphResourceKind kind) {
  return kind == FrameGraphResourceKind::ColorTexture ||
         kind == FrameGraphResourceKind::ExternalColor ||
         kind == FrameGraphResourceKind::Backbuffer;
}

bool isDepthResource(FrameGraphResourceKind kind) {
  return kind == FrameGraphResourceKind::DepthTexture ||
         kind == FrameGraphResourceKind::ExternalDepth;
}

const char* resourceClassName(GraphResourceClass resource_class) {
  switch (resource_class) {
    case GraphResourceClass::Any: return "resource";
    case GraphResourceClass::Color: return "color resource";
    case GraphResourceClass::Depth: return "depth resource";
    case GraphResourceClass::Backbuffer: return "backbuffer resource";
  }
  return "resource";
}

bool matchesResourceClass(FrameGraphResourceKind kind,
                          GraphResourceClass resource_class) {
  switch (resource_class) {
    case GraphResourceClass::Any:
      return true;
    case GraphResourceClass::Color:
      return isColorResource(kind);
    case GraphResourceClass::Depth:
      return isDepthResource(kind);
    case GraphResourceClass::Backbuffer:
      return kind == FrameGraphResourceKind::Backbuffer;
  }
  return false;
}

void addDiagnostic(FrameGraphValidationResult& result, std::string message) {
  result.diagnostics.push_back(std::move(message));
}

bool requireSlot(const FrameGraphPassDesc& pass,
                 const std::unordered_map<std::string, FrameGraphResourceKind>& resources,
                 const std::unordered_map<std::string, std::string>& slots,
                 std::string_view slot,
                 GraphResourceClass resource_class,
                 std::string_view direction,
                 FrameGraphValidationResult& result) {
  const auto found_slot = slots.find(std::string(slot));
  if (found_slot == slots.end() || found_slot->second.empty()) {
    addDiagnostic(result,
                  "graph pass '" + pass.name + "' requires " +
                      std::string(direction) + " slot '" + std::string(slot) + "'");
    return false;
  }

  const auto found_resource = resources.find(found_slot->second);
  if (found_resource == resources.end()) {
    return false;
  }
  if (!matchesResourceClass(found_resource->second, resource_class)) {
    addDiagnostic(result,
                  "graph pass '" + pass.name + "' " + std::string(direction) +
                      " slot '" + std::string(slot) + "' expects " +
                      resourceClassName(resource_class) + ": " + found_slot->second);
    return false;
  }
  return true;
}

void validateOptionalSlot(
    const FrameGraphPassDesc& pass,
    const std::unordered_map<std::string, FrameGraphResourceKind>& resources,
    const std::unordered_map<std::string, std::string>& slots,
    std::string_view slot,
    GraphResourceClass resource_class,
    std::string_view direction,
    FrameGraphValidationResult& result) {
  const auto found_slot = slots.find(std::string(slot));
  if (found_slot == slots.end() || found_slot->second.empty()) {
    return;
  }
  const auto found_resource = resources.find(found_slot->second);
  if (found_resource == resources.end()) {
    return;
  }
  if (!matchesResourceClass(found_resource->second, resource_class)) {
    addDiagnostic(result,
                  "graph pass '" + pass.name + "' " + std::string(direction) +
                      " slot '" + std::string(slot) + "' expects " +
                      resourceClassName(resource_class) + ": " + found_slot->second);
  }
}

void validateBuiltinContract(
    const FrameGraphPassDesc& pass,
    const std::unordered_map<std::string, FrameGraphResourceKind>& resources,
    FrameGraphValidationResult& result) {
  const std::string_view builtin = pass.builtin_pass;
  if (builtin == "clear") {
    requireSlot(pass, resources, pass.outputs, "target", GraphResourceClass::Color,
                "output", result);
    requireSlot(pass, resources, pass.outputs, "depth", GraphResourceClass::Depth,
                "output", result);
  } else if (builtin == "skybox") {
    validateOptionalSlot(pass, resources, pass.inputs, "depth",
                         GraphResourceClass::Depth, "input", result);
    requireSlot(pass, resources, pass.outputs, "target", GraphResourceClass::Color,
                "output", result);
  } else if (builtin == "opaque" || builtin == "terrain") {
    validateOptionalSlot(pass, resources, pass.inputs, "source",
                         GraphResourceClass::Color, "input", result);
    validateOptionalSlot(pass, resources, pass.inputs, "depth",
                         GraphResourceClass::Depth, "input", result);
    requireSlot(pass, resources, pass.outputs, "target", GraphResourceClass::Color,
                "output", result);
    requireSlot(pass, resources, pass.outputs, "depth", GraphResourceClass::Depth,
                "output", result);
  } else if (builtin == "transparent" || builtin == "particles" ||
             builtin == "lines") {
    validateOptionalSlot(pass, resources, pass.inputs, "source",
                         GraphResourceClass::Color, "input", result);
    requireSlot(pass, resources, pass.inputs, "depth", GraphResourceClass::Depth,
                "input", result);
    requireSlot(pass, resources, pass.outputs, "target", GraphResourceClass::Color,
                "output", result);
  } else if (builtin == "post_process" || builtin == "final_composite" ||
             builtin == "bloom" || builtin == "taa") {
    requireSlot(pass, resources, pass.inputs, "source", GraphResourceClass::Color,
                "input", result);
    validateOptionalSlot(pass, resources, pass.inputs, "depth",
                         GraphResourceClass::Depth, "input", result);
    requireSlot(pass, resources, pass.outputs, "target", GraphResourceClass::Color,
                "output", result);
  } else if (builtin == "present") {
    requireSlot(pass, resources, pass.inputs, "source", GraphResourceClass::Color,
                "input", result);
    requireSlot(pass, resources, pass.outputs, "target", GraphResourceClass::Backbuffer,
                "output", result);
  }
}

void validateShaderContract(
    const FrameGraphPassDesc& pass,
    const std::unordered_map<std::string, FrameGraphResourceKind>& resources,
    FrameGraphValidationResult& result) {
  validateOptionalSlot(pass, resources, pass.inputs, "source",
                       GraphResourceClass::Color, "input", result);
  validateOptionalSlot(pass, resources, pass.inputs, "history",
                       GraphResourceClass::Color, "input", result);
  validateOptionalSlot(pass, resources, pass.inputs, "depth",
                       GraphResourceClass::Depth, "input", result);
  validateOptionalSlot(pass, resources, pass.outputs, "target",
                       GraphResourceClass::Color, "output", result);
  validateOptionalSlot(pass, resources, pass.outputs, "color",
                       GraphResourceClass::Color, "output", result);
  validateOptionalSlot(pass, resources, pass.outputs, "depth",
                       GraphResourceClass::Depth, "output", result);
}

void validateSceneContract(
    const FrameGraphPassDesc& pass,
    const std::unordered_map<std::string, FrameGraphResourceKind>& resources,
    FrameGraphValidationResult& result) {
  validateOptionalSlot(pass, resources, pass.outputs, "target",
                       GraphResourceClass::Color, "output", result);
  validateOptionalSlot(pass, resources, pass.outputs, "color",
                       GraphResourceClass::Color, "output", result);
  validateOptionalSlot(pass, resources, pass.outputs, "depth",
                       GraphResourceClass::Depth, "output", result);
}

void validateCopyContract(
    const FrameGraphPassDesc& pass,
    const std::unordered_map<std::string, FrameGraphResourceKind>& resources,
    FrameGraphValidationResult& result) {
  requireSlot(pass, resources, pass.inputs, "source", GraphResourceClass::Color,
              "input", result);
  requireSlot(pass, resources, pass.outputs, "target", GraphResourceClass::Color,
              "output", result);
}

void validateSceneMaskContract(
    const FrameGraphPassDesc& pass,
    const std::unordered_map<std::string, FrameGraphResourceKind>& resources,
    FrameGraphValidationResult& result) {
  requireSlot(pass, resources, pass.outputs, "target", GraphResourceClass::Color,
              "output", result);
  validateOptionalSlot(pass, resources, pass.outputs, "depth",
                       GraphResourceClass::Depth, "output", result);
  std::unordered_set<std::string> tags;
  for (const std::string& tag : pass.render_tags) {
    if (tag.empty()) {
      addDiagnostic(result, "scene_mask graph pass '" + pass.name +
                                "' render tags must not be empty");
    } else if (!tags.insert(tag).second) {
      addDiagnostic(result, "scene_mask graph pass '" + pass.name +
                                "' has duplicate render tag: " + tag);
    }
  }
  if (pass.clear_depth) {
    const auto depth = pass.outputs.find("depth");
    if (depth == pass.outputs.end() || depth->second.empty()) {
      addDiagnostic(result, "scene_mask graph pass '" + pass.name +
                                "' clear_depth requires depth output");
    }
  }
}

void addEdge(std::vector<std::vector<size_t>>& edges, size_t from, size_t to) {
  if (from == to) {
    return;
  }
  std::vector<size_t>& outgoing = edges[from];
  if (std::find(outgoing.begin(), outgoing.end(), to) == outgoing.end()) {
    outgoing.push_back(to);
  }
}

}  // namespace

FrameGraphValidationResult validateFrameGraphDesc(
    const FrameGraphDesc& graph,
    const FrameGraphValidationOptions& options) {
  FrameGraphValidationResult result{};
  if (!graph.enabled) {
    return result;
  }

  std::unordered_map<std::string, FrameGraphResourceKind> resources;
  resources.emplace(std::string(kFrameGraphCameraColor),
                    FrameGraphResourceKind::ExternalColor);
  resources.emplace(std::string(kFrameGraphCameraDepth),
                    FrameGraphResourceKind::ExternalDepth);
  resources.emplace(std::string(kFrameGraphBackbuffer),
                    FrameGraphResourceKind::Backbuffer);

  for (const FrameGraphResourceDesc& resource : graph.resources) {
    if (resource.name.empty()) {
      addDiagnostic(result, "frame graph resource name must not be empty");
      continue;
    }
    if (isImplicitResource(resource.name)) {
      addDiagnostic(result, "frame graph resource '" + resource.name +
                                "' conflicts with an implicit camera resource");
    }
    if (!resources.emplace(resource.name, resource.kind).second) {
      addDiagnostic(result, "duplicate frame graph resource: " + resource.name);
    }
    if (resource.size_mode == FrameGraphResourceSizeMode::CameraRelative) {
      if (resource.width_scale <= 0.0f || resource.height_scale <= 0.0f) {
        addDiagnostic(result, "camera-relative resource '" + resource.name +
                                  "' must have positive scale");
      }
    } else if (resource.width == 0u || resource.height == 0u) {
      addDiagnostic(result, "absolute resource '" + resource.name +
                                "' must have non-zero width and height");
    }
    if (resource.kind == FrameGraphResourceKind::Backbuffer &&
        resource.name != kFrameGraphBackbuffer) {
      addDiagnostic(result, "backbuffer resources must use the implicit backbuffer name");
    }
  }

  if (graph.output_resource.empty()) {
    addDiagnostic(result, "frame graph output_resource must not be empty");
  } else if (const auto output = resources.find(graph.output_resource);
             output == resources.end()) {
    addDiagnostic(result, "frame graph output_resource references missing resource: " +
                              graph.output_resource);
  } else if (!isColorResource(output->second)) {
    addDiagnostic(result, "frame graph output_resource must be a color resource: " +
                              graph.output_resource);
  }

  std::unordered_set<std::string> pass_names;
  std::unordered_map<std::string, size_t> first_writer_by_resource;
  std::vector<std::vector<size_t>> edges(graph.passes.size());

  for (size_t index = 0; index < graph.passes.size(); ++index) {
    const FrameGraphPassDesc& pass = graph.passes[index];
    if (!pass.enabled) {
      continue;
    }
    for (const auto& [slot, resource_name] : pass.outputs) {
      (void)slot;
      if (!resource_name.empty()) {
        first_writer_by_resource.try_emplace(resource_name, index);
      }
    }
  }

  std::unordered_map<std::string, size_t> last_writer_by_resource;
  for (size_t index = 0; index < graph.passes.size(); ++index) {
    const FrameGraphPassDesc& pass = graph.passes[index];
    if (!pass.enabled) {
      continue;
    }
    if (pass.name.empty()) {
      addDiagnostic(result, "frame graph pass name must not be empty");
    } else if (!pass_names.insert(pass.name).second) {
      addDiagnostic(result, "duplicate frame graph pass: " + pass.name);
    }

    switch (pass.kind) {
      case FrameGraphPassKind::Scene:
        validateSceneContract(pass, resources, result);
        break;
      case FrameGraphPassKind::Builtin:
        if (pass.builtin_pass.empty()) {
          addDiagnostic(result, "builtin graph pass '" + pass.name +
                                    "' requires builtin_pass");
        } else if (!isKnownBuiltinPass(pass.builtin_pass)) {
          addDiagnostic(result, "unknown builtin graph pass '" + pass.builtin_pass +
                                    "' in pass '" + pass.name + "'");
        } else {
          validateBuiltinContract(pass, resources, result);
        }
        break;
      case FrameGraphPassKind::Shader:
        if (pass.shader_pass_key.empty()) {
          addDiagnostic(result, "shader graph pass '" + pass.name +
                                    "' requires shader_pass_key");
        } else if (options.require_shader_pass_keys &&
                   !containsKey(options.shader_pass_keys, pass.shader_pass_key)) {
          addDiagnostic(result, "shader graph pass '" + pass.name +
                                    "' references missing shader pass: " +
                                    pass.shader_pass_key);
        }
        validateShaderContract(pass, resources, result);
        break;
      case FrameGraphPassKind::Copy:
        validateCopyContract(pass, resources, result);
        break;
      case FrameGraphPassKind::SceneMask:
        if (pass.render_tags.empty()) {
          addDiagnostic(result, "scene_mask graph pass '" + pass.name +
                                    "' requires at least one render tag");
        }
        validateSceneMaskContract(pass, resources, result);
        break;
    }

    for (const auto& [slot, resource_name] : pass.inputs) {
      (void)slot;
      if (resource_name.empty() || resources.find(resource_name) == resources.end()) {
        addDiagnostic(result, "graph pass '" + pass.name +
                                  "' input references missing resource: " + resource_name);
        continue;
      }
      if (const auto writer = last_writer_by_resource.find(resource_name);
          writer != last_writer_by_resource.end()) {
        addEdge(edges, writer->second, index);
      } else if (const auto writer = first_writer_by_resource.find(resource_name);
                 writer != first_writer_by_resource.end() && writer->second != index) {
        addEdge(edges, writer->second, index);
      }
    }

    for (const auto& [slot, resource_name] : pass.outputs) {
      (void)slot;
      if (resource_name.empty() || resources.find(resource_name) == resources.end()) {
        addDiagnostic(result, "graph pass '" + pass.name +
                                  "' output references missing resource: " + resource_name);
        continue;
      }
      if (const auto writer = last_writer_by_resource.find(resource_name);
          writer != last_writer_by_resource.end()) {
        addEdge(edges, writer->second, index);
      }
      last_writer_by_resource[resource_name] = index;
    }
  }

  std::vector<uint8_t> visit_state(graph.passes.size(), 0u);
  std::function<bool(size_t)> visit = [&](size_t node) {
    if (visit_state[node] == 1u) {
      return true;
    }
    if (visit_state[node] == 2u) {
      return false;
    }
    visit_state[node] = 1u;
    for (size_t next : edges[node]) {
      if (visit(next)) {
        return true;
      }
    }
    visit_state[node] = 2u;
    return false;
  };

  for (size_t index = 0; index < graph.passes.size(); ++index) {
    if (visit(index)) {
      addDiagnostic(result, "frame graph contains a dependency cycle");
      break;
    }
  }

  return result;
}

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
