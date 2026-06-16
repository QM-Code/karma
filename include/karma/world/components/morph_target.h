#pragma once

#include <string>
#include <vector>

#include "karma/world/geometry/mesh_data.h"
#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Runtime morph target state for one renderable mesh.
///
/// GLB scene instantiation adds this to renderable primitive entities when a
/// primitive has morph target deltas. Animation systems write `weights`; the
/// mesh deformation upload stage applies those weights to `bind_mesh` and
/// updates the renderer mesh before skinning.
struct MorphTargetComponent : ecs::ComponentTag {
  /// Author/import bind mesh containing morph target delta payloads.
  geometry::MeshData bind_mesh;
  /// Last CPU-deformed mesh produced from `bind_mesh` and `weights`.
  geometry::MeshData deformed_mesh;
  /// Authored default weights used when an active clip has no morph track.
  std::vector<float> base_weights;
  /// Runtime weights, normally written by `AnimationSystem`.
  std::vector<float> weights;
  /// Human-readable runtime diagnostic for tooling.
  std::string diagnostic;
  /// Set when weights changed and the renderer mesh needs a new upload.
  bool weights_dirty = true;
  /// Whether the renderer mesh currently contains CPU-applied morph deltas.
  bool renderer_mesh_is_deformed = false;
  /// Enables runtime morph deformation for this primitive.
  bool enabled = true;
};

}  // namespace karma::components
