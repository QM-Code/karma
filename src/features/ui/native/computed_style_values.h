#pragma once

#include <karma/math.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace karma::ui::native::runtime_dom {
struct Node;
}

namespace karma::ui::native::computed_style_values {

inline constexpr float kDefaultFontSize = 16.0f;
inline constexpr float kDefaultLineHeight = 1.2f;

enum class Unit : unsigned char { Auto, Px, Percent, Vw, Vh, Em, Rem, Fr };

struct Length {
  float value = 0.0f;
  Unit unit = Unit::Auto;
};

[[nodiscard]] std::vector<std::string> splitWhitespace(
    std::string_view input);
[[nodiscard]] std::vector<std::string> splitCommaList(
    std::string_view input);
[[nodiscard]] Length parseLength(std::string_view text);
[[nodiscard]] float resolveLength(const Length& length,
                                  float reference,
                                  float viewport_width,
                                  float viewport_height,
                                  float font_size,
                                  float root_font_size,
                                  float auto_value);
[[nodiscard]] std::optional<math::Color> parseColor(std::string_view text);

[[nodiscard]] Length styleLength(const runtime_dom::Node& node,
                                 std::string_view property);
[[nodiscard]] float oneBoxValue(const runtime_dom::Node& node,
                                std::string_view base,
                                std::string_view side,
                                float reference,
                                float viewport_width,
                                float viewport_height,
                                float font_size);
[[nodiscard]] float nodeFontSize(const runtime_dom::Node& node);
[[nodiscard]] float nodeLineHeight(const runtime_dom::Node& node,
                                   float font_size);

}  // namespace karma::ui::native::computed_style_values
