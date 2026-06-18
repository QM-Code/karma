#pragma once

#include <filesystem>
#include <string_view>

namespace karma::demo {

inline std::filesystem::path resolveExamplePath(const std::filesystem::path& relative) {
  if (relative.is_absolute() && std::filesystem::exists(relative)) {
    return relative;
  }

  std::filesystem::path cwd = std::filesystem::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    const std::filesystem::path candidate = cwd / relative;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    if (!cwd.has_parent_path()) {
      break;
    }
    cwd = cwd.parent_path();
  }

  return relative;
}

inline std::filesystem::path resolveExampleAssetPath(std::string_view name) {
  return resolveExamplePath(std::filesystem::path("examples") / "assets" /
                            std::filesystem::path{name});
}

inline std::filesystem::path resolveExampleShaderPath(std::string_view name) {
  return resolveExamplePath(std::filesystem::path("examples") / "assets" / "shaders" /
                            std::filesystem::path{name});
}

}  // namespace karma::demo
