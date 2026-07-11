#include "foliage_render_prototype.h"

#include "karma/prefabs.h"

#include <system_error>
#include <utility>

namespace karma::foliage::detail {
namespace {

void addDiagnostic(FoliageRenderPrototypeBuild& result,
                   std::filesystem::path path,
                   std::string message) {
  result.diagnostics.push_back(FoliageRenderPrototypeDiagnostic{
      .path = std::move(path),
      .message = std::move(message),
  });
}

bool validatePrototypeAssets(const FoliageRenderPrototypePart& part,
                             assets::AssetRegistry& assets,
                             std::string& error) {
  const auto validate_mesh = [&](const std::string& key) {
    if (key.empty() || assets.findMeshAsset(key) == nullptr) {
      error = "prefab render prototype references missing mesh asset '" +
              key + "'";
      return false;
    }
    return true;
  };
  const auto validate_materials = [&](const auto& materials) {
    for (const auto& binding : materials) {
      if (binding.material_key.empty() ||
          !assets.resolveMaterial(binding.material_key).has_value()) {
        error =
            "prefab render prototype references missing material asset '" +
            binding.material_key + "'";
        return false;
      }
    }
    return true;
  };
  if (!validate_mesh(part.mesh.mesh_asset_key) ||
      !validate_materials(part.mesh.materials)) {
    return false;
  }
  if (!part.lod.has_value()) return true;

  std::string validation_error;
  if (!components::validateLodComponent(*part.lod, &validation_error)) {
    error = std::move(validation_error);
    return false;
  }
  for (const components::LodLevel& level : part.lod->levels) {
    if (!validate_mesh(level.mesh_asset_key) ||
        !validate_materials(level.materials)) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::filesystem::path resolveFoliagePrefabPath(
    std::filesystem::path path) {
  std::error_code error;
  if (std::filesystem::is_directory(path, error) || path.extension().empty()) {
    path /= "prefab.json";
  }
  return path.lexically_normal();
}

std::filesystem::file_time_type foliagePrefabModifiedTime(
    const std::filesystem::path& path) {
  std::error_code error;
  const auto result = std::filesystem::last_write_time(path, error);
  return error ? std::filesystem::file_time_type::min() : result;
}

void releaseFoliageRenderPrototypePackage(
    assets::AssetRegistry* assets,
    std::optional<assets::AssetPackageHandle>& package) {
  if (package.has_value() && assets != nullptr) {
    assets->sharedPackageStore().releasePackage(*package);
  }
  package.reset();
}

FoliageRenderPrototypeBuild buildFoliageRenderPrototype(
    world::World& world,
    world::Entity source,
    const components::FoliageComponent& component,
    const std::filesystem::path& reference_root,
    assets::AssetRegistry* assets) {
  FoliageRenderPrototypeBuild result{};
  if (component.prefab_path.empty()) {
    FoliageRenderPrototypePart part{};
    part.mesh.mesh_asset_key = component.mesh_asset_key;
    part.mesh.materials = component.materials;
    part.mesh.visible = component.visible;
    part.mesh.shadow_visible = component.shadow_visible;
    if (world.has<components::LodComponent>(source)) {
      part.lod = world.get<components::LodComponent>(source);
    }
    if (world.has<components::RenderTagsComponent>(source)) {
      part.render_tags =
          world.get<components::RenderTagsComponent>(source).tags;
    }
    std::string error;
    if (part.lod.has_value() &&
        !components::validateLodComponent(*part.lod, &error)) {
      addDiagnostic(result, component.sidecar_path, std::move(error));
      return result;
    }
    if (assets != nullptr &&
        !validatePrototypeAssets(part, *assets, error)) {
      addDiagnostic(result, component.sidecar_path, std::move(error));
      return result;
    }
    result.parts.push_back(std::move(part));
    result.success = true;
    return result;
  }

  if (assets == nullptr) {
    addDiagnostic(result,
                  component.prefab_path,
                  "prefab-backed foliage requires an AssetRegistry");
    return result;
  }
  std::filesystem::path prefab_path = component.prefab_path;
  if (prefab_path.is_relative() && !reference_root.empty()) {
    prefab_path = reference_root / prefab_path;
  }
  prefab_path = resolveFoliagePrefabPath(std::move(prefab_path));
  result.resolved_prefab_path = prefab_path;
  result.prefab_modified = foliagePrefabModifiedTime(prefab_path);

  const std::filesystem::path manifest =
      assets::resolveAssetPackagePath(prefab_path.parent_path());
  std::error_code filesystem_error;
  if (std::filesystem::exists(manifest, filesystem_error)) {
    std::string diagnostic;
    result.prefab_package =
        assets->sharedPackageStore().acquirePackage(manifest, &diagnostic);
    if (!result.prefab_package.has_value()) {
      addDiagnostic(result,
                    manifest,
                    diagnostic.empty()
                        ? "failed to acquire the prefab asset package"
                        : std::move(diagnostic));
      return result;
    }
  } else if (filesystem_error) {
    addDiagnostic(result,
                  manifest,
                  "failed to inspect prefab asset package: " +
                      filesystem_error.message());
    return result;
  }

  prefabs::PrefabInstantiateDesc desc{};
  desc.assets = assets;
  if (component.prefab_variables.is_object()) {
    for (auto it = component.prefab_variables.begin();
         it != component.prefab_variables.end(); ++it) {
      desc.variables[it.key()] = it.value();
    }
  }
  world::World staging_world;
  world::Scene staging_scene;
  std::optional<prefabs::PrefabInstance> instance =
      prefabs::instantiatePrefab(
          staging_world, staging_scene, prefab_path, desc);
  if (!instance.has_value()) {
    addDiagnostic(result,
                  prefab_path,
                  "failed to instantiate the foliage render prototype");
    releaseFoliageRenderPrototypePackage(assets, result.prefab_package);
    return result;
  }
  world::updateWorldTransforms(staging_world, staging_scene);

  std::vector<std::string> ignored_renderers;
  for (world::Entity entity : instance->entities) {
    if (!staging_world.has<components::MeshComponent>(entity) ||
        !staging_world.has<components::TransformComponent>(entity)) {
      continue;
    }
    const auto& mesh = staging_world.get<components::MeshComponent>(entity);
    const bool visible =
        mesh.visible &&
        (!staging_world.has<components::VisibilityComponent>(entity) ||
         staging_world.get<components::VisibilityComponent>(entity).visible);
    if (!visible) continue;
    if (staging_world.has<components::DeformableMeshComponent>(entity) &&
        staging_world.get<components::DeformableMeshComponent>(entity)
            .enabled) {
      ignored_renderers.push_back(
          staging_world.has<components::TagComponent>(entity)
              ? staging_world.get<components::TagComponent>(entity).name
              : ("entity " + std::to_string(entity.index)));
      continue;
    }
    FoliageRenderPrototypePart part{};
    part.mesh = mesh;
    const auto& transform =
        staging_world.get<components::TransformComponent>(entity);
    part.local_position = transform.getPosition();
    part.local_rotation = transform.getRotation();
    part.local_scale = transform.getScale();
    if (staging_world.has<components::LodComponent>(entity)) {
      part.lod = staging_world.get<components::LodComponent>(entity);
    }
    if (staging_world.has<components::RenderTagsComponent>(entity)) {
      part.render_tags =
          staging_world.get<components::RenderTagsComponent>(entity).tags;
    }
    std::string error;
    if (!validatePrototypeAssets(part, *assets, error)) {
      prefabs::destroyPrefab(
          staging_world, staging_scene, instance->root);
      addDiagnostic(result, prefab_path, std::move(error));
      releaseFoliageRenderPrototypePackage(assets, result.prefab_package);
      result.parts.clear();
      return result;
    }
    result.parts.push_back(std::move(part));
  }
  prefabs::destroyPrefab(staging_world, staging_scene, instance->root);

  for (const std::string& name : ignored_renderers) {
    addDiagnostic(result,
                  prefab_path,
                  "ignored deformable prefab renderer '" + name + "'");
  }
  if (result.parts.empty()) {
    addDiagnostic(result,
                  prefab_path,
                  "prefab contains no eligible rigid MeshComponent renderers");
    releaseFoliageRenderPrototypePackage(assets, result.prefab_package);
    return result;
  }
  result.success = true;
  return result;
}

}  // namespace karma::foliage::detail
