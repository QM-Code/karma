#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "karma/rendering/renderer/post_process.h"

namespace karma::renderer {

/// \ingroup karma_rendering
/// Conventional key for the engine default post-process profile.
inline constexpr std::string_view kDefaultPostProcessProfileKey = "default";

/// \ingroup karma_rendering
/// Keyed registry of post-process profiles selected by cameras.
///
/// An empty camera profile key resolves to the default profile. Missing named
/// profiles also resolve to the default profile so camera authoring mistakes do
/// not disable rendering.
///
/// \runtime `EngineApp` owns the registry and exposes borrowed pointers through
/// `GameInterface` and `RuntimeModuleContext`. Register profiles during
/// startup, or update them at runtime when camera looks need to change.
class PostProcessProfileLibrary {
 public:
  /// Replaces the default profile.
  void setDefaultProfile(PostProcessSettings settings) {
    default_profile_ = settings;
    version_ += 1;
  }

  /// Returns the current default profile.
  const PostProcessSettings& defaultProfile() const {
    return default_profile_;
  }

  /// Registers or replaces a named profile.
  ///
  /// Empty and `kDefaultPostProcessProfileKey` update the default profile.
  void registerProfile(const std::string& key, PostProcessSettings settings) {
    if (key.empty() || key == kDefaultPostProcessProfileKey) {
      setDefaultProfile(settings);
      return;
    }
    profiles_[key] = settings;
    version_ += 1;
  }

  /// Removes a named profile. The default profile cannot be removed.
  void unregisterProfile(const std::string& key) {
    if (key.empty() || key == kDefaultPostProcessProfileKey) {
      return;
    }
    if (profiles_.erase(key) > 0) {
      version_ += 1;
    }
  }

  /// Removes all named profiles while preserving the default profile.
  void clearProfiles() {
    if (!profiles_.empty()) {
      profiles_.clear();
      version_ += 1;
    }
  }

  /// Finds a named profile. Empty and "default" return the default profile.
  const PostProcessSettings* find(std::string_view key) const {
    if (key.empty() || key == kDefaultPostProcessProfileKey) {
      return &default_profile_;
    }
    const auto it = profiles_.find(std::string(key));
    if (it == profiles_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  /// Resolves a camera profile key, falling back to the default profile.
  ///
  /// Returned references remain valid until the library is modified.
  const PostProcessSettings& resolve(std::string_view key) const {
    if (const PostProcessSettings* profile = find(key)) {
      return *profile;
    }
    return default_profile_;
  }

  /// Monotonic registry version used for cache invalidation or diagnostics.
  uint64_t version() const {
    return version_;
  }

 private:
  PostProcessSettings default_profile_{};
  std::unordered_map<std::string, PostProcessSettings> profiles_;
  uint64_t version_ = 0;
};

}  // namespace karma::renderer
