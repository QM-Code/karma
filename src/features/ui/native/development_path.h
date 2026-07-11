#pragma once

#include <filesystem>
#include <optional>

namespace karma::ui::native {

/// Checks the portable lexical subset accepted by loose UI references.
/// Backslashes, root/drive prefixes, and colons (including URI schemes) are
/// rejected before any filesystem access.
bool isPortableDevelopmentPath(const std::filesystem::path& requested);

/// Canonicalizes a portable relative path and enforces the configured root
/// sandbox.
std::optional<std::filesystem::path> resolveDevelopmentPath(
    const std::filesystem::path& requested,
    const std::filesystem::path& referring_directory,
    const std::filesystem::path& root);

}  // namespace karma::ui::native
