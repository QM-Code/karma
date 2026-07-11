#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace karma::ui::native {

enum class FontFaceStyle : std::uint8_t {
  Normal,
  Italic,
  Oblique,
};

/// Parsed descriptors from one KSS @font-face rule.
struct FontFaceDefinition {
  std::string family;
  std::string asset_key;
  int weight = 400;
  FontFaceStyle style = FontFaceStyle::Normal;
  std::uint32_t face_index = 0u;
  std::size_t source_order = 0u;
};

/// Extracts supported @font-face descriptors. The optional collection face is
/// selected with `font-face-index`; omitted descriptors use the regular face.
std::vector<FontFaceDefinition> parseFontFaceDefinitions(
    std::string_view source,
    std::size_t source_order_begin = 0u);

int parseFontWeight(std::string_view value, int fallback = 400);
FontFaceStyle parseFontFaceStyle(
    std::string_view value,
    FontFaceStyle fallback = FontFaceStyle::Normal);

/// Finds the closest style/weight variant for a normalized family name.
const FontFaceDefinition* selectBestFontFace(
    const std::vector<FontFaceDefinition>& faces,
    std::string_view family,
    int weight,
    FontFaceStyle style);

/// TextEngine keys distinguish faces in a TTC/OTC while retaining the package
/// asset key separately for registry lookup and hot-reload hashing.
std::string fontRegistrationKey(std::string_view asset_key,
                                std::uint32_t face_index);

}  // namespace karma::ui::native
