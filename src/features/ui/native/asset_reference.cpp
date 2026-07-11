#include "features/ui/native/asset_reference.h"

#include "karma/assets.h"

namespace karma::ui::native {

bool isSafeAssetReference(std::string_view value) {
  return assets::AssetRegistry::isValidAssetKey(value) &&
         value.find("://") == value.npos && value.find('\\') == value.npos &&
         !value.starts_with("file:") && !value.starts_with("data:");
}

}  // namespace karma::ui::native
