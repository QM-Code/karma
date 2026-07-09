#include "karma/scenes.h"

#include "karma/assets.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace karma::scenes {

namespace {

using Json = nlohmann::json;

constexpr uint32_t kSceneBakeVersion = 1;

std::filesystem::path documentBasePath(const SceneDocument& document) {
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
                      const std::filesystem::path& path) {
  Json record{
      {"kind", std::string(kind)},
      {"id", std::string(id)},
      {"path", stablePath(path)},
      {"exists", false},
      {"hash", nullptr},
  };
  if (!path.empty() && std::filesystem::exists(path)) {
    record["exists"] = true;
    if (std::optional<std::string> hash = assets::hashFile(path); hash.has_value()) {
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

Json dependencyHashes(const SceneDocument& document, const SceneBakeDesc& desc) {
  Json dependencies = Json::array();
  if (!document.source_path.empty()) {
    dependencies.push_back(dependencyRecord("scene_document", "", document.source_path));
  }
  for (const SceneAssetRef& package : document.asset_packages) {
    dependencies.push_back(dependencyRecord("asset_package",
                                            package.id,
                                            resolveDocumentPath(document, package.path)));
  }
  for (const SceneAssetRef& scene : document.gltf_scenes) {
    dependencies.push_back(dependencyRecord("gltf_scene",
                                            scene.id,
                                            resolveDocumentPath(document, scene.path)));
  }
  for (const ScenePrefabInstance& prefab : document.prefab_instances) {
    dependencies.push_back(dependencyRecord("prefab",
                                            prefab.id,
                                            resolveDocumentPath(document, prefab.prefab_path)));
  }
  if (!desc.path.empty()) {
    dependencies.push_back(dependencyRecord("bake",
                                            desc.id,
                                            resolveDocumentPath(document, desc.path)));
  }
  for (const std::filesystem::path& nav_cache : desc.nav_cache_paths) {
    dependencies.push_back(dependencyRecord("nav_cache",
                                            desc.id,
                                            resolveDocumentPath(document, nav_cache)));
  }
  sortDependencies(dependencies);
  return dependencies;
}

std::vector<std::string> staticIdsForBake(const SceneDocument& document,
                                          const SceneBakeDesc& desc) {
  std::vector<std::string> ids = desc.static_component_ids;
  if (ids.empty()) {
    ids.reserve(document.static_components.size());
    for (const SceneStaticComponent& static_component : document.static_components) {
      ids.push_back(static_component.id);
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
  Json files = Json::array();
  for (const std::filesystem::path& nav_cache : desc.nav_cache_paths) {
    const std::filesystem::path resolved = resolveDocumentPath(document, nav_cache);
    if (!std::filesystem::exists(resolved)) {
      continue;
    }
    Json record{
        {"path", stablePath(resolved)},
        {"hash", nullptr},
    };
    if (std::optional<std::string> hash = assets::hashFile(resolved); hash.has_value()) {
      record["hash"] = *hash;
    }
    files.push_back(std::move(record));
  }
  std::sort(files.begin(), files.end(), [](const Json& a, const Json& b) {
    return a.value("path", std::string{}) < b.value("path", std::string{});
  });
  return files;
}

Json bakedLightingJson(const BakedLightingComponent& baked_lighting) {
  if (baked_lighting.entity_id.empty() &&
      baked_lighting.lightmap_asset_key.empty() &&
      baked_lighting.lightmap_path.empty()) {
    return Json::array();
  }
  return Json::array({Json{
      {"bake_id", baked_lighting.bake_id},
      {"entity_id", baked_lighting.entity_id},
      {"lightmap_asset_key", baked_lighting.lightmap_asset_key},
      {"lightmap_path", stablePath(baked_lighting.lightmap_path)},
      {"intensity", baked_lighting.intensity},
      {"enabled", baked_lighting.enabled},
  }});
}

Json fingerprintInput(const SceneDocument& document,
                      const SceneBakeDesc& desc,
                      const Json& dependencies,
                      const std::vector<std::string>& static_ids) {
  return Json{
      {"schema", "karma.scene_bake.fingerprint"},
      {"version", kSceneBakeVersion},
      {"source_scene", stablePath(document.source_path)},
      {"bake_id", desc.id},
      {"bake_path", stablePath(desc.path)},
      {"dependency_hashes", dependencies},
      {"static_ids", static_ids},
      {"nav_cache_paths", pathVectorJson(desc.nav_cache_paths)},
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

SceneBakeResult bakeScene(const SceneDocument& document, const SceneBakeDesc& desc) {
  SceneBakeResult result{};
  result.output_path = resolveDocumentPath(document, desc.path);
  if (!desc.baked_lighting.entity_id.empty() ||
      !desc.baked_lighting.lightmap_asset_key.empty() ||
      !desc.baked_lighting.lightmap_path.empty()) {
    result.baked_lighting.push_back(desc.baked_lighting);
  }

  try {
    assets::AssetRegistry assets;
    world::World world;
    world::Scene scene;

    SceneInstantiateDesc instantiate_desc{};
    instantiate_desc.instantiate_gltf_scenes = false;
    instantiate_desc.instantiate_prefabs = false;
    instantiate_desc.instantiate_authored_entities = true;
    instantiate_desc.attach_authored_components = true;

    SceneInstantiateResult instance =
        instantiateScene(world, scene, assets, document, instantiate_desc);
    if (!instance.success) {
      result.diagnostic = "scene bake failed to instantiate scene";
      if (!instance.diagnostics.empty()) {
        result.diagnostic += ": " + instance.diagnostics.front();
      }
      return result;
    }

    const SceneStaticBuildResult static_metadata =
        buildSceneStaticMetadata(document, instance, world, scene, assets);
    destroyScene(world, scene, instance);
    if (!static_metadata.success) {
      result.diagnostic = "scene bake failed to build static metadata";
      if (!static_metadata.diagnostics.empty()) {
        result.diagnostic += ": " + static_metadata.diagnostics.front();
      }
      return result;
    }

    const Json dependencies = dependencyHashes(document, desc);
    const std::vector<std::string> static_ids = staticIdsForBake(document, desc);
    const std::string fingerprint =
        assets::hashString(fingerprintInput(document, desc, dependencies, static_ids).dump());

    result.scene_fingerprint = fingerprint;
    result.metadata = Json{
        {"schema", "karma.scene_bake"},
        {"version", kSceneBakeVersion},
        {"source_scene", stablePath(document.source_path)},
        {"scene_fingerprint", fingerprint},
        {"bake", Json{{"id", desc.id}, {"path", stablePath(desc.path)}}},
        {"dependency_hashes", dependencies},
        {"static_ids", static_ids},
        {"static_metadata",
         Json{{"transforms", transformsJson(static_metadata.transforms, static_ids)},
              {"bounds", boundsJson(static_metadata.bounds, static_ids)},
              {"skipped_static_components", static_metadata.skipped_static_components},
              {"diagnostics", static_metadata.diagnostics}}},
        {"nav_cache_files", navCacheFilesJson(document, desc)},
        {"baked_lighting", bakedLightingJson(desc.baked_lighting)},
    };

    result.success = true;
  } catch (const std::exception& e) {
    result.diagnostic = std::string("scene bake failed: ") + e.what();
  }
  return result;
}

}  // namespace karma::scenes
