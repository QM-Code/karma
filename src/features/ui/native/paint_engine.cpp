#include "features/ui/native/paint_engine.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <limits>
#include <numbers>
#include <string>
#include <utility>

namespace karma::ui::paint {
namespace {

constexpr float kEpsilon = 1.0e-6f;

[[nodiscard]] std::string_view trim(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1u);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1u);
  }
  return value;
}

[[nodiscard]] std::string lowercase(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  return result;
}

void setError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

void clearError(std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
}

[[nodiscard]] bool finite(float value) { return std::isfinite(value); }

[[nodiscard]] bool finite(Rect value) {
  return finite(value.x) && finite(value.y) && finite(value.width) &&
         finite(value.height);
}

[[nodiscard]] bool drawable(Rect value) {
  return finite(value) && value.width > 0.0f && value.height > 0.0f;
}

[[nodiscard]] bool parseFloat(std::string_view source, float& output) {
  source = trim(source);
  if (source.empty()) {
    return false;
  }
  float parsed = 0.0f;
  const char* begin = source.data();
  const char* end = begin + source.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end || !finite(parsed)) {
    return false;
  }
  output = parsed;
  return true;
}

[[nodiscard]] std::vector<std::string_view> splitTopLevel(
    std::string_view source, char delimiter) {
  std::vector<std::string_view> parts;
  std::size_t start = 0u;
  int depth = 0;
  for (std::size_t index = 0u; index < source.size(); ++index) {
    if (source[index] == '(') {
      ++depth;
    } else if (source[index] == ')') {
      --depth;
    } else if (source[index] == delimiter && depth == 0) {
      parts.push_back(trim(source.substr(start, index - start)));
      start = index + 1u;
    }
  }
  parts.push_back(trim(source.substr(start)));
  return parts;
}

[[nodiscard]] std::vector<std::string_view> splitWhitespace(
    std::string_view source) {
  std::vector<std::string_view> parts;
  std::size_t index = 0u;
  while (index < source.size()) {
    while (index < source.size() &&
           std::isspace(static_cast<unsigned char>(source[index])) != 0) {
      ++index;
    }
    const std::size_t start = index;
    while (index < source.size() &&
           std::isspace(static_cast<unsigned char>(source[index])) == 0) {
      ++index;
    }
    if (start != index) {
      parts.push_back(source.substr(start, index - start));
    }
  }
  return parts;
}

[[nodiscard]] std::optional<std::string_view> functionBody(
    std::string_view source, std::string_view expected_name) {
  source = trim(source);
  const std::size_t open = source.find('(');
  if (open == std::string_view::npos || source.empty() || source.back() != ')') {
    return std::nullopt;
  }
  if (lowercase(trim(source.substr(0u, open))) != expected_name) {
    return std::nullopt;
  }
  int depth = 0;
  for (std::size_t index = open; index < source.size(); ++index) {
    if (source[index] == '(') {
      ++depth;
    } else if (source[index] == ')') {
      --depth;
      if (depth == 0 && index + 1u != source.size()) {
        return std::nullopt;
      }
      if (depth < 0) {
        return std::nullopt;
      }
    }
  }
  if (depth != 0) {
    return std::nullopt;
  }
  return source.substr(open + 1u, source.size() - open - 2u);
}

[[nodiscard]] float clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] math::Color mix(math::Color left,
                              math::Color right,
                              float amount) {
  amount = clamp01(amount);
  return {left.r + (right.r - left.r) * amount,
          left.g + (right.g - left.g) * amount,
          left.b + (right.b - left.b) * amount,
          left.a + (right.a - left.a) * amount};
}

[[nodiscard]] bool parseHexDigit(char character, unsigned int& output) {
  if (character >= '0' && character <= '9') {
    output = static_cast<unsigned int>(character - '0');
    return true;
  }
  character = static_cast<char>(
      std::tolower(static_cast<unsigned char>(character)));
  if (character >= 'a' && character <= 'f') {
    output = static_cast<unsigned int>(character - 'a' + 10);
    return true;
  }
  return false;
}

[[nodiscard]] bool parseHexByte(char high, char low, float& output) {
  unsigned int high_value = 0u;
  unsigned int low_value = 0u;
  if (!parseHexDigit(high, high_value) || !parseHexDigit(low, low_value)) {
    return false;
  }
  output = static_cast<float>((high_value << 4u) | low_value) / 255.0f;
  return true;
}

[[nodiscard]] bool parseRgbComponent(std::string_view source, float& output) {
  source = trim(source);
  const bool percent = !source.empty() && source.back() == '%';
  if (percent) {
    source.remove_suffix(1u);
  }
  float value = 0.0f;
  if (!parseFloat(source, value)) {
    return false;
  }
  output = clamp01(percent ? value / 100.0f : value / 255.0f);
  return true;
}

[[nodiscard]] bool parseAlphaComponent(std::string_view source, float& output) {
  source = trim(source);
  const bool percent = !source.empty() && source.back() == '%';
  if (percent) {
    source.remove_suffix(1u);
  }
  float value = 0.0f;
  if (!parseFloat(source, value)) {
    return false;
  }
  output = clamp01(percent ? value / 100.0f : value);
  return true;
}

[[nodiscard]] bool parseColor(std::string_view source, math::Color& output) {
  source = trim(source);
  const std::string lower = lowercase(source);
  if (lower == "transparent") {
    output = {0.0f, 0.0f, 0.0f, 0.0f};
    return true;
  }
  if (lower == "black") {
    output = {0.0f, 0.0f, 0.0f, 1.0f};
    return true;
  }
  if (lower == "white") {
    output = {1.0f, 1.0f, 1.0f, 1.0f};
    return true;
  }
  if (lower == "red") {
    output = {1.0f, 0.0f, 0.0f, 1.0f};
    return true;
  }
  if (lower == "green") {
    output = {0.0f, 0.5f, 0.0f, 1.0f};
    return true;
  }
  if (lower == "blue") {
    output = {0.0f, 0.0f, 1.0f, 1.0f};
    return true;
  }
  if (!source.empty() && source.front() == '#') {
    source.remove_prefix(1u);
    if (source.size() == 3u || source.size() == 4u) {
      std::array<float, 4u> channels{0.0f, 0.0f, 0.0f, 1.0f};
      for (std::size_t channel = 0u; channel < source.size(); ++channel) {
        unsigned int digit = 0u;
        if (!parseHexDigit(source[channel], digit)) {
          return false;
        }
        channels[channel] = static_cast<float>((digit << 4u) | digit) / 255.0f;
      }
      output = {channels[0], channels[1], channels[2], channels[3]};
      return true;
    }
    if (source.size() == 6u || source.size() == 8u) {
      std::array<float, 4u> channels{0.0f, 0.0f, 0.0f, 1.0f};
      for (std::size_t channel = 0u; channel < source.size() / 2u; ++channel) {
        if (!parseHexByte(source[channel * 2u], source[channel * 2u + 1u],
                          channels[channel])) {
          return false;
        }
      }
      output = {channels[0], channels[1], channels[2], channels[3]};
      return true;
    }
    return false;
  }

  const std::size_t open = source.find('(');
  if (open == std::string_view::npos || source.back() != ')') {
    return false;
  }
  const std::string function = lowercase(trim(source.substr(0u, open)));
  if (function != "rgb" && function != "rgba") {
    return false;
  }
  const auto components = splitTopLevel(
      source.substr(open + 1u, source.size() - open - 2u), ',');
  if (components.size() != (function == "rgb" ? 3u : 4u)) {
    return false;
  }
  math::Color parsed;
  if (!parseRgbComponent(components[0], parsed.r) ||
      !parseRgbComponent(components[1], parsed.g) ||
      !parseRgbComponent(components[2], parsed.b)) {
    return false;
  }
  parsed.a = 1.0f;
  if (components.size() == 4u && !parseAlphaComponent(components[3], parsed.a)) {
    return false;
  }
  output = parsed;
  return true;
}

[[nodiscard]] bool parseAngle(std::string_view source, float& degrees) {
  source = trim(source);
  const std::string lower = lowercase(source);
  float value = 0.0f;
  if (lower.size() > 3u && lower.ends_with("deg")) {
    if (!parseFloat(source.substr(0u, source.size() - 3u), value)) {
      return false;
    }
    degrees = value;
    return true;
  }
  if (lower.size() > 3u && lower.ends_with("rad")) {
    if (!parseFloat(source.substr(0u, source.size() - 3u), value)) {
      return false;
    }
    degrees = value * 180.0f / std::numbers::pi_v<float>;
    return finite(degrees);
  }
  if (lower.size() > 4u && lower.ends_with("turn")) {
    if (!parseFloat(source.substr(0u, source.size() - 4u), value)) {
      return false;
    }
    degrees = value * 360.0f;
    return finite(degrees);
  }
  return false;
}

[[nodiscard]] bool parseStopOffset(std::string_view source, float& output) {
  source = trim(source);
  const bool percent = !source.empty() && source.back() == '%';
  if (percent) {
    source.remove_suffix(1u);
  }
  float value = 0.0f;
  if (!parseFloat(source, value)) {
    return false;
  }
  output = percent ? value / 100.0f : value;
  return finite(output);
}

struct UnresolvedStop {
  math::Color color;
  std::optional<float> offset;
};

[[nodiscard]] bool parseGradientStop(std::string_view source,
                                     UnresolvedStop& output) {
  source = trim(source);
  int depth = 0;
  for (std::size_t cursor = source.size(); cursor > 0u; --cursor) {
    const char character = source[cursor - 1u];
    if (character == ')') {
      ++depth;
    } else if (character == '(') {
      --depth;
    } else if (depth == 0 &&
               std::isspace(static_cast<unsigned char>(character)) != 0) {
      const std::string_view color_source = trim(source.substr(0u, cursor - 1u));
      const std::string_view offset_source = trim(source.substr(cursor));
      float offset = 0.0f;
      math::Color color;
      if (!color_source.empty() && parseStopOffset(offset_source, offset) &&
          parseColor(color_source, color)) {
        output = {.color = color, .offset = offset};
        return true;
      }
    }
  }
  math::Color color;
  if (!parseColor(source, color)) {
    return false;
  }
  output = {.color = color, .offset = std::nullopt};
  return true;
}

[[nodiscard]] std::vector<GradientStop> resolveStops(
    const std::vector<UnresolvedStop>& unresolved) {
  if (unresolved.empty()) {
    return {};
  }
  std::vector<std::optional<float>> offsets;
  offsets.reserve(unresolved.size());
  for (const UnresolvedStop& stop : unresolved) {
    offsets.push_back(stop.offset);
  }
  if (!offsets.front().has_value()) {
    offsets.front() = 0.0f;
  }
  if (!offsets.back().has_value()) {
    offsets.back() = 1.0f;
  }
  std::size_t index = 1u;
  while (index + 1u < offsets.size()) {
    if (offsets[index].has_value()) {
      ++index;
      continue;
    }
    const std::size_t first_missing = index;
    while (index < offsets.size() && !offsets[index].has_value()) {
      ++index;
    }
    const float left = *offsets[first_missing - 1u];
    const float right = *offsets[index];
    const std::size_t intervals = index - first_missing + 1u;
    for (std::size_t missing = first_missing; missing < index; ++missing) {
      const float amount = static_cast<float>(missing - first_missing + 1u) /
                           static_cast<float>(intervals);
      offsets[missing] = left + (right - left) * amount;
    }
  }

  std::vector<GradientStop> resolved;
  resolved.reserve(unresolved.size() + 2u);
  float previous = 0.0f;
  for (std::size_t stop_index = 0u; stop_index < unresolved.size(); ++stop_index) {
    const float offset = std::max(previous, clamp01(*offsets[stop_index]));
    resolved.push_back({.offset = offset, .color = unresolved[stop_index].color});
    previous = offset;
  }
  if (resolved.front().offset > 0.0f) {
    resolved.insert(resolved.begin(),
                    {.offset = 0.0f, .color = resolved.front().color});
  }
  if (resolved.back().offset < 1.0f) {
    resolved.push_back({.offset = 1.0f, .color = resolved.back().color});
  }
  return resolved;
}

[[nodiscard]] std::vector<GradientStop> normalizeStops(
    const std::vector<GradientStop>& source) {
  std::vector<UnresolvedStop> unresolved;
  unresolved.reserve(source.size());
  for (const GradientStop& stop : source) {
    if (finite(stop.offset) && math::isFinite(stop.color)) {
      unresolved.push_back({.color = stop.color, .offset = stop.offset});
    }
  }
  return resolveStops(unresolved);
}

[[nodiscard]] math::Color sampleStops(const std::vector<GradientStop>& stops,
                                      float offset) {
  if (stops.empty()) {
    return {0.0f, 0.0f, 0.0f, 0.0f};
  }
  if (offset <= stops.front().offset) {
    return stops.front().color;
  }
  for (std::size_t index = 1u; index < stops.size(); ++index) {
    if (offset <= stops[index].offset) {
      const float span = stops[index].offset - stops[index - 1u].offset;
      if (span <= kEpsilon) {
        return stops[index].color;
      }
      return mix(stops[index - 1u].color, stops[index].color,
                 (offset - stops[index - 1u].offset) / span);
    }
  }
  return stops.back().color;
}

struct EllipticalRadii {
  Vec2 top_left;
  Vec2 top_right;
  Vec2 bottom_right;
  Vec2 bottom_left;
};

[[nodiscard]] float nonnegativeFinite(float value) {
  return finite(value) ? std::max(0.0f, value) : 0.0f;
}

[[nodiscard]] EllipticalRadii normalizeEllipticalRadii(
    Rect box, EllipticalRadii radii) {
  for (Vec2* radius : std::array<Vec2*, 4u>{&radii.top_left, &radii.top_right,
                                             &radii.bottom_right,
                                             &radii.bottom_left}) {
    radius->x = nonnegativeFinite(radius->x);
    radius->y = nonnegativeFinite(radius->y);
  }
  float factor = 1.0f;
  const auto reduce = [&factor](float available, float requested) {
    if (requested > available && requested > 0.0f) {
      factor = std::min(factor, available / requested);
    }
  };
  reduce(std::max(0.0f, box.width), radii.top_left.x + radii.top_right.x);
  reduce(std::max(0.0f, box.width),
         radii.bottom_left.x + radii.bottom_right.x);
  reduce(std::max(0.0f, box.height),
         radii.top_left.y + radii.bottom_left.y);
  reduce(std::max(0.0f, box.height),
         radii.top_right.y + radii.bottom_right.y);
  if (factor < 1.0f) {
    for (Vec2* radius : std::array<Vec2*, 4u>{&radii.top_left, &radii.top_right,
                                               &radii.bottom_right,
                                               &radii.bottom_left}) {
      radius->x *= factor;
      radius->y *= factor;
    }
  }
  return radii;
}

[[nodiscard]] std::vector<Vec2> roundedPerimeter(
    Rect box, EllipticalRadii radii, std::size_t corner_segments) {
  corner_segments = std::clamp<std::size_t>(corner_segments, 1u, 64u);
  radii = normalizeEllipticalRadii(box, radii);
  struct Corner {
    Vec2 center;
    Vec2 radius;
    float start_angle;
  };
  const std::array<Corner, 4u> corners{{
      {{box.x + box.width - radii.top_right.x, box.y + radii.top_right.y},
       radii.top_right, -std::numbers::pi_v<float> * 0.5f},
      {{box.x + box.width - radii.bottom_right.x,
        box.y + box.height - radii.bottom_right.y},
       radii.bottom_right, 0.0f},
      {{box.x + radii.bottom_left.x,
        box.y + box.height - radii.bottom_left.y},
       radii.bottom_left, std::numbers::pi_v<float> * 0.5f},
      {{box.x + radii.top_left.x, box.y + radii.top_left.y},
       radii.top_left, std::numbers::pi_v<float>},
  }};
  std::vector<Vec2> points;
  points.reserve(4u * (corner_segments + 1u));
  for (const Corner& corner : corners) {
    for (std::size_t segment = 0u; segment <= corner_segments; ++segment) {
      const float amount = static_cast<float>(segment) /
                           static_cast<float>(corner_segments);
      const float angle =
          corner.start_angle + amount * std::numbers::pi_v<float> * 0.5f;
      points.push_back({corner.center.x + std::cos(angle) * corner.radius.x,
                        corner.center.y + std::sin(angle) * corner.radius.y});
    }
  }
  return points;
}

[[nodiscard]] Vec2 uvFor(Rect box, Vec2 point) {
  return {(point.x - box.x) / box.width, (point.y - box.y) / box.height};
}

struct GradientPoint {
  Vec2 position;
  Vec2 uv;
  float offset = 0.0f;
};

[[nodiscard]] std::vector<GradientPoint> clipGradientPolygon(
    const std::vector<GradientPoint>& input, float boundary, bool keep_greater) {
  std::vector<GradientPoint> output;
  if (input.empty()) {
    return output;
  }
  output.reserve(input.size() + 2u);
  const auto inside = [boundary, keep_greater](float offset) {
    return keep_greater ? offset >= boundary - kEpsilon
                        : offset <= boundary + kEpsilon;
  };
  const auto intersection = [boundary](const GradientPoint& from,
                                       const GradientPoint& to) {
    const float span = to.offset - from.offset;
    const float amount = std::abs(span) <= kEpsilon
                             ? 0.0f
                             : (boundary - from.offset) / span;
    return GradientPoint{
        .position = {from.position.x + (to.position.x - from.position.x) * amount,
                     from.position.y + (to.position.y - from.position.y) * amount},
        .uv = {from.uv.x + (to.uv.x - from.uv.x) * amount,
               from.uv.y + (to.uv.y - from.uv.y) * amount},
        .offset = boundary};
  };

  GradientPoint previous = input.back();
  bool previous_inside = inside(previous.offset);
  for (const GradientPoint& current : input) {
    const bool current_inside = inside(current.offset);
    if (current_inside != previous_inside) {
      output.push_back(intersection(previous, current));
    }
    if (current_inside) {
      output.push_back(current);
    }
    previous = current;
    previous_inside = current_inside;
  }
  return output;
}

[[nodiscard]] bool parseDirection(std::string_view source, float& angle) {
  if (parseAngle(source, angle)) {
    return true;
  }
  const std::string lower = lowercase(trim(source));
  if (!lower.starts_with("to ")) {
    return false;
  }
  float x = 0.0f;
  float y = 0.0f;
  for (std::string_view token : splitWhitespace(
           std::string_view(lower).substr(3u))) {
    if (token == "left") {
      if (x != 0.0f) {
        return false;
      }
      x = -1.0f;
    } else if (token == "right") {
      if (x != 0.0f) {
        return false;
      }
      x = 1.0f;
    } else if (token == "top") {
      if (y != 0.0f) {
        return false;
      }
      y = -1.0f;
    } else if (token == "bottom") {
      if (y != 0.0f) {
        return false;
      }
      y = 1.0f;
    } else {
      return false;
    }
  }
  if (x == 0.0f && y == 0.0f) {
    return false;
  }
  angle = std::atan2(x, -y) * 180.0f / std::numbers::pi_v<float>;
  if (angle < 0.0f) {
    angle += 360.0f;
  }
  return true;
}

[[nodiscard]] bool parseNormalizedPositionToken(std::string_view source,
                                                float& value) {
  source = trim(source);
  if (source.empty() || source.back() != '%') {
    return false;
  }
  source.remove_suffix(1u);
  if (!parseFloat(source, value)) {
    return false;
  }
  value /= 100.0f;
  return true;
}

void normalizeOpposing(float available, float& first, float& second) {
  first = nonnegativeFinite(first);
  second = nonnegativeFinite(second);
  const float sum = first + second;
  if (sum > available && sum > 0.0f) {
    const float scale = std::max(0.0f, available) / sum;
    first *= scale;
    second *= scale;
  }
}

[[nodiscard]] bool parsePixelValue(std::string_view source, float& value) {
  source = trim(source);
  const std::string lower = lowercase(source);
  if (lower.size() > 2u && lower.ends_with("px")) {
    source.remove_suffix(2u);
  }
  return parseFloat(source, value);
}

[[nodiscard]] bool parseTransformLength(std::string_view source,
                                        TransformLength& output) {
  source = trim(source);
  const std::string lower = lowercase(source);
  LengthUnit unit = LengthUnit::Pixels;
  if (!source.empty() && source.back() == '%') {
    source.remove_suffix(1u);
    unit = LengthUnit::Percent;
  } else if (lower.size() > 2u && lower.ends_with("px")) {
    source.remove_suffix(2u);
  }
  float value = 0.0f;
  if (!parseFloat(source, value)) {
    return false;
  }
  output = {.value = value, .unit = unit};
  return true;
}

[[nodiscard]] std::vector<std::string_view> transformArguments(
    std::string_view source) {
  if (source.find(',') != std::string_view::npos) {
    return splitTopLevel(source, ',');
  }
  return splitWhitespace(source);
}

[[nodiscard]] Affine2D multiply(Affine2D left, Affine2D right) {
  return {.a = left.a * right.a + left.c * right.b,
          .b = left.b * right.a + left.d * right.b,
          .c = left.a * right.c + left.c * right.d,
          .d = left.b * right.c + left.d * right.d,
          .tx = left.a * right.tx + left.c * right.ty + left.tx,
          .ty = left.b * right.tx + left.d * right.ty + left.ty};
}

[[nodiscard]] Affine2D translation(float x, float y) {
  return {.tx = x, .ty = y};
}

}  // namespace

void Mesh::clear() noexcept {
  vertices.clear();
  indices.clear();
}

bool Mesh::append(const Mesh& other) {
  if (other.vertices.empty() && other.indices.empty()) {
    return true;
  }
  constexpr std::size_t kMaxIndex =
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
  if (vertices.size() > kMaxIndex || other.vertices.size() > kMaxIndex ||
      other.vertices.size() > kMaxIndex - vertices.size()) {
    return false;
  }
  const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
  for (std::uint32_t index : other.indices) {
    if (index >= other.vertices.size() || index >
            std::numeric_limits<std::uint32_t>::max() - base) {
      return false;
    }
  }
  vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
  indices.reserve(indices.size() + other.indices.size());
  for (std::uint32_t index : other.indices) {
    indices.push_back(base + index);
  }
  return true;
}

bool Mesh::valid() const noexcept {
  if (indices.size() % 3u != 0u) {
    return false;
  }
  for (const Vertex& vertex : vertices) {
    if (!finite(vertex.position.x) || !finite(vertex.position.y) ||
        !finite(vertex.uv.x) || !finite(vertex.uv.y) ||
        !math::isFinite(vertex.color)) {
      return false;
    }
  }
  return std::all_of(indices.begin(), indices.end(), [this](std::uint32_t index) {
    return index < vertices.size();
  });
}

bool parseCornerRadii(std::string_view source,
                      CornerRadii& output,
                      std::string* error) {
  clearError(error);
  const auto parts = splitWhitespace(trim(source));
  if (parts.empty() || parts.size() > 4u ||
      source.find(',') != std::string_view::npos ||
      source.find('/') != std::string_view::npos) {
    setError(error, "corner radii require one to four circular px values");
    return false;
  }
  std::array<float, 4u> values{};
  for (std::size_t index = 0u; index < parts.size(); ++index) {
    if (!parsePixelValue(parts[index], values[index]) || values[index] < 0.0f) {
      setError(error, "corner radii must be finite non-negative px values");
      return false;
    }
  }
  switch (parts.size()) {
    case 1u:
      output = {values[0], values[0], values[0], values[0]};
      break;
    case 2u:
      output = {values[0], values[1], values[0], values[1]};
      break;
    case 3u:
      output = {values[0], values[1], values[2], values[1]};
      break;
    case 4u:
      output = {values[0], values[1], values[2], values[3]};
      break;
    default:
      return false;
  }
  return true;
}

bool parseBorderWidths(std::string_view source,
                       BorderWidths& output,
                       std::string* error) {
  clearError(error);
  const auto parts = splitWhitespace(trim(source));
  if (parts.empty() || parts.size() > 4u ||
      source.find(',') != std::string_view::npos) {
    setError(error, "border widths require one to four px values");
    return false;
  }
  std::array<float, 4u> values{};
  for (std::size_t index = 0u; index < parts.size(); ++index) {
    if (!parsePixelValue(parts[index], values[index]) || values[index] < 0.0f) {
      setError(error, "border widths must be finite non-negative px values");
      return false;
    }
  }
  switch (parts.size()) {
    case 1u:
      output = {values[0], values[0], values[0], values[0]};
      break;
    case 2u:
      output = {values[1], values[0], values[1], values[0]};
      break;
    case 3u:
      output = {values[1], values[0], values[1], values[2]};
      break;
    case 4u:
      output = {values[3], values[0], values[1], values[2]};
      break;
    default:
      return false;
  }
  return true;
}

CornerRadii normalizeCornerRadii(Rect box, CornerRadii radii) {
  radii.top_left = nonnegativeFinite(radii.top_left);
  radii.top_right = nonnegativeFinite(radii.top_right);
  radii.bottom_right = nonnegativeFinite(radii.bottom_right);
  radii.bottom_left = nonnegativeFinite(radii.bottom_left);
  float factor = 1.0f;
  const auto reduce = [&factor](float available, float requested) {
    if (requested > available && requested > 0.0f) {
      factor = std::min(factor, std::max(0.0f, available) / requested);
    }
  };
  reduce(std::max(0.0f, box.width), radii.top_left + radii.top_right);
  reduce(std::max(0.0f, box.width),
         radii.bottom_left + radii.bottom_right);
  reduce(std::max(0.0f, box.height),
         radii.top_left + radii.bottom_left);
  reduce(std::max(0.0f, box.height),
         radii.top_right + radii.bottom_right);
  radii.top_left *= factor;
  radii.top_right *= factor;
  radii.bottom_right *= factor;
  radii.bottom_left *= factor;
  return radii;
}

Mesh roundedRectFill(Rect box,
                     CornerRadii radii,
                     math::Color color,
                     std::size_t corner_segments) {
  Mesh mesh;
  if (!drawable(box) || !math::isFinite(color)) {
    return mesh;
  }
  radii = normalizeCornerRadii(box, radii);
  const EllipticalRadii elliptical{
      .top_left = {radii.top_left, radii.top_left},
      .top_right = {radii.top_right, radii.top_right},
      .bottom_right = {radii.bottom_right, radii.bottom_right},
      .bottom_left = {radii.bottom_left, radii.bottom_left}};
  const std::vector<Vec2> perimeter =
      roundedPerimeter(box, elliptical, corner_segments);
  if (perimeter.size() < 3u) {
    return mesh;
  }
  const Vec2 center{box.x + box.width * 0.5f, box.y + box.height * 0.5f};
  mesh.vertices.reserve(perimeter.size() + 1u);
  mesh.vertices.push_back({.position = center, .uv = uvFor(box, center), .color = color});
  for (Vec2 point : perimeter) {
    mesh.vertices.push_back({.position = point, .uv = uvFor(box, point), .color = color});
  }
  mesh.indices.reserve(perimeter.size() * 3u);
  for (std::size_t index = 0u; index < perimeter.size(); ++index) {
    mesh.indices.push_back(0u);
    mesh.indices.push_back(static_cast<std::uint32_t>(index + 1u));
    mesh.indices.push_back(
        static_cast<std::uint32_t>((index + 1u) % perimeter.size() + 1u));
  }
  return mesh;
}

Mesh roundedRectBorder(Rect box,
                       CornerRadii radii,
                       BorderWidths widths,
                       math::Color color,
                       std::size_t corner_segments) {
  Mesh mesh;
  if (!drawable(box) || !math::isFinite(color)) {
    return mesh;
  }
  widths.left = nonnegativeFinite(widths.left);
  widths.top = nonnegativeFinite(widths.top);
  widths.right = nonnegativeFinite(widths.right);
  widths.bottom = nonnegativeFinite(widths.bottom);
  if (widths.left + widths.top + widths.right + widths.bottom <= kEpsilon) {
    return mesh;
  }
  normalizeOpposing(box.width, widths.left, widths.right);
  normalizeOpposing(box.height, widths.top, widths.bottom);
  radii = normalizeCornerRadii(box, radii);
  const EllipticalRadii outer_radii{
      .top_left = {radii.top_left, radii.top_left},
      .top_right = {radii.top_right, radii.top_right},
      .bottom_right = {radii.bottom_right, radii.bottom_right},
      .bottom_left = {radii.bottom_left, radii.bottom_left}};
  const Rect inner{box.x + widths.left,
                   box.y + widths.top,
                   std::max(0.0f, box.width - widths.left - widths.right),
                   std::max(0.0f, box.height - widths.top - widths.bottom)};
  if (inner.width <= kEpsilon || inner.height <= kEpsilon) {
    return roundedRectFill(box, radii, color, corner_segments);
  }
  const EllipticalRadii inner_radii{
      .top_left = {std::max(0.0f, radii.top_left - widths.left),
                   std::max(0.0f, radii.top_left - widths.top)},
      .top_right = {std::max(0.0f, radii.top_right - widths.right),
                    std::max(0.0f, radii.top_right - widths.top)},
      .bottom_right = {std::max(0.0f, radii.bottom_right - widths.right),
                       std::max(0.0f, radii.bottom_right - widths.bottom)},
      .bottom_left = {std::max(0.0f, radii.bottom_left - widths.left),
                      std::max(0.0f, radii.bottom_left - widths.bottom)}};
  const std::vector<Vec2> outer =
      roundedPerimeter(box, outer_radii, corner_segments);
  const std::vector<Vec2> inner_points =
      roundedPerimeter(inner, inner_radii, corner_segments);
  if (outer.size() != inner_points.size() || outer.size() < 3u) {
    return mesh;
  }
  mesh.vertices.reserve(outer.size() * 2u);
  for (Vec2 point : outer) {
    mesh.vertices.push_back({.position = point, .uv = uvFor(box, point), .color = color});
  }
  for (Vec2 point : inner_points) {
    mesh.vertices.push_back({.position = point, .uv = uvFor(box, point), .color = color});
  }
  mesh.indices.reserve(outer.size() * 6u);
  const std::uint32_t inner_base = static_cast<std::uint32_t>(outer.size());
  for (std::size_t index = 0u; index < outer.size(); ++index) {
    const std::uint32_t outer_current = static_cast<std::uint32_t>(index);
    const std::uint32_t outer_next =
        static_cast<std::uint32_t>((index + 1u) % outer.size());
    const std::uint32_t inner_current = inner_base + outer_current;
    const std::uint32_t inner_next = inner_base + outer_next;
    mesh.indices.insert(mesh.indices.end(),
                        {outer_current, outer_next, inner_next,
                         outer_current, inner_next, inner_current});
  }
  return mesh;
}

bool parseLinearGradient(std::string_view source,
                         LinearGradient& output,
                         std::string* error) {
  clearError(error);
  const auto body = functionBody(source, "linear-gradient");
  if (!body.has_value()) {
    setError(error, "expected linear-gradient(...)");
    return false;
  }
  const auto parts = splitTopLevel(*body, ',');
  if (parts.size() < 2u ||
      std::any_of(parts.begin(), parts.end(), [](std::string_view part) {
        return part.empty();
      })) {
    setError(error, "a linear gradient requires at least two color stops");
    return false;
  }
  LinearGradient parsed;
  std::size_t first_stop = 0u;
  float direction = 0.0f;
  if (parseDirection(parts.front(), direction)) {
    parsed.angle_degrees = direction;
    first_stop = 1u;
  }
  std::vector<UnresolvedStop> stops;
  for (std::size_t index = first_stop; index < parts.size(); ++index) {
    UnresolvedStop stop;
    if (!parseGradientStop(parts[index], stop)) {
      setError(error, "invalid linear-gradient color stop");
      return false;
    }
    stops.push_back(stop);
  }
  if (stops.size() < 2u) {
    setError(error, "a linear gradient requires at least two color stops");
    return false;
  }
  parsed.stops = resolveStops(stops);
  output = std::move(parsed);
  return true;
}

bool parseRadialGradient(std::string_view source,
                         RadialGradient& output,
                         std::string* error) {
  clearError(error);
  const auto body = functionBody(source, "radial-gradient");
  if (!body.has_value()) {
    setError(error, "expected radial-gradient(...)");
    return false;
  }
  const auto parts = splitTopLevel(*body, ',');
  if (parts.size() < 2u ||
      std::any_of(parts.begin(), parts.end(), [](std::string_view part) {
        return part.empty();
      })) {
    setError(error, "a radial gradient requires at least two color stops");
    return false;
  }
  RadialGradient parsed;
  std::size_t first_stop = 0u;
  UnresolvedStop candidate;
  if (!parseGradientStop(parts.front(), candidate)) {
    first_stop = 1u;
    const std::string prelude = lowercase(parts.front());
    if (prelude.find("circle") != std::string::npos) {
      parsed.circle = true;
    } else if (prelude.find("ellipse") == std::string::npos &&
               !std::string_view(prelude).starts_with("at ")) {
      setError(error, "unsupported radial-gradient shape");
      return false;
    }
    const std::size_t at = prelude.find("at ");
    if (at != std::string::npos) {
      ObjectPosition position;
      if (!parseObjectPosition(std::string_view(prelude).substr(at + 3u),
                               position, error)) {
        return false;
      }
      parsed.center = {position.x, position.y};
    }
  }
  std::vector<UnresolvedStop> stops;
  if (first_stop == 0u) {
    stops.push_back(candidate);
    first_stop = 1u;
  }
  for (std::size_t index = first_stop; index < parts.size(); ++index) {
    UnresolvedStop stop;
    if (!parseGradientStop(parts[index], stop)) {
      setError(error, "invalid radial-gradient color stop");
      return false;
    }
    stops.push_back(stop);
  }
  if (stops.size() < 2u) {
    setError(error, "a radial gradient requires at least two color stops");
    return false;
  }
  parsed.stops = resolveStops(stops);
  output = std::move(parsed);
  return true;
}

Mesh linearGradientFill(Rect box, const LinearGradient& gradient) {
  Mesh mesh;
  const std::vector<GradientStop> stops = normalizeStops(gradient.stops);
  if (!drawable(box) || stops.empty() || !finite(gradient.angle_degrees)) {
    return mesh;
  }
  const float radians = gradient.angle_degrees * std::numbers::pi_v<float> /
                        180.0f;
  const Vec2 direction{std::sin(radians), -std::cos(radians)};
  const Vec2 center{box.x + box.width * 0.5f, box.y + box.height * 0.5f};
  std::array<GradientPoint, 4u> corners{{
      {{box.x, box.y}, {0.0f, 0.0f}, 0.0f},
      {{box.x + box.width, box.y}, {1.0f, 0.0f}, 0.0f},
      {{box.x + box.width, box.y + box.height}, {1.0f, 1.0f}, 0.0f},
      {{box.x, box.y + box.height}, {0.0f, 1.0f}, 0.0f},
  }};
  float minimum = std::numeric_limits<float>::max();
  float maximum = std::numeric_limits<float>::lowest();
  for (GradientPoint& corner : corners) {
    corner.offset = (corner.position.x - center.x) * direction.x +
                    (corner.position.y - center.y) * direction.y;
    minimum = std::min(minimum, corner.offset);
    maximum = std::max(maximum, corner.offset);
  }
  const float span = maximum - minimum;
  if (span <= kEpsilon) {
    return mesh;
  }
  for (GradientPoint& corner : corners) {
    corner.offset = (corner.offset - minimum) / span;
  }
  const std::vector<GradientPoint> rectangle(corners.begin(), corners.end());
  for (std::size_t stop = 1u; stop < stops.size(); ++stop) {
    const float low = stops[stop - 1u].offset;
    const float high = stops[stop].offset;
    if (high - low <= kEpsilon) {
      continue;
    }
    std::vector<GradientPoint> polygon =
        clipGradientPolygon(rectangle, low, true);
    polygon = clipGradientPolygon(polygon, high, false);
    if (polygon.size() < 3u) {
      continue;
    }
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const GradientPoint& point : polygon) {
      const float amount = (point.offset - low) / (high - low);
      mesh.vertices.push_back({.position = point.position,
                               .uv = point.uv,
                               .color = mix(stops[stop - 1u].color,
                                            stops[stop].color, amount)});
    }
    for (std::uint32_t index = 1u; index + 1u < polygon.size(); ++index) {
      mesh.indices.insert(mesh.indices.end(),
                          {base, base + index, base + index + 1u});
    }
  }
  return mesh;
}

Mesh radialGradientFill(Rect box,
                        const RadialGradient& gradient,
                        std::size_t columns,
                        std::size_t rows) {
  Mesh mesh;
  const std::vector<GradientStop> stops = normalizeStops(gradient.stops);
  if (!drawable(box) || stops.empty() || !finite(gradient.center.x) ||
      !finite(gradient.center.y) || !finite(gradient.radius.x) ||
      !finite(gradient.radius.y)) {
    return mesh;
  }
  columns = std::clamp<std::size_t>(columns, 1u, 128u);
  rows = std::clamp<std::size_t>(rows, 1u, 128u);
  float radius_x = std::abs(gradient.radius.x);
  float radius_y = std::abs(gradient.radius.y);
  if (gradient.circle) {
    const float physical_radius = std::min(box.width, box.height) * 0.5f;
    radius_x = physical_radius / box.width;
    radius_y = physical_radius / box.height;
  }
  radius_x = std::max(radius_x, kEpsilon);
  radius_y = std::max(radius_y, kEpsilon);
  mesh.vertices.reserve((columns + 1u) * (rows + 1u));
  for (std::size_t row = 0u; row <= rows; ++row) {
    const float y = static_cast<float>(row) / static_cast<float>(rows);
    for (std::size_t column = 0u; column <= columns; ++column) {
      const float x = static_cast<float>(column) / static_cast<float>(columns);
      const float dx = (x - gradient.center.x) / radius_x;
      const float dy = (y - gradient.center.y) / radius_y;
      const float distance = std::sqrt(dx * dx + dy * dy);
      mesh.vertices.push_back(
          {.position = {box.x + x * box.width, box.y + y * box.height},
           .uv = {x, y},
           .color = sampleStops(stops, distance)});
    }
  }
  mesh.indices.reserve(columns * rows * 6u);
  const std::uint32_t stride = static_cast<std::uint32_t>(columns + 1u);
  for (std::size_t row = 0u; row < rows; ++row) {
    for (std::size_t column = 0u; column < columns; ++column) {
      const std::uint32_t top_left =
          static_cast<std::uint32_t>(row) * stride +
          static_cast<std::uint32_t>(column);
      const std::uint32_t top_right = top_left + 1u;
      const std::uint32_t bottom_left = top_left + stride;
      const std::uint32_t bottom_right = bottom_left + 1u;
      mesh.indices.insert(mesh.indices.end(),
                          {top_left, top_right, bottom_right,
                           top_left, bottom_right, bottom_left});
    }
  }
  return mesh;
}

bool parseInsets(std::string_view source, Insets& output, std::string* error) {
  clearError(error);
  const auto parts = splitWhitespace(trim(source));
  if (parts.empty() || parts.size() > 4u || source.find(',') != std::string_view::npos) {
    setError(error, "insets require one to four values");
    return false;
  }
  std::array<float, 4u> values{};
  for (std::size_t index = 0u; index < parts.size(); ++index) {
    if (!parsePixelValue(parts[index], values[index]) || values[index] < 0.0f) {
      setError(error, "insets must be finite non-negative px values");
      return false;
    }
  }
  switch (parts.size()) {
    case 1u:
      output = {values[0], values[0], values[0], values[0]};
      break;
    case 2u:
      output = {values[1], values[0], values[1], values[0]};
      break;
    case 3u:
      output = {values[1], values[0], values[1], values[2]};
      break;
    case 4u:
      output = {values[3], values[0], values[1], values[2]};
      break;
    default:
      return false;
  }
  return true;
}

bool parseNineSliceRepeat(std::string_view source,
                          NineSliceRepeat& output,
                          std::string* error) {
  clearError(error);
  const auto parts = splitWhitespace(trim(source));
  if (parts.empty() || parts.size() > 2u ||
      source.find(',') != std::string_view::npos) {
    setError(error, "nine-slice repeat requires one or two modes");
    return false;
  }
  auto parse_mode = [](std::string_view value)
      -> std::optional<NineSliceRepeatMode> {
    const std::string mode = lowercase(value);
    if (mode == "stretch") return NineSliceRepeatMode::Stretch;
    if (mode == "repeat") return NineSliceRepeatMode::Repeat;
    if (mode == "round") return NineSliceRepeatMode::Round;
    return std::nullopt;
  };
  const auto horizontal = parse_mode(parts[0]);
  const auto vertical = parse_mode(parts.size() == 1u ? parts[0] : parts[1]);
  if (!horizontal.has_value() || !vertical.has_value()) {
    setError(error, "nine-slice repeat modes must be stretch, repeat, or round");
    return false;
  }
  output = {.horizontal = *horizontal, .vertical = *vertical};
  return true;
}

Mesh nineSlicePanel(Rect destination,
                    const NineSlice& slice,
                    math::Color tint,
                    NineSliceBuildStatus* status) {
  if (status != nullptr) *status = {};
  Mesh mesh;
  if (!drawable(destination) || slice.source_size.x <= 0.0f ||
      slice.source_size.y <= 0.0f || !finite(slice.source_size.x) ||
      !finite(slice.source_size.y) || !drawable(slice.source_uv) ||
      !math::isFinite(tint)) {
    return mesh;
  }
  Insets source = slice.source_slices;
  normalizeOpposing(slice.source_size.x, source.left, source.right);
  normalizeOpposing(slice.source_size.y, source.top, source.bottom);
  Insets target = slice.destination_slices.value_or(source);
  normalizeOpposing(destination.width, target.left, target.right);
  normalizeOpposing(destination.height, target.top, target.bottom);

  // Preserve the original shared-grid mesh byte-for-byte for compatibility.
  if (slice.repeat.horizontal == NineSliceRepeatMode::Stretch &&
      slice.repeat.vertical == NineSliceRepeatMode::Stretch) {
  const std::array<float, 4u> x{{destination.x,
                                 destination.x + target.left,
                                 destination.x + destination.width - target.right,
                                 destination.x + destination.width}};
  const std::array<float, 4u> y{{destination.y,
                                 destination.y + target.top,
                                 destination.y + destination.height - target.bottom,
                                 destination.y + destination.height}};
  const std::array<float, 4u> source_x{{0.0f, source.left,
                                        slice.source_size.x - source.right,
                                        slice.source_size.x}};
  const std::array<float, 4u> source_y{{0.0f, source.top,
                                        slice.source_size.y - source.bottom,
                                        slice.source_size.y}};
  std::array<float, 4u> u{};
  std::array<float, 4u> v{};
  for (std::size_t index = 0u; index < 4u; ++index) {
    u[index] = slice.source_uv.x +
               slice.source_uv.width * source_x[index] / slice.source_size.x;
    v[index] = slice.source_uv.y +
               slice.source_uv.height * source_y[index] / slice.source_size.y;
  }
  mesh.vertices.reserve(16u);
  for (std::size_t row = 0u; row < 4u; ++row) {
    for (std::size_t column = 0u; column < 4u; ++column) {
      mesh.vertices.push_back({.position = {x[column], y[row]},
                               .uv = {u[column], v[row]},
                               .color = tint});
    }
  }
  mesh.indices.reserve(54u);
  for (std::uint32_t row = 0u; row < 3u; ++row) {
    for (std::uint32_t column = 0u; column < 3u; ++column) {
      if (x[column + 1u] - x[column] <= kEpsilon ||
          y[row + 1u] - y[row] <= kEpsilon) {
        continue;
      }
      const std::uint32_t top_left = row * 4u + column;
      const std::uint32_t top_right = top_left + 1u;
      const std::uint32_t bottom_left = top_left + 4u;
      const std::uint32_t bottom_right = bottom_left + 1u;
      mesh.indices.insert(mesh.indices.end(),
                          {top_left, top_right, bottom_right,
                           top_left, bottom_right, bottom_left});
    }
  }
    if (status != nullptr) {
      status->generated_cells = mesh.indices.size() / 6u;
    }
  return mesh;
  }

  const std::array<float, 4u> destination_x{{
      destination.x,
      destination.x + target.left,
      destination.x + destination.width - target.right,
      destination.x + destination.width}};
  const std::array<float, 4u> destination_y{{
      destination.y,
      destination.y + target.top,
      destination.y + destination.height - target.bottom,
      destination.y + destination.height}};
  const std::array<float, 4u> source_x{{
      0.0f, source.left, slice.source_size.x - source.right,
      slice.source_size.x}};
  const std::array<float, 4u> source_y{{
      0.0f, source.top, slice.source_size.y - source.bottom,
      slice.source_size.y}};

  auto positive_ratio = [](float destination_extent,
                           float source_extent) -> std::optional<float> {
    if (destination_extent <= kEpsilon || source_extent <= kEpsilon) {
      return std::nullopt;
    }
    const float ratio = destination_extent / source_extent;
    return finite(ratio) && ratio > kEpsilon ? std::optional<float>{ratio}
                                             : std::nullopt;
  };
  auto opposing_scale = [&](float first_destination,
                            float first_source,
                            float second_destination,
                            float second_source) {
    const auto first = positive_ratio(first_destination, first_source);
    const auto second = positive_ratio(second_destination, second_source);
    if (first.has_value() && second.has_value()) return std::min(*first, *second);
    if (first.has_value()) return *first;
    if (second.has_value()) return *second;
    return 1.0f;
  };
  // Keep the center motif at the scale established by the adjacent edges.
  // Horizontal edge motifs are scaled by the top/bottom border thickness;
  // vertical edge motifs are scaled by the left/right border thickness.  The
  // center therefore uses those same scales on the matching axes so its motif
  // does not change size at the edge/center seams.
  const float center_horizontal_scale =
      opposing_scale(target.top, source.top, target.bottom, source.bottom);
  const float center_vertical_scale =
      opposing_scale(target.left, source.left, target.right, source.right);

  struct RegionPlan {
    float destination_x0 = 0.0f;
    float destination_x1 = 0.0f;
    float destination_y0 = 0.0f;
    float destination_y1 = 0.0f;
    float source_x0 = 0.0f;
    float source_x1 = 0.0f;
    float source_y0 = 0.0f;
    float source_y1 = 0.0f;
    NineSliceRepeatMode horizontal = NineSliceRepeatMode::Stretch;
    NineSliceRepeatMode vertical = NineSliceRepeatMode::Stretch;
    float nominal_width = 0.0f;
    float nominal_height = 0.0f;
    std::size_t natural_columns = 1u;
    std::size_t natural_rows = 1u;
    std::size_t columns = 1u;
    std::size_t rows = 1u;
  };

  auto natural_count = [](float extent,
                          float nominal,
                          NineSliceRepeatMode mode) -> std::size_t {
    if (mode == NineSliceRepeatMode::Stretch || extent <= kEpsilon ||
        nominal <= kEpsilon || !finite(nominal)) {
      return 1u;
    }
    const double ratio = static_cast<double>(extent) / nominal;
    double count = mode == NineSliceRepeatMode::Repeat
                       ? std::ceil(ratio)
                       : std::floor(ratio + 0.5);
    count = std::max(1.0, count);
    constexpr double sentinel =
        static_cast<double>(kNineSliceCellLimit + 1u);
    if (!std::isfinite(count) || count >= sentinel) {
      return kNineSliceCellLimit + 1u;
    }
    return static_cast<std::size_t>(count);
  };

  std::vector<RegionPlan> regions;
  regions.reserve(9u);
  for (std::size_t row = 0u; row < 3u; ++row) {
    for (std::size_t column = 0u; column < 3u; ++column) {
      const float destination_width =
          destination_x[column + 1u] - destination_x[column];
      const float destination_height =
          destination_y[row + 1u] - destination_y[row];
      const float source_width = source_x[column + 1u] - source_x[column];
      const float source_height = source_y[row + 1u] - source_y[row];
      if (destination_width <= kEpsilon || destination_height <= kEpsilon ||
          source_width <= kEpsilon || source_height <= kEpsilon) {
        continue;
      }
      RegionPlan region{
          .destination_x0 = destination_x[column],
          .destination_x1 = destination_x[column + 1u],
          .destination_y0 = destination_y[row],
          .destination_y1 = destination_y[row + 1u],
          .source_x0 = source_x[column],
          .source_x1 = source_x[column + 1u],
          .source_y0 = source_y[row],
          .source_y1 = source_y[row + 1u]};
      if (column == 1u) {
        region.horizontal = slice.repeat.horizontal;
        const float scale = row == 1u
                                ? center_horizontal_scale
                                : destination_height / source_height;
        region.nominal_width = source_width * scale;
      }
      if (row == 1u) {
        region.vertical = slice.repeat.vertical;
        const float scale = column == 1u
                                ? center_vertical_scale
                                : destination_width / source_width;
        region.nominal_height = source_height * scale;
      }
      region.natural_columns = natural_count(
          destination_width, region.nominal_width, region.horizontal);
      region.natural_rows = natural_count(
          destination_height, region.nominal_height, region.vertical);
      region.columns = region.natural_columns;
      region.rows = region.natural_rows;
      regions.push_back(region);
    }
  }

  auto total_cells = [&]() {
    std::uint64_t total = 0u;
    for (const RegionPlan& region : regions) {
      total += static_cast<std::uint64_t>(region.columns) * region.rows;
    }
    return total;
  };
  bool limit_reduced = false;
  while (total_cells() > kNineSliceCellLimit) {
    RegionPlan* selected = nullptr;
    bool reduce_columns = false;
    std::size_t greatest_saving = 0u;
    for (RegionPlan& region : regions) {
      if (region.columns > 1u && region.rows > greatest_saving) {
        selected = &region;
        reduce_columns = true;
        greatest_saving = region.rows;
      }
      if (region.rows > 1u && region.columns > greatest_saving) {
        selected = &region;
        reduce_columns = false;
        greatest_saving = region.columns;
      }
    }
    if (selected == nullptr) break;
    if (reduce_columns) {
      --selected->columns;
    } else {
      --selected->rows;
    }
    limit_reduced = true;
  }

  struct AxisSegment {
    float start = 0.0f;
    float end = 0.0f;
    float uv_start = 0.0f;
    float uv_end = 0.0f;
  };
  auto snap = [](float value, float scale) {
    return finite(scale) && scale > kEpsilon
               ? std::round(value * scale) / scale
               : value;
  };
  auto uv_span = [](float source_start,
                    float selected_source_extent,
                    float source_total,
                    float uv_start,
                    float uv_extent) {
    const float inset = std::min(0.5f, selected_source_extent * 0.5f);
    const float first = source_start + inset;
    const float second = source_start + selected_source_extent - inset;
    return std::pair{
        uv_start + uv_extent * first / source_total,
        uv_start + uv_extent * second / source_total};
  };
  auto segments = [&](float destination_start,
                      float destination_end,
                      float source_start,
                      float source_end,
                      NineSliceRepeatMode mode,
                      float nominal_extent,
                      std::size_t natural,
                      std::size_t count,
                      float pixel_scale,
                      float source_total,
                      float uv_start,
                      float uv_extent) {
    std::vector<AxisSegment> output;
    output.reserve(count);
    const float destination_extent = destination_end - destination_start;
    const float source_extent = source_end - source_start;
    const bool forced_fit = count < natural;
    const bool evenly_fit = mode == NineSliceRepeatMode::Stretch ||
                            mode == NineSliceRepeatMode::Round || forced_fit ||
                            nominal_extent <= kEpsilon;
    for (std::size_t index = 0u; index < count; ++index) {
      const float raw_start = evenly_fit
                                  ? destination_start + destination_extent *
                                        static_cast<float>(index) /
                                        static_cast<float>(count)
                                  : destination_start + nominal_extent *
                                        static_cast<float>(index);
      const float raw_end = evenly_fit
                                ? destination_start + destination_extent *
                                      static_cast<float>(index + 1u) /
                                      static_cast<float>(count)
                                : std::min(destination_end,
                                           raw_start + nominal_extent);
      const float fraction =
          mode == NineSliceRepeatMode::Repeat && !forced_fit &&
                  nominal_extent > kEpsilon
              ? std::clamp((raw_end - raw_start) / nominal_extent, 0.0f, 1.0f)
              : 1.0f;
      const auto [first_uv, second_uv] = uv_span(
          source_start, source_extent * fraction, source_total, uv_start,
          uv_extent);
      output.push_back({.start = snap(raw_start, pixel_scale),
                        .end = snap(raw_end, pixel_scale),
                        .uv_start = first_uv,
                        .uv_end = second_uv});
    }
    return output;
  };

  const std::size_t reserved_cells =
      static_cast<std::size_t>(std::min<std::uint64_t>(
          total_cells(), static_cast<std::uint64_t>(kNineSliceCellLimit)));
  mesh.vertices.reserve(reserved_cells * 4u);
  mesh.indices.reserve(reserved_cells * 6u);
  std::size_t generated_cells = 0u;
  for (const RegionPlan& region : regions) {
    const auto horizontal = segments(
        region.destination_x0, region.destination_x1, region.source_x0,
        region.source_x1, region.horizontal, region.nominal_width,
        region.natural_columns, region.columns, slice.pixel_scale.x,
        slice.source_size.x, slice.source_uv.x, slice.source_uv.width);
    const auto vertical = segments(
        region.destination_y0, region.destination_y1, region.source_y0,
        region.source_y1, region.vertical, region.nominal_height,
        region.natural_rows, region.rows, slice.pixel_scale.y,
        slice.source_size.y, slice.source_uv.y, slice.source_uv.height);
    for (const AxisSegment& vertical_segment : vertical) {
      for (const AxisSegment& horizontal_segment : horizontal) {
        if (horizontal_segment.end - horizontal_segment.start <= kEpsilon ||
            vertical_segment.end - vertical_segment.start <= kEpsilon) {
          continue;
        }
        const std::uint32_t base =
            static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.insert(
            mesh.vertices.end(),
            {{.position = {horizontal_segment.start, vertical_segment.start},
              .uv = {horizontal_segment.uv_start, vertical_segment.uv_start},
              .color = tint},
             {.position = {horizontal_segment.end, vertical_segment.start},
              .uv = {horizontal_segment.uv_end, vertical_segment.uv_start},
              .color = tint},
             {.position = {horizontal_segment.end, vertical_segment.end},
              .uv = {horizontal_segment.uv_end, vertical_segment.uv_end},
              .color = tint},
             {.position = {horizontal_segment.start, vertical_segment.end},
              .uv = {horizontal_segment.uv_start, vertical_segment.uv_end},
              .color = tint}});
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1u, base + 2u,
                             base, base + 2u, base + 3u});
        ++generated_cells;
      }
    }
  }
  if (status != nullptr) {
    status->generated_cells = generated_cells;
    status->cell_limit_reduced = limit_reduced;
  }
  return mesh;
}

std::optional<ObjectFit> parseObjectFit(std::string_view source) {
  const std::string lower = lowercase(trim(source));
  if (lower == "fill") {
    return ObjectFit::Fill;
  }
  if (lower == "contain") {
    return ObjectFit::Contain;
  }
  if (lower == "cover") {
    return ObjectFit::Cover;
  }
  if (lower == "none") {
    return ObjectFit::None;
  }
  if (lower == "scale-down") {
    return ObjectFit::ScaleDown;
  }
  return std::nullopt;
}

bool parseObjectPosition(std::string_view source,
                         ObjectPosition& output,
                         std::string* error) {
  clearError(error);
  const std::string lower = lowercase(trim(source));
  const auto tokens = splitWhitespace(lower);
  if (tokens.empty() || tokens.size() > 2u) {
    setError(error, "object position requires one or two components");
    return false;
  }
  ObjectPosition parsed;
  bool x_set = false;
  bool y_set = false;
  const auto assign = [&](std::string_view token, bool preferred_x) {
    float value = 0.0f;
    if (parseNormalizedPositionToken(token, value)) {
      if (preferred_x && !x_set) {
        parsed.x = value;
        x_set = true;
      } else if (!y_set) {
        parsed.y = value;
        y_set = true;
      } else {
        return false;
      }
      return true;
    }
    if (token == "left" || token == "right") {
      if (x_set) {
        return false;
      }
      parsed.x = token == "left" ? 0.0f : 1.0f;
      x_set = true;
      return true;
    }
    if (token == "top" || token == "bottom") {
      if (y_set) {
        return false;
      }
      parsed.y = token == "top" ? 0.0f : 1.0f;
      y_set = true;
      return true;
    }
    if (token == "center") {
      if (preferred_x && !x_set) {
        parsed.x = 0.5f;
        x_set = true;
      } else if (!y_set) {
        parsed.y = 0.5f;
        y_set = true;
      } else if (!x_set) {
        parsed.x = 0.5f;
        x_set = true;
      } else {
        return false;
      }
      return true;
    }
    return false;
  };

  if (tokens.size() == 1u) {
    const bool vertical = tokens[0] == "top" || tokens[0] == "bottom";
    if (!assign(tokens[0], !vertical)) {
      setError(error, "invalid object-position component");
      return false;
    }
  } else {
    const bool first_vertical = tokens[0] == "top" || tokens[0] == "bottom";
    if (!assign(tokens[0], !first_vertical) ||
        !assign(tokens[1], first_vertical)) {
      setError(error, "invalid object-position components");
      return false;
    }
  }
  output = parsed;
  return true;
}

ObjectPlacement placeObject(Rect content_box,
                            Vec2 intrinsic_size,
                            ObjectFit fit,
                            ObjectPosition position) {
  ObjectPlacement placement;
  if (!drawable(content_box) || !finite(intrinsic_size.x) ||
      !finite(intrinsic_size.y) || intrinsic_size.x <= 0.0f ||
      intrinsic_size.y <= 0.0f) {
    return placement;
  }
  position.x = clamp01(finite(position.x) ? position.x : 0.5f);
  position.y = clamp01(finite(position.y) ? position.y : 0.5f);
  if (fit == ObjectFit::Fill) {
    placement.destination = content_box;
    return placement;
  }
  if (fit == ObjectFit::ScaleDown) {
    fit = intrinsic_size.x <= content_box.width &&
                  intrinsic_size.y <= content_box.height
              ? ObjectFit::None
              : ObjectFit::Contain;
  }
  if (fit == ObjectFit::Cover) {
    const float scale = std::max(content_box.width / intrinsic_size.x,
                                 content_box.height / intrinsic_size.y);
    const float rendered_width = intrinsic_size.x * scale;
    const float rendered_height = intrinsic_size.y * scale;
    const float visible_u = clamp01(content_box.width / rendered_width);
    const float visible_v = clamp01(content_box.height / rendered_height);
    placement.destination = content_box;
    placement.uv = {(1.0f - visible_u) * position.x,
                    (1.0f - visible_v) * position.y,
                    visible_u,
                    visible_v};
    return placement;
  }

  float rendered_width = intrinsic_size.x;
  float rendered_height = intrinsic_size.y;
  if (fit == ObjectFit::Contain) {
    const float scale = std::min(content_box.width / intrinsic_size.x,
                                 content_box.height / intrinsic_size.y);
    rendered_width *= scale;
    rendered_height *= scale;
  }
  placement.destination = {
      content_box.x + (content_box.width - rendered_width) * position.x,
      content_box.y + (content_box.height - rendered_height) * position.y,
      rendered_width,
      rendered_height};
  return placement;
}

bool parseTransform(std::string_view source,
                    Transform& output,
                    std::string* error) {
  clearError(error);
  source = trim(source);
  if (lowercase(source) == "none") {
    output.operations.clear();
    return true;
  }
  Transform parsed;
  std::size_t cursor = 0u;
  while (cursor < source.size()) {
    while (cursor < source.size() &&
           std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
      ++cursor;
    }
    if (cursor == source.size()) {
      break;
    }
    const std::size_t name_start = cursor;
    while (cursor < source.size() &&
           (std::isalpha(static_cast<unsigned char>(source[cursor])) != 0 ||
            source[cursor] == '-')) {
      ++cursor;
    }
    const std::string name = lowercase(source.substr(name_start, cursor - name_start));
    if (name.empty() || cursor == source.size() || source[cursor] != '(') {
      setError(error, "expected a supported transform function");
      return false;
    }
    const std::size_t arguments_start = ++cursor;
    int depth = 1;
    while (cursor < source.size() && depth > 0) {
      if (source[cursor] == '(') {
        ++depth;
      } else if (source[cursor] == ')') {
        --depth;
      }
      ++cursor;
    }
    if (depth != 0) {
      setError(error, "unterminated transform function");
      return false;
    }
    const std::string_view arguments_source =
        source.substr(arguments_start, cursor - arguments_start - 1u);
    const auto arguments = transformArguments(arguments_source);
    if (arguments.empty() ||
        std::any_of(arguments.begin(), arguments.end(), [](std::string_view value) {
          return value.empty();
        })) {
      setError(error, "transform function has invalid arguments");
      return false;
    }
    TransformOp operation;
    if (name == "translate" || name == "translatex" || name == "translatey") {
      operation.kind = TransformOpKind::Translate;
      if (name == "translate") {
        if (arguments.size() > 2u ||
            !parseTransformLength(arguments[0], operation.x) ||
            (arguments.size() == 2u &&
             !parseTransformLength(arguments[1], operation.y))) {
          setError(error, "translate expects one or two lengths");
          return false;
        }
      } else if (arguments.size() != 1u) {
        setError(error, "translateX/Y expects one length");
        return false;
      } else if (name == "translatex") {
        if (!parseTransformLength(arguments[0], operation.x)) {
          setError(error, "translateX expects a length");
          return false;
        }
      } else if (!parseTransformLength(arguments[0], operation.y)) {
        setError(error, "translateY expects a length");
        return false;
      }
    } else if (name == "scale" || name == "scalex" || name == "scaley") {
      operation.kind = TransformOpKind::Scale;
      operation.x.value = 1.0f;
      operation.y.value = 1.0f;
      if (name == "scale") {
        if (arguments.size() > 2u || !parseFloat(arguments[0], operation.x.value)) {
          setError(error, "scale expects one or two numbers");
          return false;
        }
        operation.y.value = operation.x.value;
        if (arguments.size() == 2u && !parseFloat(arguments[1], operation.y.value)) {
          setError(error, "scale expects one or two numbers");
          return false;
        }
      } else if (arguments.size() != 1u) {
        setError(error, "scaleX/Y expects one number");
        return false;
      } else if (name == "scalex") {
        if (!parseFloat(arguments[0], operation.x.value)) {
          setError(error, "scaleX expects one number");
          return false;
        }
      } else if (!parseFloat(arguments[0], operation.y.value)) {
        setError(error, "scaleY expects one number");
        return false;
      }
    } else if (name == "rotate") {
      operation.kind = TransformOpKind::Rotate;
      if (arguments.size() != 1u ||
          !parseAngle(arguments[0], operation.angle_degrees)) {
        setError(error, "rotate expects one angle");
        return false;
      }
    } else {
      setError(error, "unsupported transform function: " + name);
      return false;
    }
    parsed.operations.push_back(operation);
  }
  if (parsed.operations.empty()) {
    setError(error, "expected a transform function or none");
    return false;
  }
  output = std::move(parsed);
  return true;
}

Affine2D composeTransform(const Transform& transform,
                          Vec2 reference_size,
                          ObjectPosition origin) {
  reference_size.x = finite(reference_size.x) ? reference_size.x : 0.0f;
  reference_size.y = finite(reference_size.y) ? reference_size.y : 0.0f;
  origin.x = finite(origin.x) ? origin.x : 0.5f;
  origin.y = finite(origin.y) ? origin.y : 0.5f;
  Affine2D matrix;
  for (const TransformOp& operation : transform.operations) {
    Affine2D operation_matrix;
    switch (operation.kind) {
      case TransformOpKind::Translate: {
        const float x = operation.x.unit == LengthUnit::Percent
                            ? operation.x.value * reference_size.x / 100.0f
                            : operation.x.value;
        const float y = operation.y.unit == LengthUnit::Percent
                            ? operation.y.value * reference_size.y / 100.0f
                            : operation.y.value;
        operation_matrix = translation(x, y);
        break;
      }
      case TransformOpKind::Scale:
        operation_matrix.a = operation.x.value;
        operation_matrix.d = operation.y.value;
        break;
      case TransformOpKind::Rotate: {
        const float radians = operation.angle_degrees *
                              std::numbers::pi_v<float> / 180.0f;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        operation_matrix = {.a = cosine,
                            .b = sine,
                            .c = -sine,
                            .d = cosine};
        break;
      }
    }
    matrix = multiply(matrix, operation_matrix);
  }
  const float origin_x = reference_size.x * origin.x;
  const float origin_y = reference_size.y * origin.y;
  return multiply(translation(origin_x, origin_y),
                  multiply(matrix, translation(-origin_x, -origin_y)));
}

Vec2 applyTransform(Affine2D transform, Vec2 point) {
  return {transform.a * point.x + transform.c * point.y + transform.tx,
          transform.b * point.x + transform.d * point.y + transform.ty};
}

void applyTransform(Affine2D transform, Mesh& mesh) {
  for (Vertex& vertex : mesh.vertices) {
    vertex.position = applyTransform(transform, vertex.position);
  }
}

}  // namespace karma::ui::paint
