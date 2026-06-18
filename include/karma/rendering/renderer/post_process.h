#pragma once

namespace karma::renderer {

/// \ingroup karma_rendering
/// Renderer-owned image-space effects applied after scene rendering.
///
/// All effects are opt-in. Backends may implement individual effects with
/// different quality/performance tradeoffs, but unsupported features should be
/// ignored rather than changing scene rendering semantics.
///
/// Settings are normally registered in `content::AssetRegistry` and selected
/// by `CameraComponent::post_process_profile_key`. `RenderSystem` resolves the
/// active settings per camera pass before calling the backend.
struct PostProcessSettings {
  /// Master switch for the profile. When false, backends skip post processing.
  bool enabled = true;

  /// Enables the Diligent bloom prefilter/downsample/upsample chain.
  bool bloom_enabled = false;
  float bloom_threshold = 1.0f;
  float bloom_intensity = 0.25f;
  float bloom_radius = 1.0f;

  /// Enables final tone/color mapping in the display composite path.
  bool tone_mapping_enabled = false;
  float tone_exposure = 1.0f;
  float tone_contrast = 1.0f;
  float tone_saturation = 1.0f;

  /// Enables depth-derived ambient occlusion in the current composite path.
  bool ssao_enabled = false;
  float ssao_radius = 1.5f;
  float ssao_intensity = 0.35f;
  float ssao_power = 1.4f;

  /// Enables screen-space reflections in the current composite path.
  bool screen_space_reflections_enabled = false;
  float ssr_intensity = 0.25f;
  float ssr_max_roughness = 0.75f;
  float ssr_thickness = 0.08f;

  /// Enables temporal history blending where the backend has history resources.
  bool temporal_antialiasing_enabled = false;
  float taa_feedback = 0.92f;
  float taa_sharpening = 0.08f;

  /// Enables depth-of-field controls in the current composite path.
  bool depth_of_field_enabled = false;
  float dof_focus_depth = 8.0f;
  float dof_focus_range = 4.0f;
  float dof_intensity = 1.0f;
};

}  // namespace karma::renderer
