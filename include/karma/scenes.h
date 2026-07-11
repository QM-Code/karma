#pragma once

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/math.h"
#include "karma/world.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <functional>
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
  /// Optional instance-level static membership applied to the linked root.
  /// This is the only supported per-instance component override.
  std::optional<components::StaticComponent> static_component;
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
  /// Non-negative baked-light contribution multiplier.
  float intensity = 1.0f;
  bool enabled = true;
};

/// Settings for the bounded, deterministic CPU lightmap baker.
///
/// The portable core retains usable authored UV1 data and can derive a
/// deterministic per-triangle UV1 atlas when needed. CPU work is capped by
/// implementation limits even when a larger atlas/sample count is requested.
struct SceneLightmapBakeSettings {
  bool enabled = true;
  bool generate_uv1 = true;
  float texels_per_unit = 16.0f;
  uint32_t max_atlas_size = 2048u;
  uint32_t padding = 4u;
  uint32_t dilation = 8u;
  uint32_t sky_samples = 64u;
  float ao_max_distance = 10.0f;
  bool directional = true;
};

/// Settings for deterministic navigation artifact generation.
struct SceneNavigationBakeSettings {
  bool enabled = true;
};

/// One real lightmap artifact assignment in a scene bake manifest.
struct BakedLightmapBinding {
  std::string target_id;
  std::string derived_mesh_asset_key;
  std::string irradiance_asset_key;
  std::string direction_asset_key;
  std::array<float, 4> uv_scale_offset{1.0f, 1.0f, 0.0f, 0.0f};
  float intensity = 1.0f;
  uint64_t mixed_light_mask = 0u;
};

/// Kind of serialized navigation state owned by a scene or prefab entity.
enum class BakedNavigationKind : uint8_t {
  NavMesh,
  TileCache,
};

/// One navigation artifact assignment in a scene bake manifest.
struct BakedNavigationBinding {
  using Kind = BakedNavigationKind;

  std::string owner_id;
  BakedNavigationKind kind = BakedNavigationKind::NavMesh;
  std::filesystem::path path;
  std::string source_fingerprint;
};

/// Deterministic work counters from the CPU lightmap ray tracer.
struct SceneLightBakeStatistics {
  uint64_t ray_queries = 0u;
  uint64_t bvh_node_visits = 0u;
  uint64_t triangle_tests = 0u;
};

/// Offline bake request stored in a scene document.
struct SceneBakeDesc {
  std::string id;
  std::filesystem::path path;
  bool enabled = true;
  bool load_at_runtime = true;
  SceneLightmapBakeSettings lighting{};
  SceneNavigationBakeSettings navigation{};
  std::vector<std::string> static_component_ids;
  std::vector<std::filesystem::path> nav_cache_paths;
  BakedLightingComponent baked_lighting{};
};

/// Offline bake result contract for tools.
struct SceneBakeResult {
  bool success = false;
  bool cancelled = false;
  std::string diagnostic;
  std::filesystem::path output_path;
  std::string scene_fingerprint;
  nlohmann::json metadata = nlohmann::json::object();
  std::vector<SceneAssetRef> produced_assets;
  std::vector<BakedLightingComponent> baked_lighting;
  std::vector<BakedLightmapBinding> lightmap_bindings;
  std::vector<BakedNavigationBinding> navigation_bindings;
  /// Bit N in a lightmap binding's mask identifies this sorted Mixed light.
  std::vector<std::string> mixed_light_ids;
  /// Non-fatal exclusions or quality limitations reported by the light baker.
  std::vector<std::string> lighting_warnings;
  SceneLightBakeStatistics lighting_statistics{};
};

/// Stable stages reported by the synchronous bake core. The callback contract
/// is worker-thread friendly; callbacks execute on whichever thread invokes
/// `bakeScene`.
enum class SceneBakeStage : uint8_t {
  Preparing,
  StaticMetadata,
  Navigation,
  Lighting,
  Finalizing,
  Complete,
};

/// Deterministic progress snapshot for an offline scene bake.
struct SceneBakeProgress {
  SceneBakeStage stage = SceneBakeStage::Preparing;
  uint64_t current = 0u;
  uint64_t total = 0u;
  std::string message;
};

/// Optional cancellation and progress hooks for running a bake in a job.
struct SceneBakeExecutionOptions {
  /// Runtime-only stage selection; authored bake settings remain unchanged.
  bool bake_lighting = true;
  bool bake_navigation = true;
  std::function<bool()> is_cancelled;
  std::function<void(const SceneBakeProgress&)> on_progress;
};

/// Options for loading a scene source document.
struct SceneLoadDesc {
  std::filesystem::path path;
  bool require_kscene_json_extension = true;
  /// Runtime-only root used to resolve portable paths in the loaded document.
  /// Empty preserves the scene document directory as the default root.
  std::filesystem::path reference_root;
};

/// Parsed scene source document.
struct SceneDocument {
  uint32_t version = kSceneDocumentVersion;
  std::string name;
  std::filesystem::path source_path;
  /// Runtime-only path root. This value is never serialized into scene JSON.
  std::filesystem::path reference_root;
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

/// Result of validating an in-memory scene document.
struct SceneValidationResult {
  std::vector<std::string> diagnostics;

  bool success() const { return diagnostics.empty(); }
  explicit operator bool() const { return success(); }
};

/// Validates ids, references, hierarchy, and finite runtime values.
///
/// Callers that construct `SceneDocument` directly receive the same structural
/// guarantees as documents loaded from JSON.
SceneValidationResult validateSceneDocument(const SceneDocument& document);

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

/// Canonically serializes a validated scene document contract to JSON.
///
/// Runtime-only fields such as `source_path` and `reference_root` are omitted.
nlohmann::json sceneDocumentToJson(const SceneDocument& document);

/// Options for atomically saving a scene source document.
struct SceneSaveDesc {
  std::filesystem::path path;
  bool require_kscene_json_extension = true;
};

/// Result of scene document validation and atomic persistence.
struct SceneSaveResult {
  std::filesystem::path path;
  std::vector<std::string> diagnostics;

  bool success() const { return diagnostics.empty(); }
  explicit operator bool() const { return success(); }
};

/// Validates and atomically saves a canonical `*.kscene.json` document.
SceneSaveResult saveSceneDocument(const SceneDocument& document,
                                  const SceneSaveDesc& desc);

/// Validates and atomically saves a canonical `*.kscene.json` document.
SceneSaveResult saveSceneDocument(const SceneDocument& document,
                                  const std::filesystem::path& path);

/// Runtime options for instantiating a parsed scene document.
struct SceneInstantiateDesc {
  bool instantiate_gltf_scenes = true;
  bool instantiate_prefabs = true;
  bool instantiate_authored_entities = true;
  bool attach_authored_components = true;
  bool create_synthetic_gltf_roots = false;
  bool autoplay_gltf_animations = false;
  /// Runtime-only root override for all portable paths in this instance.
  /// Precedence is this value, then `SceneDocument::reference_root`, then the
  /// scene document directory.
  std::filesystem::path reference_root;
};

/// Runtime scene instance and stable authored-id mappings.
struct SceneInstantiateResult {
  SceneInstantiateResult() = default;
  ~SceneInstantiateResult() = default;
  SceneInstantiateResult(const SceneInstantiateResult&) = delete;
  SceneInstantiateResult& operator=(const SceneInstantiateResult&) = delete;
  SceneInstantiateResult(SceneInstantiateResult&& other) noexcept;
  /// Moving over a result that still owns runtime resources is rejected.
  /// Call `destroyScene` before reusing an instance result.
  SceneInstantiateResult& operator=(SceneInstantiateResult&& other);

  bool success = false;
  std::vector<std::string> diagnostics;

  /// Stable identity of the World that owns every entity handle below.
  uint64_t world_instance_id = 0u;
  /// Stable identity of the Scene graph that owns every hierarchy node.
  uint64_t scene_instance_id = 0u;
  assets::AssetRegistry* asset_registry = nullptr;
  std::vector<assets::AssetPackageHandle> asset_packages;
  std::vector<assets::AssetPackageHandle> prefab_asset_packages;
  /// Runtime lightmap resources owned by this instantiated scene.
  std::vector<std::string> generated_mesh_asset_keys;
  std::vector<std::string> generated_texture_asset_keys;
  std::vector<std::string> generated_material_asset_keys;

  std::vector<world::Entity> entities;
  std::vector<world::Entity> prefab_roots;
  std::unordered_map<std::string, world::Entity> entities_by_id;
  std::unordered_map<std::string, world::Entity> gltf_scene_roots_by_id;
  std::unordered_map<std::string, std::vector<world::Entity>> gltf_scene_entities_by_id;
  std::unordered_map<std::string, world::Entity> prefab_roots_by_id;
  std::unordered_map<std::string, world::Entity> cameras_by_id;
  std::unordered_map<std::string, world::Entity> lights_by_id;
  /// Stable owners used by baked navigation bindings. Keys are
  /// `entity:<scene-id>` or `prefab:<instance-id>/node:<saved-node-id>`.
  std::unordered_map<std::string, world::Entity> navigation_owners_by_id;

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

/// Computes the source/dependency fingerprint used by Scene Bake V2 without
/// instantiating the scene or publishing artifacts.
std::string sceneBakeFingerprint(const SceneDocument& document,
                                 const SceneBakeDesc& desc);

/// Builds deterministic V2 scene bake metadata for offline tools.
SceneBakeResult bakeScene(const SceneDocument& document,
                          const SceneBakeDesc& desc = {},
                          const SceneBakeExecutionOptions& execution = {});

/// Destroys entities and releases scene-owned asset packages from a scene instance.
bool destroyScene(world::World& world,
                  world::Scene& scene,
                  SceneInstantiateResult& result);

}  // namespace karma::scenes
