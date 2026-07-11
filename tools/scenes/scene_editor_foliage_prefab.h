#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace karma::tools::scene_editor {

/// How one resolved prefab renderer participates in painted foliage.
enum class FoliagePrefabRendererDisposition : uint8_t {
  PaintedRigidMesh,
  IgnoredInvisibleMesh,
  IgnoredDeformableMesh,
  IgnoredInstancedMesh,
  IgnoredMeshWithoutTransform,
};

/// UI-independent description of one renderer found in a resolved prefab.
struct FoliagePrefabRendererSummary {
  std::string node_name;
  std::string mesh_asset_key;
  FoliagePrefabRendererDisposition disposition =
      FoliagePrefabRendererDisposition::PaintedRigidMesh;
  size_t lod_level_count = 0u;
};

/// Resolved renderer eligibility for one prefab and variable-override set.
struct FoliagePrefabInspection {
  std::vector<FoliagePrefabRendererSummary> renderers;
  std::string diagnostic;
  size_t eligible_rigid_meshes = 0u;

  bool inspected() const { return diagnostic.empty(); }
  bool paintable() const {
    return inspected() && eligible_rigid_meshes > 0u;
  }
};

/// Stages prefab data without loading its adjacent package, resolves variables,
/// and classifies the renderer types supported by instanced foliage painting.
class FoliagePrefabInspector {
 public:
  FoliagePrefabInspection inspect(
      const std::filesystem::path& prefab_path,
      const nlohmann::json& variable_overrides = nlohmann::json::object());

  bool validate(
      const std::filesystem::path& prefab_path,
      const nlohmann::json& variable_overrides = nlohmann::json::object(),
      std::string* diagnostic = nullptr);

  void clearCache();

 private:
  struct CacheEntry {
    std::filesystem::path source_path;
    std::filesystem::file_time_type modified =
        std::filesystem::file_time_type::min();
    nlohmann::json variable_overrides = nlohmann::json::object();
    FoliagePrefabInspection inspection;
  };

  std::optional<CacheEntry> cache_;
};

}  // namespace karma::tools::scene_editor
