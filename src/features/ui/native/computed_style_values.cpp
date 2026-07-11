#include "features/ui/native/computed_style_values.h"

#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/string_utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>

namespace karma::ui::native::computed_style_values {

using string_utils::lower;
using string_utils::parseFiniteDouble;
using string_utils::trim;

std::vector<std::string> splitCommaList(std::string_view input) {
  std::vector<std::string> output;
  int parentheses = 0;
  std::size_t begin = 0u;
  for (std::size_t index = 0u; index < input.size(); ++index) {
    if (input[index] == '(') {
      ++parentheses;
    } else if (input[index] == ')') {
      parentheses = std::max(0, parentheses - 1);
    } else if (input[index] == ',' && parentheses == 0) {
      output.push_back(trim(input.substr(begin, index - begin)));
      begin = index + 1u;
    }
  }
  output.push_back(trim(input.substr(begin)));
  output.erase(std::remove_if(output.begin(), output.end(),
                              [](const std::string& item) {
                                return item.empty();
                              }),
               output.end());
  return output;
}

std::vector<std::string> splitWhitespace(std::string_view input) {
  std::vector<std::string> output;
  std::size_t cursor = 0u;
  while (cursor < input.size()) {
    while (cursor < input.size() &&
           std::isspace(static_cast<unsigned char>(input[cursor])) != 0) {
      ++cursor;
    }
    const std::size_t begin = cursor;
    while (cursor < input.size() &&
           std::isspace(static_cast<unsigned char>(input[cursor])) == 0) {
      ++cursor;
    }
    if (cursor > begin) output.emplace_back(input.substr(begin, cursor - begin));
  }
  return output;
}

Length parseLength(std::string_view text) {
  const std::string value = lower(trim(text));
  if (value.empty() || value == "auto") return {};

  static constexpr std::array<std::pair<std::string_view, Unit>, 7u>
      suffixes = {{{"px", Unit::Px},
                   {"%", Unit::Percent},
                   {"vw", Unit::Vw},
                   {"vh", Unit::Vh},
                   {"em", Unit::Em},
                   {"rem", Unit::Rem},
                   {"fr", Unit::Fr}}};
  for (const auto& [suffix, unit] : suffixes) {
    if (value.size() <= suffix.size() ||
        value.substr(value.size() - suffix.size()) != suffix) {
      continue;
    }
    const auto number = parseFiniteDouble(std::string_view(value).substr(
        0u, value.size() - suffix.size()));
    return number.has_value()
               ? Length{.value = static_cast<float>(*number), .unit = unit}
               : Length{};
  }
  const auto number = parseFiniteDouble(value);
  return number.has_value()
             ? Length{.value = static_cast<float>(*number), .unit = Unit::Px}
             : Length{};
}

float resolveLength(const Length& length,
                    float reference,
                    float viewport_width,
                    float viewport_height,
                    float font_size,
                    float root_font_size,
                    float auto_value) {
  switch (length.unit) {
    case Unit::Auto: return auto_value;
    case Unit::Px: return length.value;
    case Unit::Percent: return reference * length.value * 0.01f;
    case Unit::Vw: return viewport_width * length.value * 0.01f;
    case Unit::Vh: return viewport_height * length.value * 0.01f;
    case Unit::Em: return font_size * length.value;
    case Unit::Rem: return root_font_size * length.value;
    case Unit::Fr: return auto_value;
  }
  return auto_value;
}

std::optional<math::Color> parseColor(std::string_view text) {
  const std::string value = lower(trim(text));
  if (value.empty() || value == "transparent") {
    return value == "transparent"
               ? std::optional<math::Color>{math::Color{0, 0, 0, 0}}
               : std::nullopt;
  }
  static const std::unordered_map<std::string, math::Color> names = {
      {"black", {0, 0, 0, 1}},   {"white", {1, 1, 1, 1}},
      {"red", {1, 0, 0, 1}},     {"green", {0, 0.5f, 0, 1}},
      {"blue", {0, 0, 1, 1}},    {"gray", {0.5f, 0.5f, 0.5f, 1}},
      {"grey", {0.5f, 0.5f, 0.5f, 1}},
      {"yellow", {1, 1, 0, 1}},  {"magenta", {1, 0, 1, 1}},
      {"cyan", {0, 1, 1, 1}},
  };
  if (const auto found = names.find(value); found != names.end()) {
    return found->second;
  }
  if (value.front() == '#') {
    const std::string_view hex(value.data() + 1u, value.size() - 1u);
    const auto nibble = [](char character) -> int {
      if (character >= '0' && character <= '9') return character - '0';
      if (character >= 'a' && character <= 'f') return character - 'a' + 10;
      return -1;
    };
    const auto byte = [&](std::size_t offset) -> int {
      const int high = nibble(hex[offset]);
      const int low = nibble(hex[offset + 1u]);
      return high < 0 || low < 0 ? -1 : high * 16 + low;
    };
    if (hex.size() == 3u || hex.size() == 4u) {
      const int red = nibble(hex[0u]);
      const int green = nibble(hex[1u]);
      const int blue = nibble(hex[2u]);
      const int alpha = hex.size() == 4u ? nibble(hex[3u]) : 15;
      if (red >= 0 && green >= 0 && blue >= 0 && alpha >= 0) {
        return math::Color{red / 15.0f, green / 15.0f, blue / 15.0f,
                           alpha / 15.0f};
      }
    }
    if (hex.size() == 6u || hex.size() == 8u) {
      const int red = byte(0u);
      const int green = byte(2u);
      const int blue = byte(4u);
      const int alpha = hex.size() == 8u ? byte(6u) : 255;
      if (red >= 0 && green >= 0 && blue >= 0 && alpha >= 0) {
        return math::Color{red / 255.0f, green / 255.0f, blue / 255.0f,
                           alpha / 255.0f};
      }
    }
    return std::nullopt;
  }

  const bool rgba = value.starts_with("rgba(");
  if (!rgba && !value.starts_with("rgb(")) return std::nullopt;
  const std::size_t begin = value.find('(') + 1u;
  const std::size_t end = value.rfind(')');
  if (end == std::string::npos || end <= begin) return std::nullopt;
  const auto parts =
      splitCommaList(std::string_view(value).substr(begin, end - begin));
  if (parts.size() != (rgba ? 4u : 3u)) return std::nullopt;
  std::array<double, 4u> channels{0.0, 0.0, 0.0, 1.0};
  for (std::size_t index = 0u; index < parts.size(); ++index) {
    const auto channel = parseFiniteDouble(parts[index]);
    if (!channel.has_value()) return std::nullopt;
    channels[index] = *channel;
  }
  const double scale =
      channels[0u] > 1.0 || channels[1u] > 1.0 || channels[2u] > 1.0
          ? 255.0
          : 1.0;
  return math::Color{
      static_cast<float>(std::clamp(channels[0u] / scale, 0.0, 1.0)),
      static_cast<float>(std::clamp(channels[1u] / scale, 0.0, 1.0)),
      static_cast<float>(std::clamp(channels[2u] / scale, 0.0, 1.0)),
      static_cast<float>(std::clamp(channels[3u], 0.0, 1.0))};
}

Length styleLength(const runtime_dom::Node& node, std::string_view property) {
  const auto found = node.style.find(std::string(property));
  return found == node.style.end() ? Length{} : parseLength(found->second);
}

float oneBoxValue(const runtime_dom::Node& node,
                  std::string_view base,
                  std::string_view side,
                  float reference,
                  float viewport_width,
                  float viewport_height,
                  float font_size) {
  const std::string side_name =
      base == "border-width"
          ? "border-" + std::string(side) + "-width"
          : std::string(base) + "-" + std::string(side);
  auto found = node.style.find(side_name);
  if (found == node.style.end()) found = node.style.find(std::string(base));
  if (found == node.style.end()) return 0.0f;
  const auto values = splitWhitespace(found->second);
  std::string selected;
  if (found->first == side_name || values.size() <= 1u) {
    selected = found->second;
  } else {
    std::size_t index = 0u;
    if (values.size() == 2u) {
      index = side == "left" || side == "right" ? 1u : 0u;
    }
    if (values.size() == 3u) {
      if (side == "top") index = 0u;
      else if (side == "bottom") index = 2u;
      else index = 1u;
    }
    if (values.size() >= 4u) {
      if (side == "top") index = 0u;
      else if (side == "right") index = 1u;
      else if (side == "bottom") index = 2u;
      else index = 3u;
    }
    selected = values[index];
  }
  return resolveLength(parseLength(selected), reference, viewport_width,
                       viewport_height, font_size, kDefaultFontSize, 0.0f);
}

float nodeFontSize(const runtime_dom::Node& node) {
  return std::max(
      1.0f,
      resolveLength(styleLength(node, "font-size"), kDefaultFontSize, 1.0f,
                    1.0f, kDefaultFontSize, kDefaultFontSize,
                    kDefaultFontSize));
}

float nodeLineHeight(const runtime_dom::Node& node, float font_size) {
  const auto found = node.style.find("line-height");
  if (found == node.style.end()) return font_size * kDefaultLineHeight;
  if (const auto unitless = parseFiniteDouble(found->second)) {
    return font_size * static_cast<float>(*unitless);
  }
  return resolveLength(parseLength(found->second), font_size, 1.0f, 1.0f,
                       font_size, kDefaultFontSize,
                       font_size * kDefaultLineHeight);
}

}  // namespace karma::ui::native::computed_style_values
