#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "karma/rendering/renderer/ids.h"

namespace karma::renderer {

/// \ingroup karma_rendering
/// Renderer-owned deformation payload for one draw-time mesh instance.
///
/// Joint matrices are final mesh-space skinning matrices. Morph weights are
/// indexed by the target order stored on `geometry::MeshData::morph_targets`.
struct DeformationDesc {
  std::vector<glm::mat4> joint_palette;
  std::vector<float> morph_weights;
  bool skinning_enabled = false;
  bool morphing_enabled = false;
};

/// \ingroup karma_rendering
/// Runtime deformation resource counters for diagnostics and debug overlays.
struct DeformationStats {
  uint32_t resource_count = 0;
  uint32_t joint_matrix_count = 0;
  uint32_t morph_weight_count = 0;
};

}  // namespace karma::renderer
