#include "karma/scenes.h"

#include "karma/assets.h"
#include "scene_light_bake.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace karma::scenes {

namespace {

using Json = nlohmann::json;

constexpr uint32_t kSceneBakeVersion = 2;

std::filesystem::path documentBasePath(const SceneDocument& document) {
  if (!document.reference_root.empty()) {
    return document.reference_root;
  }
  const std::filesystem::path parent = document.source_path.parent_path();
  return parent.empty() ? std::filesystem::path(".") : parent;
}

std::filesystem::path resolveDocumentPath(const SceneDocument& document,
                                          const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) {
    return path;
  }
  return (documentBasePath(document) / path).lexically_normal();
}

std::string stablePath(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

std::filesystem::path portableResolvedPath(
    const SceneDocument& document,
    const std::filesystem::path& resolved_path) {
  if (resolved_path.empty()) return {};
  std::error_code error;
  std::filesystem::path base =
      std::filesystem::absolute(documentBasePath(document), error);
  if (error) base = documentBasePath(document);
  error.clear();
  std::filesystem::path resolved =
      std::filesystem::absolute(resolved_path, error);
  if (error) resolved = resolved_path;
  const std::filesystem::path relative =
      resolved.lexically_normal().lexically_relative(base.lexically_normal());
  if (!relative.empty() && !relative.is_absolute()) {
    bool escapes_root = false;
    for (const auto& part : relative) escapes_root |= part == "..";
    if (!escapes_root) return relative.lexically_normal();
  }
  return resolved_path.filename();
}

std::filesystem::path portableAuthoredPath(
    const SceneDocument& document,
    const std::filesystem::path& authored_path,
    const std::filesystem::path& resolved_path) {
  if (!authored_path.empty() && !authored_path.is_absolute() &&
      !authored_path.has_root_path()) {
    return authored_path.lexically_normal();
  }
  return portableResolvedPath(document, resolved_path);
}

std::filesystem::path portableChildPath(
    const std::filesystem::path& logical_parent,
    std::filesystem::path child) {
  if (child.is_absolute() || child.has_root_path()) child = child.filename();
  return (logical_parent / child).lexically_normal();
}

Json vec3Json(const math::Vec3& value) {
  return Json::array({value.x, value.y, value.z});
}

Json quatJson(const math::Quat& value) {
  return Json::array({value.x, value.y, value.z, value.w});
}

Json pathVectorJson(const std::vector<std::filesystem::path>& paths) {
  Json out = Json::array();
  for (const std::filesystem::path& path : paths) {
    out.push_back(stablePath(path));
  }
  std::sort(out.begin(), out.end(), [](const Json& a, const Json& b) {
    return a.get<std::string>() < b.get<std::string>();
  });
  return out;
}

Json transformJson(const SceneTransform& transform) {
  return Json{
      {"position", vec3Json(transform.position)},
      {"rotation", quatJson(transform.rotation)},
      {"scale", vec3Json(transform.scale)},
  };
}

Json dependencyRecord(std::string_view kind,
                      std::string_view id,
                      const std::filesystem::path& logical_path,
                      const std::filesystem::path& resolved_path) {
  Json record{
      {"kind", std::string(kind)},
      {"id", std::string(id)},
      {"path", stablePath(logical_path)},
      {"exists", false},
      {"hash", nullptr},
  };
  if (!resolved_path.empty() && std::filesystem::exists(resolved_path)) {
    record["exists"] = true;
    if (std::optional<std::string> hash = assets::hashFile(resolved_path);
        hash.has_value()) {
      record["hash"] = *hash;
    }
  }
  return record;
}

void sortDependencies(Json& dependencies) {
  std::sort(dependencies.begin(), dependencies.end(), [](const Json& a, const Json& b) {
    const std::string a_key = a.value("kind", std::string{}) + "\n" +
                              a.value("id", std::string{}) + "\n" +
                              a.value("path", std::string{});
    const std::string b_key = b.value("kind", std::string{}) + "\n" +
                              b.value("id", std::string{}) + "\n" +
                              b.value("path", std::string{});
    return a_key < b_key;
  });
}

void appendPackageManifestDependencies(Json& dependencies,
                                       std::string_view package_id,
                                       std::filesystem::path logical_manifest_path,
                                       std::filesystem::path resolved_manifest_path) {
  std::error_code error;
  if (std::filesystem::is_directory(resolved_manifest_path, error)) {
    resolved_manifest_path /= "assets.package.json";
    logical_manifest_path /= "assets.package.json";
  }
  dependencies.push_back(dependencyRecord("asset_package",
                                          package_id,
                                          logical_manifest_path,
                                          resolved_manifest_path));

  Json manifest;
  try {
    std::ifstream stream(resolved_manifest_path);
    if (!stream) return;
    stream >> manifest;
  } catch (const std::exception&) {
    return;
  }
  const auto assets_it = manifest.find("assets");
  if (!manifest.is_object() || assets_it == manifest.end() ||
      !assets_it->is_array()) {
    return;
  }
  for (const Json& entry : *assets_it) {
    if (!entry.is_object()) continue;
    const std::string key = entry.value("key", std::string{});
    const auto path_it = entry.find("path");
    if (path_it != entry.end() && path_it->is_string() &&
        !path_it->get_ref<const std::string&>().empty()) {
      dependencies.push_back(dependencyRecord(
          "asset_package_source",
          std::string(package_id) + ":" + key,
          portableChildPath(logical_manifest_path.parent_path(),
                            path_it->get_ref<const std::string&>()),
          (resolved_manifest_path.parent_path() /
           path_it->get_ref<const std::string&>())
              .lexically_normal()));
    }
    const auto explicit_dependencies = entry.find("dependencies");
    if (explicit_dependencies == entry.end() ||
        !explicit_dependencies->is_array()) {
      continue;
    }
    size_t index = 0u;
    for (const Json& dependency : *explicit_dependencies) {
      if (dependency.is_string() &&
          !dependency.get_ref<const std::string&>().empty()) {
        dependencies.push_back(dependencyRecord(
            "asset_package_dependency",
            std::string(package_id) + ":" + key + ":" +
                std::to_string(index),
            portableChildPath(logical_manifest_path.parent_path(),
                              dependency.get_ref<const std::string&>()),
            (resolved_manifest_path.parent_path() /
             dependency.get_ref<const std::string&>())
                .lexically_normal()));
      }
      ++index;
    }
  }
}

std::filesystem::path resolvedPrefabPath(const SceneDocument& document,
                                         const ScenePrefabInstance& prefab) {
  std::filesystem::path path =
      resolveDocumentPath(document, prefab.prefab_path);
  std::error_code error;
  if (std::filesystem::is_directory(path, error) || path.extension().empty()) {
    path /= "prefab.json";
  }
  return path.lexically_normal();
}

std::filesystem::path logicalPrefabPath(
    const SceneDocument& document,
    const ScenePrefabInstance& prefab,
    const std::filesystem::path& resolved_path) {
  std::filesystem::path path = prefab.prefab_path;
  if (path.is_absolute()) path = portableResolvedPath(document, resolved_path);
  if (resolved_path.filename() == "prefab.json" &&
      path.filename() != "prefab.json") {
    path /= "prefab.json";
  }
  return path.lexically_normal();
}

struct PrefabPackagePaths {
  std::filesystem::path logical;
  std::filesystem::path resolved;
};

PrefabPackagePaths prefabPackagePaths(
    const std::filesystem::path& logical_prefab_path,
    const std::filesystem::path& resolved_prefab_path) {
  std::filesystem::path package_reference = "assets.package.json";
  try {
    std::ifstream stream(resolved_prefab_path);
    Json prefab_json;
    if (!stream) {
      return {
          .logical = logical_prefab_path.parent_path() / package_reference,
          .resolved = resolved_prefab_path.parent_path() / package_reference,
      };
    }
    stream >> prefab_json;
    const auto package = prefab_json.find("asset_package");
    if (package != prefab_json.end() && package->is_string() &&
        !package->get_ref<const std::string&>().empty()) {
      package_reference = package->get_ref<const std::string&>();
    } else if (package != prefab_json.end() && package->is_object()) {
      const auto path = package->find("path");
      if (path != package->end() && path->is_string() &&
          !path->get_ref<const std::string&>().empty()) {
        package_reference = path->get_ref<const std::string&>();
      }
    }
  } catch (const std::exception&) {
  }
  const std::filesystem::path resolved_package =
      package_reference.is_absolute()
          ? package_reference.lexically_normal()
          : (resolved_prefab_path.parent_path() / package_reference)
                .lexically_normal();
  if (package_reference.is_absolute() || package_reference.has_root_path()) {
    package_reference = package_reference.filename();
  }
  return {
      .logical =
          (logical_prefab_path.parent_path() / package_reference)
              .lexically_normal(),
      .resolved =
          resolved_package,
  };
}

Json dependencyHashes(const SceneDocument& document, const SceneBakeDesc& desc) {
  (void)desc;
  Json dependencies = Json::array();
  if (!document.source_path.empty()) {
    dependencies.push_back(dependencyRecord(
        "scene_document",
        "",
        portableResolvedPath(document, document.source_path),
        document.source_path));
  }
  for (const SceneAssetRef& package : document.asset_packages) {
    const std::filesystem::path resolved_package =
        resolveDocumentPath(document, package.path);
    appendPackageManifestDependencies(
        dependencies,
        package.id,
        portableAuthoredPath(document, package.path, resolved_package),
        resolved_package);
  }
  for (const SceneAssetRef& scene : document.gltf_scenes) {
    const std::filesystem::path resolved_scene =
        resolveDocumentPath(document, scene.path);
    dependencies.push_back(dependencyRecord("gltf_scene",
                                            scene.id,
                                            portableAuthoredPath(
                                                document,
                                                scene.path,
                                                resolved_scene),
                                            resolved_scene));
  }
  for (const ScenePrefabInstance& prefab : document.prefab_instances) {
    const std::filesystem::path prefab_path =
        resolvedPrefabPath(document, prefab);
    const std::filesystem::path logical_prefab_path =
        logicalPrefabPath(document, prefab, prefab_path);
    dependencies.push_back(dependencyRecord("prefab",
                                            prefab.id,
                                            logical_prefab_path,
                                            prefab_path));
    const PrefabPackagePaths package_paths =
        prefabPackagePaths(logical_prefab_path, prefab_path);
    appendPackageManifestDependencies(dependencies,
                                      "prefab:" + prefab.id,
                                      package_paths.logical,
                                      package_paths.resolved);
  }
  for (const SceneEntity& entity : document.entities) {
    if (!entity.components.is_object()) continue;
    const auto foliage = entity.components.find("FoliageComponent");
    if (foliage == entity.components.end() || !foliage->is_object()) {
      continue;
    }
    const auto sidecar = foliage->find("sidecar_path");
    if (sidecar != foliage->end() && sidecar->is_string() &&
        !sidecar->get_ref<const std::string&>().empty()) {
      const std::filesystem::path logical =
          sidecar->get_ref<const std::string&>();
      dependencies.push_back(dependencyRecord(
          "foliage_sidecar",
          entity.id,
          logical,
          resolveDocumentPath(document, logical)));
    }
    const auto prefab = foliage->find("prefab_path");
    if (prefab == foliage->end() || !prefab->is_string() ||
        prefab->get_ref<const std::string&>().empty()) {
      continue;
    }
    std::filesystem::path logical_prefab =
        prefab->get_ref<const std::string&>();
    std::filesystem::path resolved_prefab =
        resolveDocumentPath(document, logical_prefab);
    std::error_code error;
    if (std::filesystem::is_directory(resolved_prefab, error) ||
        resolved_prefab.extension().empty()) {
      resolved_prefab /= "prefab.json";
      logical_prefab /= "prefab.json";
    }
    dependencies.push_back(dependencyRecord(
        "foliage_prefab", entity.id, logical_prefab, resolved_prefab));
    const PrefabPackagePaths package_paths =
        prefabPackagePaths(logical_prefab, resolved_prefab);
    appendPackageManifestDependencies(dependencies,
                                      "foliage_prefab:" + entity.id,
                                      package_paths.logical,
                                      package_paths.resolved);
  }
  sortDependencies(dependencies);
  return dependencies;
}

std::vector<std::string> staticIdsForBake(const SceneDocument& document,
                                          const SceneBakeDesc& desc,
                                          const SceneStaticBuildResult& metadata) {
  std::vector<std::string> ids = desc.static_component_ids;
  if (ids.empty()) {
    ids.reserve(metadata.transforms.size() + document.static_components.size());
    for (const SceneStaticTransform& transform : metadata.transforms) {
      ids.push_back(transform.static_component_id);
    }
    if (ids.empty()) {
      for (const SceneStaticComponent& static_component : document.static_components) {
        ids.push_back(static_component.id);
      }
    }
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}

Json transformsJson(std::vector<SceneStaticTransform> transforms,
                    const std::vector<std::string>& static_ids) {
  std::sort(transforms.begin(),
            transforms.end(),
            [](const SceneStaticTransform& a, const SceneStaticTransform& b) {
              return a.static_component_id < b.static_component_id;
            });

  Json out = Json::array();
  for (const SceneStaticTransform& transform : transforms) {
    if (!static_ids.empty() &&
        !std::binary_search(static_ids.begin(), static_ids.end(), transform.static_component_id)) {
      continue;
    }
    out.push_back(Json{
        {"static_id", transform.static_component_id},
        {"entity_id", transform.entity_id},
        {"local", transformJson(transform.local)},
        {"world", transformJson(transform.world)},
    });
  }
  return out;
}

Json boundsJson(std::vector<SceneStaticBounds> bounds,
                const std::vector<std::string>& static_ids) {
  std::sort(bounds.begin(), bounds.end(), [](const SceneStaticBounds& a, const SceneStaticBounds& b) {
    return a.static_component_id < b.static_component_id;
  });

  Json out = Json::array();
  for (const SceneStaticBounds& bound : bounds) {
    if (!static_ids.empty() &&
        !std::binary_search(static_ids.begin(), static_ids.end(), bound.static_component_id)) {
      continue;
    }
    out.push_back(Json{
        {"static_id", bound.static_component_id},
        {"entity_id", bound.entity_id},
        {"mesh_asset_key", bound.mesh_asset_key},
        {"local_min", vec3Json(bound.local_min)},
        {"local_max", vec3Json(bound.local_max)},
        {"world_min", vec3Json(bound.world_min)},
        {"world_max", vec3Json(bound.world_max)},
        {"world_center", vec3Json(bound.world_center)},
        {"world_radius", bound.world_radius},
    });
  }
  return out;
}

Json navCacheFilesJson(const SceneDocument& document, const SceneBakeDesc& desc) {
  (void)document;
  Json files = Json::array();
  for (const std::filesystem::path& nav_cache : desc.nav_cache_paths) {
    files.push_back(Json{{"path", stablePath(nav_cache)}});
  }
  std::sort(files.begin(), files.end(), [](const Json& a, const Json& b) {
    return a.value("path", std::string{}) < b.value("path", std::string{});
  });
  return files;
}

Json lightmapSettingsJson(const SceneLightmapBakeSettings& settings) {
  return Json{
      {"enabled", settings.enabled},
      {"generate_uv1", settings.generate_uv1},
      {"texels_per_unit", settings.texels_per_unit},
      {"max_atlas_size", settings.max_atlas_size},
      {"padding", settings.padding},
      {"dilation", settings.dilation},
      {"sky_samples", settings.sky_samples},
      {"ao_max_distance", settings.ao_max_distance},
      {"directional", settings.directional},
  };
}

Json navigationSettingsJson(const SceneNavigationBakeSettings& settings) {
  return Json{{"enabled", settings.enabled}};
}

std::string navigationKindName(BakedNavigationKind kind) {
  return kind == BakedNavigationKind::TileCache ? "tile_cache" : "navmesh";
}

Json lightmapBindingsJson(const std::vector<BakedLightmapBinding>& bindings) {
  Json out = Json::array();
  for (const BakedLightmapBinding& binding : bindings) {
    out.push_back(Json{
        {"target_id", binding.target_id},
        {"derived_mesh_asset_key", binding.derived_mesh_asset_key},
        {"irradiance_asset_key", binding.irradiance_asset_key},
        {"direction_asset_key", binding.direction_asset_key},
        {"uv_scale_offset",
         Json::array({binding.uv_scale_offset[0],
                      binding.uv_scale_offset[1],
                      binding.uv_scale_offset[2],
                      binding.uv_scale_offset[3]})},
        {"intensity", binding.intensity},
        {"mixed_light_mask", binding.mixed_light_mask},
    });
  }
  return out;
}

Json navigationBindingsJson(
    const std::vector<BakedNavigationBinding>& bindings) {
  Json out = Json::array();
  for (const BakedNavigationBinding& binding : bindings) {
    out.push_back(Json{
        {"owner_id", binding.owner_id},
        {"kind", navigationKindName(binding.kind)},
        {"path", stablePath(binding.path)},
        {"source_fingerprint", binding.source_fingerprint},
    });
  }
  return out;
}

Json producedAssetsJson(const std::vector<SceneAssetRef>& assets) {
  Json out = Json::array();
  for (const SceneAssetRef& asset : assets) {
    out.push_back(Json{
        {"id", asset.id},
        {"path", stablePath(asset.path)},
        {"type", asset.type},
    });
  }
  std::sort(out.begin(), out.end(), [](const Json& lhs, const Json& rhs) {
    return std::tie(lhs["id"], lhs["type"], lhs["path"]) <
           std::tie(rhs["id"], rhs["type"], rhs["path"]);
  });
  return out;
}

Json bakeSettingsJson(const SceneBakeDesc& desc) {
  return Json{
      {"enabled", desc.enabled},
      {"load_at_runtime", desc.load_at_runtime},
      {"lighting", lightmapSettingsJson(desc.lighting)},
      {"navigation", navigationSettingsJson(desc.navigation)},
  };
}

std::string safeArtifactStem(std::string_view owner_id) {
  std::string out;
  out.reserve(owner_id.size());
  for (const char ch : owner_id) {
    const unsigned char value = static_cast<unsigned char>(ch);
    out.push_back(std::isalnum(value) != 0 || ch == '-' || ch == '_'
                      ? ch
                      : '_');
  }
  return out.empty() ? std::string("navigation") : out;
}

std::filesystem::path defaultNavigationPath(const SceneBakeDesc& desc,
                                            std::string_view owner_id,
                                            BakedNavigationKind kind) {
  std::filesystem::path directory = desc.path.parent_path();
  if (directory.empty()) {
    directory = "bakes";
  }
  const std::string bake_id = desc.id.empty() ? "scene" : safeArtifactStem(desc.id);
  const char* extension =
      kind == BakedNavigationKind::TileCache ? ".kntc" : ".knav";
  return directory /
         (bake_id + "." + safeArtifactStem(owner_id) + extension);
}

#if defined(KARMA_ENABLE_NAVIGATION)
bool bakeNavigationArtifacts(const SceneDocument& document,
                             const SceneBakeDesc& desc,
                             world::World& world,
                             const SceneInstantiateResult& instance,
                             assets::AssetRegistry& assets,
                             std::string_view scene_fingerprint,
                             const SceneBakeExecutionOptions& execution,
                             detail::BakeArtifactTransaction& artifacts,
                             SceneBakeResult& result) {
  std::vector<std::pair<std::string, world::Entity>> owners(
      instance.navigation_owners_by_id.begin(),
      instance.navigation_owners_by_id.end());
  std::sort(owners.begin(), owners.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.first < rhs.first;
  });
  owners.erase(std::remove_if(owners.begin(),
                              owners.end(),
                              [&](const auto& owner) {
                                return !world.isAlive(owner.second) ||
                                       !world.has<components::NavMeshComponent>(
                                           owner.second);
                              }),
               owners.end());
  if (owners.empty()) {
    return true;
  }

  navigation::NavigationSystem navigation_system(&assets);
  navigation_system.update(world, 0.0f);

  size_t output_index = 0u;
  for (size_t index = 0u; index < owners.size(); ++index) {
    if (execution.is_cancelled && execution.is_cancelled()) {
      result.cancelled = true;
      result.diagnostic = "scene bake cancelled during navigation";
      return false;
    }
    if (execution.on_progress) {
      execution.on_progress(SceneBakeProgress{
          .stage = SceneBakeStage::Navigation,
          .current = static_cast<uint64_t>(index),
          .total = static_cast<uint64_t>(owners.size()),
          .message = "Writing navigation artifact for " + owners[index].first,
      });
    }

    const std::string& owner_id = owners[index].first;
    const world::Entity owner = owners[index].second;
    auto& nav_mesh = world.get<components::NavMeshComponent>(owner);
    if (!nav_mesh.enabled || !nav_mesh.built || !nav_mesh.nav_mesh.isValid()) {
      result.diagnostic = "navigation bake failed for owner '" + owner_id +
                          "': " + nav_mesh.last_build_result.message;
      return false;
    }

    const bool has_tile_cache =
        world.has<components::NavTileCacheComponent>(owner) &&
        world.get<components::NavTileCacheComponent>(owner).enabled;
    const BakedNavigationKind kind =
        has_tile_cache ? BakedNavigationKind::TileCache
                       : BakedNavigationKind::NavMesh;
    const std::filesystem::path relative_path =
        output_index < desc.nav_cache_paths.size()
            ? desc.nav_cache_paths[output_index]
            : defaultNavigationPath(desc, owner_id, kind);
    ++output_index;
    if (!detail::isPortableBakeArtifactPath(relative_path)) {
      result.diagnostic =
          "navigation artifacts require content-root-relative paths: " +
          relative_path.generic_string();
      return false;
    }
    const std::filesystem::path output_path =
        resolveDocumentPath(document, relative_path);
    const std::filesystem::path staged_path = artifacts.stage(output_path);

    bool saved = false;
    if (kind == BakedNavigationKind::TileCache) {
      auto& tile_cache = world.get<components::NavTileCacheComponent>(owner);
      if (!tile_cache.built || !tile_cache.tile_cache.isValid()) {
        result.diagnostic = "navigation tile-cache bake failed for owner '" +
                            owner_id + "': " +
                            tile_cache.last_build_result.message;
        return false;
      }
      saved = assets::saveNavTileCacheSnapshot(
          staged_path, tile_cache.tile_cache.snapshot(nav_mesh.nav_mesh));
    } else {
      const std::shared_ptr<const navigation::NavMeshSnapshot> snapshot =
          nav_mesh.nav_mesh.snapshot();
      saved = snapshot != nullptr &&
              assets::saveNavMeshSnapshot(staged_path, *snapshot);
    }
    if (!saved) {
      result.diagnostic = "failed to write navigation artifact: " +
                          output_path.generic_string();
      return false;
    }

    const std::string source_fingerprint = assets::hashString(
        std::string(scene_fingerprint) + "\n" + owner_id + "\n" +
        navigationKindName(kind));
    result.navigation_bindings.push_back(BakedNavigationBinding{
        .owner_id = owner_id,
        .kind = kind,
        .path = relative_path,
        .source_fingerprint = source_fingerprint,
    });
    result.produced_assets.push_back(SceneAssetRef{
        .id = owner_id,
        .path = relative_path,
        .type = kind == BakedNavigationKind::TileCache
                    ? "navigation_tile_cache"
                    : "navigation_navmesh",
    });
  }
  return true;
}
#endif

Json fingerprintInput(const SceneDocument& document,
                      const SceneBakeDesc& desc,
                      const Json& dependencies,
                      const std::vector<std::string>& static_ids) {
  return Json{
      {"schema", "karma.scene_bake.fingerprint"},
      {"version", kSceneBakeVersion},
      {"source_scene",
       stablePath(portableResolvedPath(document, document.source_path))},
      {"bake_id", desc.id},
      {"bake_path", stablePath(desc.path)},
      {"settings", bakeSettingsJson(desc)},
      {"dependency_hashes", dependencies},
      {"static_ids", static_ids},
      {"nav_cache_paths", pathVectorJson(desc.nav_cache_paths)},
      {"document", sceneDocumentToJson(document)},
      {"document_counts",
       Json{{"asset_packages", document.asset_packages.size()},
            {"gltf_scenes", document.gltf_scenes.size()},
            {"prefab_instances", document.prefab_instances.size()},
            {"entities", document.entities.size()},
            {"static_components", document.static_components.size()},
            {"bakes", document.bakes.size()}}},
  };
}

}  // namespace

std::string sceneBakeFingerprint(const SceneDocument& document,
                                 const SceneBakeDesc& desc) {
  const Json dependencies = dependencyHashes(document, desc);
  std::vector<std::string> static_ids = desc.static_component_ids;
  if (static_ids.empty()) {
    static_ids.reserve(document.static_components.size());
    for (const SceneStaticComponent& component : document.static_components) {
      static_ids.push_back(component.id);
    }
  }
  std::sort(static_ids.begin(), static_ids.end());
  static_ids.erase(std::unique(static_ids.begin(), static_ids.end()),
                   static_ids.end());
  return assets::hashString(
      fingerprintInput(document, desc, dependencies, static_ids).dump());
}

SceneBakeResult bakeScene(const SceneDocument& document,
                          const SceneBakeDesc& desc,
                          const SceneBakeExecutionOptions& execution) {
  SceneBakeResult result{};
  result.output_path = resolveDocumentPath(document, desc.path);

  auto report = [&](SceneBakeStage stage,
                    uint64_t current,
                    uint64_t total,
                    std::string message) {
    if (execution.on_progress) {
      execution.on_progress(SceneBakeProgress{
          .stage = stage,
          .current = current,
          .total = total,
          .message = std::move(message),
      });
    }
  };
  auto cancelled = [&](std::string_view stage) {
    if (!execution.is_cancelled || !execution.is_cancelled()) {
      return false;
    }
    result.cancelled = true;
    result.diagnostic = "scene bake cancelled during " + std::string(stage);
    return true;
  };

  try {
    detail::BakeArtifactTransaction artifact_transaction;
    report(SceneBakeStage::Preparing, 0u, 1u, "Preparing scene bake");
    if (cancelled("preparation")) {
      return result;
    }

    assets::AssetRegistry assets;
    world::World world;
    world::Scene scene;

    SceneInstantiateDesc instantiate_desc{};
    instantiate_desc.instantiate_gltf_scenes = false;
    instantiate_desc.instantiate_prefabs = true;
    instantiate_desc.instantiate_authored_entities = true;
    instantiate_desc.attach_authored_components = true;

    // Offline baking must always inspect authored source state. Loading a
    // previous bake here could replace source meshes/materials and make a
    // rebake recursively consume its own derived artifacts.
    SceneDocument source_document = document;
    for (SceneBakeDesc& source_bake : source_document.bakes) {
      source_bake.load_at_runtime = false;
    }

    SceneInstantiateResult instance =
        instantiateScene(world,
                         scene,
                         assets,
                         source_document,
                         instantiate_desc);
    if (!instance.success) {
      result.diagnostic = "scene bake failed to instantiate scene";
      if (!instance.diagnostics.empty()) {
        result.diagnostic += ": " + instance.diagnostics.front();
      }
      return result;
    }
    report(SceneBakeStage::Preparing, 1u, 1u, "Scene instantiated");
    if (cancelled("static metadata")) {
      destroyScene(world, scene, instance);
      return result;
    }

    report(SceneBakeStage::StaticMetadata,
           0u,
           1u,
           "Building static scene metadata");
    const SceneStaticBuildResult static_metadata =
        buildSceneStaticMetadata(document, instance, world, scene, assets);
    if (!static_metadata.success) {
      destroyScene(world, scene, instance);
      result.diagnostic = "scene bake failed to build static metadata";
      if (!static_metadata.diagnostics.empty()) {
        result.diagnostic += ": " + static_metadata.diagnostics.front();
      }
      return result;
    }
    report(SceneBakeStage::StaticMetadata,
           1u,
           1u,
           "Static scene metadata complete");

    const Json dependencies = dependencyHashes(document, desc);
    const std::vector<std::string> static_ids =
        staticIdsForBake(document, desc, static_metadata);
    const std::string fingerprint = sceneBakeFingerprint(document, desc);

    result.scene_fingerprint = fingerprint;
    const bool run_navigation = desc.enabled && desc.navigation.enabled &&
                                execution.bake_navigation;
    const bool run_lighting = desc.enabled && desc.lighting.enabled &&
                              execution.bake_lighting;
    report(SceneBakeStage::Navigation,
           0u,
           1u,
           run_navigation
               ? "Baking navigation artifacts"
               : (!execution.bake_navigation
                      ? "Navigation bake not requested"
                      : "Navigation bake disabled"));
    if (cancelled("navigation")) {
      destroyScene(world, scene, instance);
      return result;
    }
#if defined(KARMA_ENABLE_NAVIGATION)
    if (run_navigation &&
        !bakeNavigationArtifacts(document,
                                 desc,
                                 world,
                                 instance,
                                 assets,
                                 fingerprint,
                                 execution,
                                 artifact_transaction,
                                 result)) {
      destroyScene(world, scene, instance);
      return result;
    }
#endif
    report(SceneBakeStage::Navigation,
           1u,
           1u,
           !execution.bake_navigation
               ? "Navigation bake not requested"
               : (!desc.enabled || !desc.navigation.enabled
               ? "Navigation bake disabled"
               :
#if defined(KARMA_ENABLE_NAVIGATION)
                 "Navigation bake complete"
#else
                 "Navigation support unavailable in this build"
#endif
                 ));

    report(SceneBakeStage::Lighting,
           0u,
           1u,
           run_lighting
               ? "Baking CPU lightmaps"
               : (!execution.bake_lighting
                      ? "Lighting bake not requested"
                      : "Lighting bake disabled"));
    if (cancelled("lighting")) {
      destroyScene(world, scene, instance);
      return result;
    }
    if (run_lighting &&
        !detail::bakeSceneLightmaps(document,
                                    desc,
                                    world,
                                    instance,
                                    assets,
                                    fingerprint,
                                    execution,
                                    artifact_transaction,
                                    result)) {
      destroyScene(world, scene, instance);
      return result;
    }
    report(SceneBakeStage::Lighting, 1u, 1u, "Lighting stage complete");
    destroyScene(world, scene, instance);

    if (cancelled("finalization")) {
      return result;
    }
    report(SceneBakeStage::Finalizing, 0u, 1u, "Finalizing bake manifest");
    const Json lightmap_bindings = lightmapBindingsJson(result.lightmap_bindings);
    const Json navigation_bindings =
        navigationBindingsJson(result.navigation_bindings);
    const Json produced_assets = producedAssetsJson(result.produced_assets);
    result.metadata = Json{
        {"schema", "karma.scene_bake"},
        {"version", kSceneBakeVersion},
        {"source_scene",
         stablePath(portableResolvedPath(document, document.source_path))},
        {"scene_fingerprint", fingerprint},
        {"bake",
         Json{{"id", desc.id},
              {"path", stablePath(desc.path)},
              {"enabled", desc.enabled},
              {"load_at_runtime", desc.load_at_runtime},
              {"lighting", lightmapSettingsJson(desc.lighting)},
              {"navigation", navigationSettingsJson(desc.navigation)}}},
        {"dependency_hashes", dependencies},
        {"static_ids", static_ids},
        {"static_metadata",
         Json{{"transforms", transformsJson(static_metadata.transforms, static_ids)},
              {"bounds", boundsJson(static_metadata.bounds, static_ids)},
              {"skipped_static_components", static_metadata.skipped_static_components},
              {"diagnostics", static_metadata.diagnostics}}},
        {"nav_cache_files", navCacheFilesJson(document, desc)},
        {"baked_lighting", Json::array()},
        {"produced_assets", produced_assets},
        {"lightmap_bindings", lightmap_bindings},
        {"navigation_bindings", navigation_bindings},
        {"lighting_output",
         Json{{"generated", !result.lightmap_bindings.empty()},
              {"status",
               !execution.bake_lighting
                   ? "not_requested"
                   : (!desc.enabled || !desc.lighting.enabled
                   ? "disabled"
                   : (result.lightmap_bindings.empty() ? "not_generated"
                                                       : "generated"))},
              {"mixed_light_ids", result.mixed_light_ids},
              {"warnings", result.lighting_warnings},
              {"statistics",
               Json{{"ray_queries", result.lighting_statistics.ray_queries},
                    {"bvh_node_visits",
                     result.lighting_statistics.bvh_node_visits},
                    {"triangle_tests",
                     result.lighting_statistics.triangle_tests}}},
              {"bindings", lightmap_bindings}}},
        {"navigation_output",
         Json{{"generated", !result.navigation_bindings.empty()},
              {"status",
               !execution.bake_navigation
                   ? "not_requested"
                   : (!desc.enabled || !desc.navigation.enabled
                          ? "disabled"
                          : (result.navigation_bindings.empty()
                                 ?
#if defined(KARMA_ENABLE_NAVIGATION)
                                   "not_generated"
#else
                                   "unavailable"
#endif
                                 : "generated"))},
              {"bindings", navigation_bindings}}},
    };

    std::string publish_diagnostic;
    if (!artifact_transaction.publish(&publish_diagnostic)) {
      result.diagnostic = "failed to publish scene bake artifacts: " +
                          publish_diagnostic;
      return result;
    }

    result.success = true;
    report(SceneBakeStage::Finalizing, 1u, 1u, "Bake manifest finalized");
    report(SceneBakeStage::Complete, 1u, 1u, "Scene bake complete");
  } catch (const std::exception& e) {
    result.diagnostic = std::string("scene bake failed: ") + e.what();
  }
  return result;
}

}  // namespace karma::scenes
