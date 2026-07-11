#include "scene_editor_foliage_prefab.h"

#include "karma/components.h"
#include "karma/prefabs.h"
#include "karma/world.h"

#include <system_error>
#include <utility>

namespace karma::tools::scene_editor {
namespace {

std::filesystem::path resolvedPrefabPath(std::filesystem::path path) {
  std::error_code error;
  if (std::filesystem::is_directory(path, error) || path.extension().empty()) {
    path /= "prefab.json";
  }
  return path.lexically_normal();
}

std::string joinDiagnostics(const std::vector<std::string>& diagnostics) {
  std::string joined;
  for (const std::string& diagnostic : diagnostics) {
    if (!joined.empty()) joined += '\n';
    joined += diagnostic;
  }
  return joined;
}

}  // namespace

FoliagePrefabInspection FoliagePrefabInspector::inspect(
    const std::filesystem::path& prefab_path,
    const nlohmann::json& variable_overrides) {
  const std::filesystem::path resolved_path =
      resolvedPrefabPath(prefab_path);
  std::error_code filesystem_error;
  const std::filesystem::file_time_type modified =
      std::filesystem::last_write_time(resolved_path, filesystem_error);
  const std::filesystem::file_time_type stable_modified =
      filesystem_error ? std::filesystem::file_time_type::min() : modified;
  const nlohmann::json normalized_overrides =
      variable_overrides.is_object() ? variable_overrides
                                     : nlohmann::json::object();
  if (cache_.has_value() && cache_->source_path == resolved_path &&
      cache_->modified == stable_modified &&
      cache_->variable_overrides == normalized_overrides) {
    return cache_->inspection;
  }
  const auto finish = [&](FoliagePrefabInspection inspection) {
    cache_ = CacheEntry{
        .source_path = resolved_path,
        .modified = stable_modified,
        .variable_overrides = normalized_overrides,
        .inspection = inspection,
    };
    return inspection;
  };

  FoliagePrefabInspection result{};
  const prefabs::PrefabLoadResult loaded =
      prefabs::loadPrefabDocument(resolved_path);
  if (!loaded.success() || !loaded.document.has_value()) {
    result.diagnostic = loaded.diagnostics.empty()
                            ? "Prefab source could not be resolved"
                            : joinDiagnostics(loaded.diagnostics);
    return finish(std::move(result));
  }

  prefabs::PrefabInstantiateDesc desc{};
  desc.auto_load_package = false;
  for (auto override = normalized_overrides.begin();
       override != normalized_overrides.end(); ++override) {
    desc.variables[override.key()] = override.value();
  }
  world::World staging_world;
  world::Scene staging_scene;
  std::optional<prefabs::PrefabInstance> instance =
      prefabs::instantiatePrefab(
          staging_world, staging_scene, resolved_path, desc);
  if (!instance.has_value() || !instance->valid()) {
    result.diagnostic =
        "Prefab could not be instantiated for foliage validation";
    return finish(std::move(result));
  }

  world::updateWorldTransforms(staging_world, staging_scene);
  for (const prefabs::PrefabNode& node : loaded.document->nodes) {
    const world::Entity entity = instance->find(node.id);
    if (!staging_world.isAlive(entity)) continue;
    const std::string node_name =
        node.name.empty() ? "Node " + std::to_string(node.id) : node.name;

    if (staging_world.has<components::InstancedMeshComponent>(entity)) {
      const auto& renderer =
          staging_world.get<components::InstancedMeshComponent>(entity);
      result.renderers.push_back(FoliagePrefabRendererSummary{
          .node_name = node_name,
          .mesh_asset_key = renderer.mesh_asset_key,
          .disposition =
              FoliagePrefabRendererDisposition::IgnoredInstancedMesh,
      });
    }

    const bool deformable =
        staging_world.has<components::DeformableMeshComponent>(entity) &&
        staging_world.get<components::DeformableMeshComponent>(entity)
            .enabled;
    if (!staging_world.has<components::MeshComponent>(entity)) {
      if (deformable) {
        result.renderers.push_back(FoliagePrefabRendererSummary{
            .node_name = node_name,
            .mesh_asset_key = {},
            .disposition =
                FoliagePrefabRendererDisposition::IgnoredDeformableMesh,
        });
      }
      continue;
    }

    const auto& mesh = staging_world.get<components::MeshComponent>(entity);
    FoliagePrefabRendererSummary summary{
        .node_name = node_name,
        .mesh_asset_key = mesh.mesh_asset_key,
    };
    if (deformable) {
      summary.disposition =
          FoliagePrefabRendererDisposition::IgnoredDeformableMesh;
    } else if (!staging_world.has<components::TransformComponent>(entity)) {
      summary.disposition =
          FoliagePrefabRendererDisposition::IgnoredMeshWithoutTransform;
    } else {
      const bool visible =
          mesh.visible &&
          (!staging_world.has<components::VisibilityComponent>(entity) ||
           staging_world.get<components::VisibilityComponent>(entity)
               .visible);
      if (!visible) {
        summary.disposition =
            FoliagePrefabRendererDisposition::IgnoredInvisibleMesh;
      } else {
        summary.disposition =
            FoliagePrefabRendererDisposition::PaintedRigidMesh;
        ++result.eligible_rigid_meshes;
        if (staging_world.has<components::LodComponent>(entity)) {
          summary.lod_level_count =
              staging_world.get<components::LodComponent>(entity)
                  .levels.size();
        }
      }
    }
    result.renderers.push_back(std::move(summary));
  }
  prefabs::destroyPrefab(staging_world, staging_scene, instance->root);
  return finish(std::move(result));
}

bool FoliagePrefabInspector::validate(
    const std::filesystem::path& prefab_path,
    const nlohmann::json& variable_overrides,
    std::string* diagnostic) {
  const FoliagePrefabInspection inspection =
      inspect(prefab_path, variable_overrides);
  if (inspection.paintable()) {
    if (diagnostic != nullptr) diagnostic->clear();
    return true;
  }
  if (diagnostic != nullptr) {
    *diagnostic = inspection.diagnostic.empty()
                      ? "Foliage prefabs require at least one visible rigid "
                        "MeshComponent. InstancedMeshComponent and enabled "
                        "DeformableMeshComponent renderers are ignored."
                      : inspection.diagnostic;
  }
  return false;
}

void FoliagePrefabInspector::clearCache() {
  cache_.reset();
}

}  // namespace karma::tools::scene_editor
