#pragma once

#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "karma/content/assets/asset_cache.h"
#include "karma/content/assets/asset_registry.h"

namespace karma::content {

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

}  // namespace karma::content
