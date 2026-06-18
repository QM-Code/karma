#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace karma::geometry {

/// \ingroup karma_world
/// Index range for one draw subset of a mesh.
struct MeshSubmesh {
  uint32_t index_offset = 0;
  uint32_t index_count = 0;
  uint32_t material_slot = 0;
};

/// \ingroup karma_world
/// Material slot authored by a mesh asset.
struct MeshMaterialSlot {
  std::string name;
  std::string default_material_key;
};

/// \ingroup karma_world
/// Shared CPU-side mesh geometry used by importers, simulation, and rendering uploads.
struct MeshData {
  std::vector<glm::vec3> vertices;
  std::vector<glm::vec3> normals;
  std::vector<glm::vec2> uvs;
  std::vector<glm::vec2> uvs1;
  std::vector<glm::vec4> tangents;
  std::vector<glm::uvec4> joint_indices;
  std::vector<glm::vec4> joint_weights;
  std::vector<uint32_t> indices;

  /// Per-target morph delta payload.
  struct MorphTarget {
    std::vector<glm::vec3> position_deltas;
    std::vector<glm::vec3> normal_deltas;
    std::vector<glm::vec3> tangent_deltas;
  };
  std::vector<MorphTarget> morph_targets;

  /// Draw ranges and material-slot indices. Empty means the whole index buffer is one submesh.
  std::vector<MeshSubmesh> submeshes;
  /// Authored material slots and their default material asset keys.
  std::vector<MeshMaterialSlot> material_slots;
};

}  // namespace karma::geometry
