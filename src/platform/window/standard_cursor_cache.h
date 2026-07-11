#pragma once

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

namespace karma::platform::detail {

// Window backends own the platform cursor lifetime, while this helper keeps
// repeated shape requests from allocating another native cursor object.
template <typename Key, typename Handle>
class StandardCursorCache {
 public:
  StandardCursorCache() = default;
  StandardCursorCache(const StandardCursorCache&) = delete;
  StandardCursorCache& operator=(const StandardCursorCache&) = delete;

  template <typename Factory>
  Handle getOrCreate(const Key& key, Factory&& factory) {
    if (const auto cached = cursors_.find(key); cached != cursors_.end()) {
      return cached->second;
    }

    Handle cursor = std::invoke(std::forward<Factory>(factory), key);
    if (!cursor) {
      return {};
    }
    cursors_.emplace(key, cursor);
    return cursor;
  }

  template <typename Destroyer>
  void clear(Destroyer&& destroyer) {
    for (auto& [key, cursor] : cursors_) {
      (void)key;
      if (cursor) {
        std::invoke(destroyer, cursor);
      }
    }
    cursors_.clear();
  }

  [[nodiscard]] std::size_t size() const { return cursors_.size(); }

 private:
  std::unordered_map<Key, Handle> cursors_;
};

}  // namespace karma::platform::detail
