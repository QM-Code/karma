#include "features/ui/native/font_face.h"

#include "features/ui/native/string_utils.h"

#include "karma/assets.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <tuple>
#include <unordered_map>

namespace karma::ui::native {
namespace {

using string_utils::lower;
using string_utils::trim;
using string_utils::unquote;

std::string withoutComments(std::string_view source) {
  std::string cleaned(source);
  for (std::size_t begin = cleaned.find("/*"); begin != std::string::npos;
       begin = cleaned.find("/*", begin)) {
    const std::size_t end = cleaned.find("*/", begin + 2u);
    cleaned.erase(begin, end == std::string::npos ? cleaned.size() - begin
                                                  : end + 2u - begin);
  }
  return cleaned;
}

std::unordered_map<std::string, std::string> parseDeclarations(
    std::string_view source) {
  std::unordered_map<std::string, std::string> output;
  std::size_t begin = 0u;
  int parentheses = 0;
  char quote = '\0';
  for (std::size_t cursor = 0u; cursor <= source.size(); ++cursor) {
    if (cursor < source.size()) {
      const char value = source[cursor];
      if (quote != '\0') {
        if (value == quote && (cursor == 0u || source[cursor - 1u] != '\\')) {
          quote = '\0';
        }
      } else if (value == '"' || value == '\'') {
        quote = value;
      } else if (value == '(') {
        ++parentheses;
      } else if (value == ')') {
        parentheses = std::max(0, parentheses - 1);
      }
    }
    if (cursor != source.size() &&
        (source[cursor] != ';' || parentheses != 0 || quote != '\0')) {
      continue;
    }
    const std::string_view declaration = source.substr(begin, cursor - begin);
    begin = cursor + 1u;
    const std::size_t colon = declaration.find(':');
    if (colon == std::string_view::npos) continue;
    std::string name = lower(trim(declaration.substr(0u, colon)));
    std::string value = trim(declaration.substr(colon + 1u));
    if (!name.empty() && !value.empty()) {
      output[std::move(name)] = std::move(value);
    }
  }
  return output;
}

bool parseUnsigned(std::string_view source, std::uint32_t& output) {
  const std::string value = trim(source);
  if (value.empty()) return false;
  std::uint64_t parsed = 0u;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
      parsed > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  output = static_cast<std::uint32_t>(parsed);
  return true;
}

int styleRank(FontFaceStyle candidate, FontFaceStyle requested) {
  if (candidate == requested) return 0;
  if (requested == FontFaceStyle::Normal) {
    return candidate == FontFaceStyle::Oblique ? 1 : 2;
  }
  return candidate == FontFaceStyle::Normal ? 2 : 1;
}

// CSS Fonts chooses weights in a directional order around 400/500 instead of
// using absolute distance alone. Return that ordered rank as a sortable pair.
std::pair<int, int> weightRank(int candidate, int requested) {
  candidate = std::clamp(candidate, 1, 1000);
  requested = std::clamp(requested, 1, 1000);
  if (requested < 400) {
    return candidate <= requested
               ? std::pair{0, requested - candidate}
               : std::pair{1, candidate - requested};
  }
  if (requested > 500) {
    return candidate >= requested
               ? std::pair{0, candidate - requested}
               : std::pair{1, requested - candidate};
  }
  if (candidate >= requested && candidate <= 500) {
    return {0, candidate - requested};
  }
  if (candidate < requested) {
    return {1, requested - candidate};
  }
  return {2, candidate - 500};
}

}  // namespace

int parseFontWeight(std::string_view value, int fallback) {
  const std::string normalized = lower(trim(value));
  if (normalized == "normal") return 400;
  if (normalized == "bold") return 700;
  int parsed = 0;
  const auto result =
      std::from_chars(normalized.data(), normalized.data() + normalized.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != normalized.data() + normalized.size()) {
    return std::clamp(fallback, 1, 1000);
  }
  return std::clamp(parsed, 1, 1000);
}

FontFaceStyle parseFontFaceStyle(std::string_view value, FontFaceStyle fallback) {
  const std::string normalized = lower(trim(value));
  if (normalized == "normal") return FontFaceStyle::Normal;
  if (normalized == "italic") return FontFaceStyle::Italic;
  if (normalized == "oblique" || normalized.starts_with("oblique ")) {
    return FontFaceStyle::Oblique;
  }
  return fallback;
}

std::vector<FontFaceDefinition> parseFontFaceDefinitions(
    std::string_view source,
    std::size_t source_order_begin) {
  const std::string cleaned = withoutComments(source);
  const std::string normalized = lower(cleaned);
  std::vector<FontFaceDefinition> faces;
  std::size_t cursor = 0u;
  while ((cursor = normalized.find("@font-face", cursor)) != std::string::npos) {
    const std::size_t open = cleaned.find('{', cursor + 10u);
    if (open == std::string::npos) break;
    int nesting = 1;
    std::size_t close = open + 1u;
    for (; close < cleaned.size() && nesting > 0; ++close) {
      if (cleaned[close] == '{') ++nesting;
      if (cleaned[close] == '}') --nesting;
    }
    if (nesting != 0) break;
    const auto declarations = parseDeclarations(
        std::string_view(cleaned).substr(open + 1u, close - open - 2u));
    const auto family = declarations.find("font-family");
    const auto source_value = declarations.find("src");
    if (family != declarations.end() && source_value != declarations.end()) {
      const std::string lowered_source = lower(source_value->second);
      const std::size_t function = lowered_source.find("font(");
      if (function != std::string::npos) {
        const std::size_t argument_begin = function + 5u;
        const std::size_t argument_end =
            source_value->second.find(')', argument_begin);
        if (argument_end != std::string::npos) {
          std::string key = unquote(source_value->second.substr(
              argument_begin, argument_end - argument_begin));
          std::string family_name = lower(unquote(family->second));
          std::uint32_t face_index = 0u;
          const auto index = declarations.find("font-face-index");
          const bool valid_index = index == declarations.end() ||
                                   parseUnsigned(index->second, face_index);
          if (!family_name.empty() && assets::AssetRegistry::isValidAssetKey(key) &&
              valid_index) {
            const auto weight = declarations.find("font-weight");
            const auto style = declarations.find("font-style");
            faces.push_back({
                .family = std::move(family_name),
                .asset_key = std::move(key),
                .weight = weight == declarations.end()
                              ? 400
                              : parseFontWeight(weight->second),
                .style = style == declarations.end()
                             ? FontFaceStyle::Normal
                             : parseFontFaceStyle(style->second),
                .face_index = face_index,
                .source_order = source_order_begin + faces.size(),
            });
          }
        }
      }
    }
    cursor = close;
  }
  return faces;
}

const FontFaceDefinition* selectBestFontFace(
    const std::vector<FontFaceDefinition>& faces,
    std::string_view family,
    int weight,
    FontFaceStyle style) {
  const std::string normalized_family = lower(trim(family));
  const FontFaceDefinition* best = nullptr;
  std::tuple<int, int, int, std::size_t> best_rank{};
  for (const FontFaceDefinition& face : faces) {
    if (face.family != normalized_family) continue;
    const auto weight_rank = weightRank(face.weight, weight);
    const auto rank = std::tuple{styleRank(face.style, style), weight_rank.first,
                                 weight_rank.second, face.source_order};
    if (best == nullptr || rank < best_rank) {
      best = &face;
      best_rank = rank;
    }
  }
  return best;
}

std::string fontRegistrationKey(std::string_view asset_key,
                                std::uint32_t face_index) {
  std::string key(asset_key);
  key += "#face=";
  key += std::to_string(face_index);
  return key;
}

}  // namespace karma::ui::native
