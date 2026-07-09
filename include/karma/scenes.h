#pragma once

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/math.h"
#include "karma/world.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace karma::assets {
class AssetRegistry;
struct AssetPackageHandle;
}  // namespace karma::assets

namespace karma::scenes {

/// Scene source format version supported by this parser.
constexpr uint32_t kSceneDocumentVersion = 1;

/// Authored transform data used by scene entities and instances.
struct SceneTransform {
  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
};

/// Stable-id scene asset reference with a portable relative source path.
struct SceneAssetRef {
  std::string id;
  std::filesystem::path path;
  std::filesystem::path baked_cache_path;
  std::string type;
  std::string asset_package_id;
};

/// Serialized scene entity record. Runtime instantiation is intentionally out of scope.
struct SceneEntity {
  std::string id;
  std::string name;
  std::string parent_id;
  SceneTransform transform{};
  nlohmann::json components = nlohmann::json::object();
};

/// Serialized prefab instance record.
struct ScenePrefabInstance {
  std::string id;
  std::filesystem::path prefab_path;
  std::string asset_package_id;
  std::string parent_entity_id;
  SceneTransform transform{};
  nlohmann::json variables = nlohmann::json::object();
};

/// Scene-level environment settings.
struct SceneEnvironment {
  std::string id;
  std::string entity_id;
  std::string environment_map_asset_id;
  std::filesystem::path environment_map_path;
  components::EnvironmentComponent component{};
};

/// Scene camera authoring record.
struct SceneCamera {
  std::string id;
  std::string entity_id;
  components::CameraComponent component{};
};

/// Scene light authoring record.
struct SceneLight {
  std::string id;
  std::string entity_id;
  components::LightComponent component{};
};

/// Static render/lighting membership for an authored entity.
struct SceneStaticComponent {
  std::string id;
  std::string entity_id;
  std::string gltf_scene_id;
  std::string mesh_asset_key;
  std::string material_asset_key;
  bool transform = true;
  bool render = true;
  bool lighting = false;
  bool collision = false;
  bool navigation = false;
  bool casts_shadows = true;
  bool receives_baked_lighting = false;
};

/// Precomputed transform for an authored static scene node.
struct SceneStaticTransform {
  std::string static_component_id;
  std::string entity_id;
  world::Entity entity{};
  SceneTransform local{};
  SceneTransform world{};
};

/// Precomputed local and world-space mesh bounds for an authored static node.
struct SceneStaticBounds {
  std::string static_component_id;
  std::string entity_id;
  std::string mesh_asset_key;
  world::Entity entity{};
  math::Vec3 local_min{};
  math::Vec3 local_max{};
  math::Vec3 world_min{};
  math::Vec3 world_max{};
  math::Vec3 world_center{};
  float world_radius = 0.0f;
};

/// Options for static scene metadata analysis.
struct SceneStaticBuildDesc {
  bool build_mesh_bounds = true;
  bool require_scene_node = true;
  bool include_gltf_static_components = false;
};

/// Static scene metadata produced for future bake/runtime acceleration paths.
struct SceneStaticBuildResult {
  bool success = true;
  std::vector<std::string> diagnostics;
  std::vector<SceneStaticTransform> transforms;
  std::vector<SceneStaticBounds> bounds;
  size_t skipped_static_components = 0;
};

/// Baked lighting attachment authored by scene bake tools.
struct BakedLightingComponent {
  std::string bake_id;
  std::string entity_id;
  std::string lightmap_asset_key;
  std::filesystem::path lightmap_path;
  float intensity = 1.0f;
  bool enabled = true;
};

/// Offline bake request stored in a scene document.
struct SceneBakeDesc {
  std::string id;
  std::filesystem::path path;
  std::vector<std::string> static_component_ids;
  std::vector<std::filesystem::path> nav_cache_paths;
  BakedLightingComponent baked_lighting{};
};

/// Offline bake result contract for tools.
struct SceneBakeResult {
  bool success = false;
  std::string diagnostic;
  std::filesystem::path output_path;
  std::string scene_fingerprint;
  nlohmann::json metadata = nlohmann::json::object();
  std::vector<SceneAssetRef> produced_assets;
  std::vector<BakedLightingComponent> baked_lighting;
};

/// Options for loading a scene source document.
struct SceneLoadDesc {
  std::filesystem::path path;
  bool require_kscene_json_extension = true;
};

/// Parsed scene source document.
struct SceneDocument {
  uint32_t version = kSceneDocumentVersion;
  std::string name;
  std::filesystem::path source_path;
  std::vector<SceneAssetRef> asset_packages;
  std::vector<SceneAssetRef> gltf_scenes;
  std::vector<ScenePrefabInstance> prefab_instances;
  std::vector<SceneEntity> entities;
  std::optional<SceneEnvironment> environment;
  std::vector<SceneCamera> cameras;
  std::vector<SceneLight> lights;
  std::vector<SceneStaticComponent> static_components;
  std::vector<SceneBakeDesc> bakes;
};

}  // namespace karma::scenes

namespace karma::assets {

/// Parsed Karma scene document registered as a package asset.
struct SceneAsset {
  std::filesystem::path source_path;
  scenes::SceneDocument document;
};

}  // namespace karma::assets

namespace karma::scenes {

/// Result of scene document parsing and validation.
struct SceneLoadResult {
  std::optional<SceneDocument> document;
  std::vector<std::string> diagnostics;

  bool success() const { return document.has_value() && diagnostics.empty(); }
};

/// Loads and validates a `*.kscene.json` scene document.
SceneLoadResult loadSceneDocument(const SceneLoadDesc& desc);

/// Loads and validates a `*.kscene.json` scene document.
SceneLoadResult loadSceneDocument(const std::filesystem::path& path);

/// Runtime options for instantiating a parsed scene document.
struct SceneInstantiateDesc {
  bool instantiate_gltf_scenes = true;
  bool instantiate_prefabs = true;
  bool instantiate_authored_entities = true;
  bool attach_authored_components = true;
  bool create_synthetic_gltf_roots = false;
  bool autoplay_gltf_animations = false;
};

/// Runtime scene instance and stable authored-id mappings.
struct SceneInstantiateResult {
  bool success = false;
  std::vector<std::string> diagnostics;

  assets::AssetRegistry* asset_registry = nullptr;
  std::vector<assets::AssetPackageHandle> asset_packages;
  std::vector<assets::AssetPackageHandle> prefab_asset_packages;

  std::vector<world::Entity> entities;
  std::vector<world::Entity> prefab_roots;
  std::unordered_map<std::string, world::Entity> entities_by_id;
  std::unordered_map<std::string, world::Entity> gltf_scene_roots_by_id;
  std::unordered_map<std::string, std::vector<world::Entity>> gltf_scene_entities_by_id;
  std::unordered_map<std::string, world::Entity> prefab_roots_by_id;
  std::unordered_map<std::string, world::Entity> cameras_by_id;
  std::unordered_map<std::string, world::Entity> lights_by_id;

  /// Finds the primary entity associated with a scene-authored id.
  world::Entity find(std::string_view scene_id) const;
};

/// Instantiates a parsed scene document into ECS and scene graph state.
SceneInstantiateResult instantiateScene(world::World& world,
                                        world::Scene& scene,
                                        assets::AssetRegistry& assets,
                                        const SceneDocument& document,
                                        const SceneInstantiateDesc& desc = {});

/// Builds non-mutating static transform and bounds metadata for authored static nodes.
SceneStaticBuildResult buildSceneStaticMetadata(
    const SceneDocument& document,
    const SceneInstantiateResult& instance,
    const world::World& world,
    const world::Scene& scene,
    const assets::AssetRegistry& assets,
    const SceneStaticBuildDesc& desc = {});

/// Builds deterministic V1 scene bake metadata for offline tools.
SceneBakeResult bakeScene(const SceneDocument& document,
                          const SceneBakeDesc& desc = {});

/// Destroys entities and releases scene-owned asset packages from a scene instance.
bool destroyScene(world::World& world,
                  world::Scene& scene,
                  SceneInstantiateResult& result);

}  // namespace karma::scenes
