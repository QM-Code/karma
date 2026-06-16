#include "karma/simulation/navigation/nav_tile_cache.h"

#include <memory>

#include "detail/nav_tile_cache_impl.h"

namespace karma::navigation {

NavTileCache::NavTileCache()
    : impl_(std::make_unique<Impl>()) {}

NavTileCache::~NavTileCache() = default;

NavTileCache::NavTileCache(NavTileCache&&) noexcept = default;

NavTileCache& NavTileCache::operator=(NavTileCache&&) noexcept = default;

void NavTileCache::reset() {
  if (impl_ != nullptr) {
    impl_->reset();
  }
}

bool NavTileCache::isValid() const {
  return impl_ != nullptr && impl_->tile_cache != nullptr;
}

const NavTileCacheBuildResult& NavTileCache::lastBuildResult() const {
  static const NavTileCacheBuildResult empty{};
  return impl_ != nullptr ? impl_->last_result : empty;
}

}  // namespace karma::navigation
