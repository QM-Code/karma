#pragma once

#include <string_view>

namespace karma::ui::native {

/// Accepts only package-relative Karma asset keys. URI schemes and platform
/// paths are deliberately excluded from authored UI references.
[[nodiscard]] bool isSafeAssetReference(std::string_view value);

}  // namespace karma::ui::native
