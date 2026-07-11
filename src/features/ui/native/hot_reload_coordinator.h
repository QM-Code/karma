#pragma once

#include "features/ui/native/hot_reload_runtime.h"
#include "karma/ui.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace karma::assets {
class AssetRegistry;
}

namespace karma::ui::native::runtime_dom {
struct DocumentInstance;
}

namespace karma::ui::native {

/// Frame-thread configuration for source polling and loose-file development
/// reloads. Resource staging remains asynchronous inside HotReloadWorker.
struct HotReloadCoordinatorConfig {
  bool enabled = false;
  std::chrono::milliseconds source_poll_interval{250};
  DevelopmentUiFilesConfig development_files{};
};

/// Owns all hot-reload scheduling state and source observation. Documents are
/// borrowed only for the duration of tick(); completed immutable stages are
/// adopted by System so DOM handle ownership stays outside this service.
class HotReloadCoordinator {
 public:
  HotReloadCoordinator(assets::AssetRegistry& assets,
                       HotReloadCoordinatorConfig config = {});
  ~HotReloadCoordinator();

  HotReloadCoordinator(const HotReloadCoordinator&) = delete;
  HotReloadCoordinator& operator=(const HotReloadCoordinator&) = delete;
  HotReloadCoordinator(HotReloadCoordinator&&) = delete;
  HotReloadCoordinator& operator=(HotReloadCoordinator&&) = delete;

  /// Configured roots in normalized watcher priority order when a watcher is
  /// active, otherwise the authored roots unchanged.
  [[nodiscard]] const std::vector<std::filesystem::path>& developmentRoots()
      const noexcept;

  /// Advances polling/debounce state, snapshots due sources, and returns every
  /// completed worker stage. All open documents, including hidden documents,
  /// must be supplied.
  [[nodiscard]] std::vector<HotReloadStage> tick(
      float dt,
      std::span<runtime_dom::DocumentInstance* const> documents);

  /// Invalidates queued and completed worker work. Idempotent.
  void invalidate();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace karma::ui::native
