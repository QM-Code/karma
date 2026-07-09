#pragma once

#include "karma/core.h"
#include "karma/math.h"
#include "karma/world.h"
#include "karma/rendering.h"
#include "karma/navigation.h"
#include "karma/components.h"



#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace karma::assets {

/// \ingroup karma_content
/// Loaded RGBA8 image payload.
struct Rgba8Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;

  /// Returns true when dimensions and pixel count are consistent.
  bool valid() const {
    return width > 0 && height > 0 &&
           pixels.size() == static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height) * 4u;
  }
};

/// Options for loading RGBA8 image data.
struct Rgba8ImageLoadOptions {
  bool flip_y = false;
};

/// Scalar image source format for height/data maps.
enum class ScalarImageFormat : uint8_t {
  Auto = 0,
  ImageFile = 1,
  Raw16Unsigned = 2,
  R32Float = 3,
};

/// Options for loading a scalar height/data map.
struct ScalarImageLoadOptions {
  ScalarImageFormat format = ScalarImageFormat::Auto;
  uint32_t raw_width = 0u;
  uint32_t raw_height = 0u;
  bool little_endian = true;
  bool flip_y = false;
  float value_min = 0.0f;
  float value_max = 1.0f;
};

/// Loaded normalized scalar image payload.
struct ScalarImage {
  int width = 0;
  int height = 0;
  std::vector<float> values;

  /// Returns true when dimensions and sample count are consistent.
  bool valid() const {
    return width > 0 && height > 0 &&
           values.size() == static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height);
  }
};

/// Loads an image file into RGBA8 pixels when supported.
std::optional<Rgba8Image> loadRgba8Image(
    const std::filesystem::path& path,
    const Rgba8ImageLoadOptions& options = {});

/// Loads encoded image bytes into RGBA8 pixels when supported.
std::optional<Rgba8Image> loadRgba8ImageFromMemory(const std::uint8_t* data,
                                                   std::size_t size,
                                                   const Rgba8ImageLoadOptions& options = {});

/// Loads an image, RAW16, or R32 file into normalized scalar samples.
std::optional<ScalarImage> loadScalarImage(
    const std::filesystem::path& path,
    const ScalarImageLoadOptions& options = {});

}  // namespace karma::assets


#include <filesystem>
#include <string>
#include <vector>


namespace karma::visual::particles {

/// \ingroup karma_particles
/// Canonical v3 emitter authoring record parsed from `.kpeffect` JSON.
struct ParticleEmitterDesc {
  components::ParticleEmitterComponent emitter{};
  std::string texture_key;
};

/// \ingroup karma_particles
/// Canonical v3 particle effect asset.
struct ParticleEffectAsset {
  std::vector<ParticleEmitterDesc> emitters;

  const ParticleEmitterDesc* primaryEmitter() const {
    return emitters.empty() ? nullptr : &emitters.front();
  }
};

using ParticleEffectDesc = ParticleEffectAsset;

/// Parses a `.kpeffect` JSON file into a particle effect asset.
bool loadParticleEffectAsset(const std::filesystem::path& path,
                             ParticleEffectAsset& out_asset);

}  // namespace karma::visual::particles


#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>


namespace karma::assets {

/// CPU-side texture payload ready for renderer upload.
struct TextureAsset {
  enum class PayloadFormat : uint32_t {
    RGBA8 = 0u,
    KTX2_BASIS_UASTC = 1u,
    PreparedUpload = 2u,
  };

  enum class Semantic : uint32_t {
    Color = 0u,
    Linear = 1u,
    Normal = 2u,
    Data = 3u,
  };

  rendering::TextureDesc desc{};
  PayloadFormat payload_format = PayloadFormat::RGBA8;
  Semantic semantic = Semantic::Color;
  std::vector<rendering::TextureUploadSubresource> subresources;
  std::vector<uint8_t> bytes;
  std::vector<uint8_t> fallback_rgba8;
  std::string content_hash;
};

/// Texture format choices available on the active renderer backend.
struct TextureRuntimeCapabilities {
  bool bc7_unorm = false;
  bool bc7_srgb = false;
};

/// Final texture descriptor and byte payload selected for renderer upload.
struct PreparedTextureUpload {
  rendering::TextureDesc desc{};
  rendering::TextureUploadData upload{};

  bool valid() const {
    return desc.width > 0 && desc.height > 0 && !upload.bytes.empty();
  }
};

/// Converts a cached/imported texture payload into the best backend upload.
std::optional<PreparedTextureUpload> prepareTextureUpload(
    const TextureAsset& texture,
    TextureRuntimeCapabilities capabilities = {});

/// Stable asset-cache key for a backend-ready texture upload derived from a
/// source texture and runtime format capabilities.
std::string preparedTextureUploadCacheKey(
    const TextureAsset& texture,
    TextureRuntimeCapabilities capabilities = {});

/// Options used when importing an image file into a CPU texture asset.
struct TextureImportOptions {
  bool srgb = false;
  bool generate_mips = true;
  bool alpha_bleed = false;
  float alpha_coverage_cutoff = 0.5f;
  TextureAsset::Semantic semantic = TextureAsset::Semantic::Color;
  bool prefer_compressed = true;
};

/// Runtime audio clip registration. `path` is an import source resolved at
/// registration time; runtime components reference only `key`.
struct AudioClipAsset {
  std::filesystem::path path;
  int max_instances = 5;
};

/// Runtime environment map registration. Components reference `key`; renderer
/// backends still receive the resolved source path while environment import
/// caching remains future work.
struct EnvironmentMapAsset {
  std::filesystem::path path;
};

struct GltfSceneAssetPrimitive {
  std::string name;
  std::string mesh_key;
  std::string material_key;
  bool casts_shadows = true;
  uint32_t skin_index = world::kInvalidAnimationIndex;
  std::vector<float> morph_weights;
  std::vector<uint32_t> joint_node_indices;
  std::vector<glm::mat4> inverse_bind_matrices;
};

struct GltfSceneAssetNode {
  std::string name;
  math::Vec3 local_position{};
  math::Quat local_rotation{};
  math::Vec3 local_scale{1.0f, 1.0f, 1.0f};
  math::Vec3 world_position{};
  math::Quat world_rotation{};
  math::Vec3 world_scale{1.0f, 1.0f, 1.0f};
  bool has_light = false;
  components::LightComponent light{};
  std::vector<GltfSceneAssetPrimitive> primitives;
  std::vector<uint32_t> children;
};

/// Deterministic child keys produced by a glTF scene import.
struct GltfSceneAsset {
  std::filesystem::path source_path;
  uint32_t root_node = std::numeric_limits<uint32_t>::max();
  std::vector<GltfSceneAssetNode> nodes;
  std::vector<std::string> mesh_asset_keys;
  std::vector<std::string> texture_asset_keys;
  std::vector<std::string> material_keys;
  std::vector<std::string> animation_clip_keys;
  std::vector<std::string> skeleton_keys;
  std::vector<std::string> skin_keys;
  std::string scene_key;

  bool valid() const {
    return root_node < nodes.size();
  }
};

struct SceneAsset;

/// \ingroup karma_content
/// Explicit registry for normalized runtime assets.
///
/// Source files enter through asset packages. Runtime systems resolve
/// components through these keys and never treat component strings as file paths.
class AssetRegistry {
 public:
  AssetRegistry();
  ~AssetRegistry();
  AssetRegistry(AssetRegistry&&) noexcept;
  AssetRegistry& operator=(AssetRegistry&&) noexcept;
  AssetRegistry(const AssetRegistry&) = delete;
  AssetRegistry& operator=(const AssetRegistry&) = delete;

  void clear();
  bool moveAssetFrom(AssetRegistry& source,
                     const std::string& type,
                     const std::string& key);

  static bool isValidAssetKey(std::string_view key);
  static std::string assetKeyValidationError(std::string_view key);

  bool registerMeshAsset(const std::string& key, world::MeshData mesh);
  bool unregisterMeshAsset(const std::string& key);
  const world::MeshData* findMeshAsset(std::string_view key) const;

  bool registerTextureAsset(const std::string& key, TextureAsset texture);
  bool unregisterTextureAsset(const std::string& key);
  const TextureAsset* findTextureAsset(std::string_view key) const;
  std::vector<std::string> registerImportedMaterialTextures(
      const std::string& material_key,
      rendering::MaterialAssetDesc& material);

  bool registerMaterialAsset(const std::string& key, rendering::MaterialAssetDesc material);
  bool registerMaterialAsset(const std::string& key, rendering::MaterialDesc surface);
  bool registerMaterialVariant(const std::string& key, rendering::MaterialVariantDesc material);
  bool registerMaterialVariant(
      const std::string& key,
      const std::string& base_material_key,
      std::unordered_map<std::string, rendering::MaterialParameterValue> params = {},
      std::unordered_map<std::string, std::string> textures = {});
  bool unregisterMaterial(const std::string& key);
  const rendering::MaterialAssetDesc* findMaterialAsset(std::string_view key) const;
  const rendering::MaterialVariantDesc* findMaterialVariant(std::string_view key) const;
  std::optional<rendering::ResolvedMaterialDesc> resolveMaterial(std::string_view key) const;

  bool registerShaderPass(const std::string& key, rendering::ShaderPassAssetDesc pass);
  bool unregisterShaderPass(const std::string& key);
  const rendering::ShaderPassAssetDesc* findShaderPass(std::string_view key) const;

  bool registerFrameGraph(const std::string& key, rendering::FrameGraphDesc graph);
  bool unregisterFrameGraph(const std::string& key);
  const rendering::FrameGraphDesc* findFrameGraph(std::string_view key) const;
  const rendering::FrameGraphDesc& resolveFrameGraph(std::string_view key) const;

  bool registerParticleEffect(const std::string& key, visual::particles::ParticleEffectAsset effect);
  bool unregisterParticleEffect(const std::string& key);
  const visual::particles::ParticleEffectAsset* findParticleEffect(std::string_view key) const;

  bool registerAudioClip(const std::string& key, AudioClipAsset clip);
  bool unregisterAudioClip(const std::string& key);
  const AudioClipAsset* findAudioClip(std::string_view key) const;

  bool registerEnvironmentMap(const std::string& key, EnvironmentMapAsset environment);
  bool unregisterEnvironmentMap(const std::string& key);
  const EnvironmentMapAsset* findEnvironmentMap(std::string_view key) const;

  bool registerAnimationClip(const std::string& key, world::AnimationClip clip);
  bool unregisterAnimationClip(const std::string& key);
  const world::AnimationClip* findAnimationClip(std::string_view key) const;

  bool registerSkeleton(const std::string& key, world::Skeleton skeleton);
  bool unregisterSkeleton(const std::string& key);
  const world::Skeleton* findSkeleton(std::string_view key) const;

  bool registerSkin(const std::string& key, world::Skin skin);
  bool unregisterSkin(const std::string& key);
  const world::Skin* findSkin(std::string_view key) const;

  bool registerGltfSceneAsset(const std::string& key, GltfSceneAsset scene);
  bool unregisterGltfSceneAsset(const std::string& key);
  const GltfSceneAsset* findGltfSceneAsset(std::string_view key) const;

  bool registerSceneAsset(const std::string& key, SceneAsset scene);
  bool unregisterSceneAsset(const std::string& key);
  const SceneAsset* findSceneAsset(std::string_view key) const;

  uint64_t version() const;
  uint64_t meshVersion() const;
  uint64_t textureVersion() const;

 private:
  void bumpVersion();
  void bumpMeshVersion();
  void bumpTextureVersion();

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::assets


#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>


namespace karma::assets {

/// \ingroup karma_content
/// Persistent asset import cache configuration.
struct AssetCacheConfig {
  std::filesystem::path root;
  bool enabled = true;
  bool flush = false;
  bool ensure_layout = true;

  static AssetCacheConfig fromEnvironment();
};

/// \ingroup karma_content
/// User-cache backed store for serialized imported asset blobs.
class AssetCache {
 public:
  static constexpr uint32_t kSchemaVersion = 2u;
  static constexpr std::string_view kAssetCacheVersion = "karma-asset-cache-v2";

  explicit AssetCache(AssetCacheConfig config = AssetCacheConfig::fromEnvironment());

  const AssetCacheConfig& config() const { return config_; }
  const std::filesystem::path& root() const { return config_.root; }
  bool enabled() const { return config_.enabled && !config_.root.empty(); }

  void flush();

  std::string makeSourceKey(const std::filesystem::path& source,
                            std::string_view importer_version,
                            const nlohmann::json& import_options,
                            std::string_view package_manifest_hash = {},
                            std::string_view dependency_version = {}) const;

  std::optional<TextureAsset> readTexture(std::string_view cache_key,
                                          std::string* diagnostic = nullptr);
  bool writeTexture(std::string_view cache_key,
                    const TextureAsset& texture,
                    std::string* diagnostic = nullptr);
  /// Writes a texture blob without updating the best-effort cache index.
  /// Useful for derived runtime caches where the deterministic key is enough.
  bool writeTextureNoIndex(std::string_view cache_key,
                           const TextureAsset& texture,
                           std::string* diagnostic = nullptr);
  std::optional<world::MeshData> readMesh(std::string_view cache_key,
                                             std::string* diagnostic = nullptr);
  bool writeMesh(std::string_view cache_key,
                 const world::MeshData& mesh,
                 std::string* diagnostic = nullptr);
  std::optional<rendering::MaterialAssetDesc> readMaterialAsset(
      std::string_view cache_key,
      std::string* diagnostic = nullptr);
  bool writeMaterialAsset(std::string_view cache_key,
                          const rendering::MaterialAssetDesc& material,
                          std::string* diagnostic = nullptr);
  std::optional<rendering::MaterialVariantDesc> readMaterialVariant(
      std::string_view cache_key,
      std::string* diagnostic = nullptr);
  bool writeMaterialVariant(std::string_view cache_key,
                            const rendering::MaterialVariantDesc& material,
                            std::string* diagnostic = nullptr);
  std::optional<visual::particles::ParticleEffectAsset> readParticleEffect(
      std::string_view cache_key,
      std::string* diagnostic = nullptr);
  bool writeParticleEffect(std::string_view cache_key,
                           const visual::particles::ParticleEffectAsset& effect,
                           std::string* diagnostic = nullptr);
  std::optional<GltfSceneAsset> readGltfScene(std::string_view cache_key,
                                              std::string* diagnostic = nullptr);
  bool writeGltfScene(std::string_view cache_key,
                      const GltfSceneAsset& scene,
                      std::string* diagnostic = nullptr);
  std::optional<world::AnimationClip> readAnimationClip(
      std::string_view cache_key,
      std::string* diagnostic = nullptr);
  bool writeAnimationClip(std::string_view cache_key,
                          const world::AnimationClip& clip,
                          std::string* diagnostic = nullptr);
  std::optional<world::Skeleton> readSkeleton(std::string_view cache_key,
                                                  std::string* diagnostic = nullptr);
  bool writeSkeleton(std::string_view cache_key,
                     const world::Skeleton& skeleton,
                     std::string* diagnostic = nullptr);
  std::optional<world::Skin> readSkin(std::string_view cache_key,
                                          std::string* diagnostic = nullptr);
  bool writeSkin(std::string_view cache_key,
                 const world::Skin& skin,
                 std::string* diagnostic = nullptr);

  std::optional<nlohmann::json> readPackageManifest(std::string_view manifest_hash,
                                                    std::string* diagnostic = nullptr);
  bool writePackageManifest(std::string_view manifest_hash,
                            const nlohmann::json& manifest,
                            std::string* diagnostic = nullptr);

 private:
  std::filesystem::path blobPath(std::string_view cache_key) const;
  std::filesystem::path packageManifestPath(std::string_view manifest_hash) const;
  void ensureLayout();
  void touchIndex(std::string_view cache_key, std::string_view kind);

  AssetCacheConfig config_{};
};

std::string hashBytes(const std::uint8_t* data, std::size_t size);
std::string hashString(std::string_view value);
std::optional<std::string> hashFile(const std::filesystem::path& path);

}  // namespace karma::assets


#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>


namespace karma::assets {

/// One asset registered by an imported package.
struct AssetPackageLoadedAsset {
  std::string type;
  std::string key;
  std::string cache_blob_key;
};

/// Handle returned by a successful package import.
struct AssetPackageHandle {
  std::filesystem::path manifest_path;
  std::vector<AssetPackageLoadedAsset> assets;
  uint64_t instance_id = 0u;

  bool valid() const { return !manifest_path.empty(); }
};

/// Options shared by synchronous and asynchronous package loading.
struct AssetPackageOptions {
  AssetCacheConfig cache = AssetCacheConfig::fromEnvironment();
};

/// Options for writing a portable baked asset package.
struct AssetPackageBakeOptions {
  std::string package_id;
  std::string scene_fingerprint;
  AssetPackageOptions import_options{};
};

/// Background asset package import job. `commitAssetPackageJob` is the only API
/// that mutates a live registry.
class AssetPackageJob {
 public:
  AssetPackageJob();
  ~AssetPackageJob();
  AssetPackageJob(AssetPackageJob&&) noexcept;
  AssetPackageJob& operator=(AssetPackageJob&&) noexcept;
  AssetPackageJob(const AssetPackageJob&) = delete;
  AssetPackageJob& operator=(const AssetPackageJob&) = delete;

  bool valid() const;
  bool ready() const;
  void wait();
  bool success() const;
  const std::string& diagnostic() const;
  const AssetPackageHandle* handle() const;

 private:
  struct State;
  explicit AssetPackageJob(std::shared_ptr<State> state);
  friend AssetPackageJob loadAssetPackageAsync(const std::filesystem::path&,
                                               const AssetPackageOptions&);
  friend bool commitAssetPackageJob(AssetRegistry&, AssetPackageJob&, AssetPackageHandle*);
  std::shared_ptr<State> state_;
};

/// Resolves a package path. Directories resolve to `assets.package.json`.
std::filesystem::path resolveAssetPackagePath(const std::filesystem::path& path);

/// Imports an asset package all-or-nothing.
std::optional<AssetPackageHandle> importAssetPackage(AssetRegistry& assets,
                                                     const std::filesystem::path& path,
                                                     std::string* diagnostic = nullptr);
std::optional<AssetPackageHandle> importAssetPackage(AssetRegistry& assets,
                                                     const std::filesystem::path& path,
                                                     const AssetPackageOptions& options,
                                                     std::string* diagnostic = nullptr);

/// Imports a source package once and writes portable baked blobs into `output_dir`.
bool bakeAssetPackage(const std::filesystem::path& source_package_path,
                      const std::filesystem::path& output_dir,
                      const AssetPackageBakeOptions& options,
                      std::string* diagnostic = nullptr);

/// Restores a portable baked package from `baked.package.json` and local blobs.
std::optional<AssetPackageHandle> importBakedAssetPackage(
    AssetRegistry& assets,
    const std::filesystem::path& baked_cache_path,
    std::string* diagnostic = nullptr);

/// Validates that a baked package descriptor, blobs, and source fingerprint are fresh.
bool checkBakedAssetPackage(const std::filesystem::path& source_package_path,
                            const std::filesystem::path& baked_cache_path,
                            const AssetPackageBakeOptions& options,
                            std::string* diagnostic = nullptr);

/// Imports an asset package on a worker thread without mutating a live registry.
AssetPackageJob loadAssetPackageAsync(const std::filesystem::path& path,
                                      const AssetPackageOptions& options = {});

/// Commits a finished package job into `assets` all-or-nothing on the caller thread.
bool commitAssetPackageJob(AssetRegistry& assets,
                           AssetPackageJob& job,
                           AssetPackageHandle* out_handle = nullptr);

/// Unregisters assets that were imported by a package handle.
bool unloadAssetPackage(AssetRegistry& assets, const AssetPackageHandle& package);

/// Ref-counted package store for shared package lifetime.
class AssetPackageStore {
 public:
  explicit AssetPackageStore(AssetRegistry& assets,
                             AssetPackageOptions options = {});
  ~AssetPackageStore();

  std::optional<AssetPackageHandle> acquirePackage(
      const std::filesystem::path& path,
      std::string* diagnostic = nullptr);
  std::optional<AssetPackageHandle> acquireBakedPackage(
      const std::filesystem::path& baked_cache_path,
      std::string* diagnostic = nullptr);
  bool releasePackage(const AssetPackageHandle& package);
  void clear();

 private:
  struct Record {
    AssetPackageHandle handle;
    uint32_t ref_count = 0u;
  };

  std::string packageKey(const std::filesystem::path& manifest_path) const;

  AssetRegistry* assets_ = nullptr;
  AssetPackageOptions options_{};
  uint64_t next_instance_id_ = 1u;
  std::unordered_map<std::string, Record> records_;
  std::unordered_map<uint64_t, std::string> keys_by_instance_id_;
};

}  // namespace karma::assets


#include <filesystem>
#include <optional>
#include <string>


namespace karma::assets {
class AssetRegistry;
}

namespace karma::assets {

/// Result metadata for loading a JSON `.mat` file.
struct MaterialLoadResult {
  bool success = false;
  std::string diagnostic;
};

/// Parses a shared material asset from a JSON `.mat` file.
std::optional<rendering::MaterialAssetDesc> loadMaterialAssetDesc(
    const std::filesystem::path& path,
    std::string* diagnostic = nullptr);

/// Parses a material variant from a JSON `.mat` file.
std::optional<rendering::MaterialVariantDesc> loadMaterialVariantDesc(
    const std::filesystem::path& path,
    std::string* diagnostic = nullptr);

/// Loads a JSON `.mat` file and registers it in the asset registry.
MaterialLoadResult loadMaterialFile(AssetRegistry& assets,
                                    const std::string& key,
                                    const std::filesystem::path& path);

}  // namespace karma::assets


#include <filesystem>


namespace karma::assets {

/// \ingroup karma_content
/// Loads an opaque navigation tile-cache snapshot from disk.
navigation::NavTileCacheSnapshot loadNavTileCacheSnapshot(const std::filesystem::path& path);

/// \ingroup karma_content
/// Saves an opaque navigation tile-cache snapshot to disk.
bool saveNavTileCacheSnapshot(const std::filesystem::path& path,
                              const navigation::NavTileCacheSnapshot& snapshot);

}  // namespace karma::assets


#include <cstdint>
#include <vector>


namespace karma::assets {
class AssetRegistry;
struct GltfSceneAsset;
}  // namespace karma::assets

namespace karma::world {
class World;
}  // namespace karma::world

namespace karma::world {
class Scene;

/// \ingroup karma_content
/// Sentinel node id for imported glTF scene data.
constexpr uint32_t kInvalidGltfSceneNode = Node::kInvalidId;

/// \ingroup karma_content
/// Controls how a registered glTF scene asset is instantiated into ECS/scene data.
struct GltfSceneInstantiateOptions {
  bool create_synthetic_root = false;
  bool autoplay_animations = true;
};

/// \ingroup karma_content
/// ECS/scene entities created from a registered glTF scene asset.
struct GltfSceneImportResult {
  world::Entity root_entity{};
  world::NodeId root_node = world::Node::kInvalidId;
  std::vector<world::Entity> entities;
  std::vector<world::Entity> node_entities_by_index;
  /// Renderable morph primitive entities keyed by imported glTF node index.
  std::vector<std::vector<world::Entity>> morph_entities_by_node_index;

  /// Returns true when a root ECS entity and scene node were created.
  bool valid() const {
    return root_entity.isValid() && root_node != world::Node::kInvalidId;
  }
};

/// Instantiates cached/registered glTF scene metadata into world and scene data.
GltfSceneImportResult instantiateGltfSceneAsset(
    world::World& world,
    world::Scene& scene,
    assets::AssetRegistry& assets,
    const assets::GltfSceneAsset& asset,
    const GltfSceneInstantiateOptions& options = {});

}  // namespace karma::world

#include "karma/scenes.h"
