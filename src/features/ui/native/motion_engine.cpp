#include "features/ui/native/motion_engine.h"

#include "features/ui/native/string_utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace karma::ui::native {
namespace {

constexpr double kValueEpsilon = 1.0e-9;

std::string_view trimView(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1);
  }
  return value;
}

using string_utils::lower;

bool parseFiniteDouble(std::string_view source, double& output) {
  const std::optional<double> parsed =
      string_utils::parseFiniteDouble(source);
  if (!parsed.has_value()) return false;
  output = *parsed;
  return true;
}

bool isZero(double value) {
  return std::abs(value) <= kValueEpsilon;
}

void setError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

bool splitTopLevel(std::string_view source,
                   char separator,
                   std::vector<std::string_view>& output,
                   std::string* error) {
  output.clear();
  int parentheses = 0;
  std::size_t begin = 0;
  for (std::size_t index = 0; index < source.size(); ++index) {
    if (source[index] == '(') {
      ++parentheses;
    } else if (source[index] == ')') {
      --parentheses;
      if (parentheses < 0) {
        setError(error, "unexpected ')' in transition value");
        return false;
      }
    } else if (source[index] == separator && parentheses == 0) {
      output.push_back(trimView(source.substr(begin, index - begin)));
      begin = index + 1;
    }
  }
  if (parentheses != 0) {
    setError(error, "unclosed '(' in transition value");
    return false;
  }
  output.push_back(trimView(source.substr(begin)));
  return true;
}

bool splitWhitespace(std::string_view source,
                     std::vector<std::string_view>& output,
                     std::string* error) {
  output.clear();
  int parentheses = 0;
  std::size_t begin = 0;
  bool in_token = false;
  for (std::size_t index = 0; index < source.size(); ++index) {
    const char ch = source[index];
    if (ch == '(') {
      ++parentheses;
    } else if (ch == ')') {
      --parentheses;
      if (parentheses < 0) {
        setError(error, "unexpected ')' in transition item");
        return false;
      }
    }
    if (parentheses == 0 &&
        std::isspace(static_cast<unsigned char>(ch)) != 0) {
      if (in_token) {
        output.push_back(trimView(source.substr(begin, index - begin)));
        in_token = false;
      }
    } else if (!in_token) {
      begin = index;
      in_token = true;
    }
  }
  if (parentheses != 0) {
    setError(error, "unclosed '(' in transition item");
    return false;
  }
  if (in_token) {
    output.push_back(trimView(source.substr(begin)));
  }
  return true;
}

bool validProperty(std::string_view property) {
  if (property.empty()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(property.front());
  if (std::isalpha(first) == 0 && property.front() != '-' &&
      property.front() != '_') {
    return false;
  }
  return std::all_of(property.begin() + 1, property.end(), [](unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '-' || ch == '_';
  });
}

bool parseTransitionItem(std::string_view source,
                         TransitionSpec& output,
                         std::string* error) {
  std::vector<std::string_view> tokens;
  if (!splitWhitespace(source, tokens, error) || tokens.empty()) {
    setError(error, "empty transition item");
    return false;
  }

  TransitionSpec parsed;
  bool saw_property = false;
  bool saw_easing = false;
  int time_count = 0;
  for (const std::string_view token : tokens) {
    if (const auto time = parseTimeSeconds(token)) {
      if (time_count == 0) {
        if (*time < 0.0) {
          setError(error, "transition duration cannot be negative");
          return false;
        }
        parsed.duration_seconds = *time;
      } else if (time_count == 1) {
        parsed.delay_seconds = *time;
      } else {
        setError(error, "transition item contains more than two times");
        return false;
      }
      ++time_count;
      continue;
    }

    if (const auto easing = parseEasing(token)) {
      if (saw_easing) {
        setError(error, "transition item contains more than one easing");
        return false;
      }
      parsed.easing = *easing;
      saw_easing = true;
      continue;
    }

    if (!saw_property && validProperty(token)) {
      parsed.property = std::string(token);
      saw_property = true;
      continue;
    }

    setError(error, "invalid or duplicate transition token: " + std::string(token));
    return false;
  }

  if (lower(parsed.property) == "none") {
    setError(error, "'none' must be the entire transition shorthand");
    return false;
  }
  output = std::move(parsed);
  return true;
}

std::string formatNumber(double value) {
  if (isZero(value)) {
    value = 0.0;
  }
  std::ostringstream stream;
  stream << std::setprecision(12) << value;
  return stream.str();
}

std::string_view suffix(MotionUnit unit) {
  switch (unit) {
    case MotionUnit::Number: return {};
    case MotionUnit::Pixels: return "px";
    case MotionUnit::Percent: return "%";
    case MotionUnit::ViewportWidth: return "vw";
    case MotionUnit::ViewportHeight: return "vh";
    case MotionUnit::Em: return "em";
    case MotionUnit::Rem: return "rem";
    case MotionUnit::Fraction: return "fr";
    case MotionUnit::Degrees: return "deg";
  }
  return {};
}

double cubic(double t, double first, double second) {
  const double inverse = 1.0 - t;
  return 3.0 * inverse * inverse * t * first +
         3.0 * inverse * t * t * second + t * t * t;
}

double cubicDerivative(double t, double first, double second) {
  const double inverse = 1.0 - t;
  return 3.0 * inverse * inverse * first +
         6.0 * inverse * t * (second - first) +
         3.0 * t * t * (1.0 - second);
}

double cubicBezierAtX(double x,
                      double x1,
                      double y1,
                      double x2,
                      double y2) {
  if (x <= 0.0 || x >= 1.0) {
    return x <= 0.0 ? 0.0 : 1.0;
  }

  double parameter = x;
  for (int iteration = 0; iteration < 8; ++iteration) {
    const double difference = cubic(parameter, x1, x2) - x;
    const double derivative = cubicDerivative(parameter, x1, x2);
    if (std::abs(difference) < 1.0e-7 || std::abs(derivative) < 1.0e-8) {
      break;
    }
    const double candidate = parameter - difference / derivative;
    if (candidate < 0.0 || candidate > 1.0) {
      break;
    }
    parameter = candidate;
  }

  double low = 0.0;
  double high = 1.0;
  for (int iteration = 0; iteration < 18; ++iteration) {
    const double sampled_x = cubic(parameter, x1, x2);
    if (std::abs(sampled_x - x) < 1.0e-7) {
      break;
    }
    if (sampled_x < x) {
      low = parameter;
    } else {
      high = parameter;
    }
    parameter = (low + high) * 0.5;
  }
  return std::clamp(cubic(parameter, y1, y2), 0.0, 1.0);
}

bool equalValues(const MotionValue& left, const MotionValue& right) {
  if (left.index() != right.index()) {
    return false;
  }
  if (const auto* left_number = std::get_if<MotionNumber>(&left)) {
    const auto& right_number = std::get<MotionNumber>(right);
    return left_number->unit == right_number.unit &&
           std::abs(left_number->value - right_number.value) <= kValueEpsilon;
  }
  if (const auto* left_color = std::get_if<math::Color>(&left)) {
    const auto& right_color = std::get<math::Color>(right);
    return std::abs(left_color->r - right_color.r) <= kValueEpsilon &&
           std::abs(left_color->g - right_color.g) <= kValueEpsilon &&
           std::abs(left_color->b - right_color.b) <= kValueEpsilon &&
           std::abs(left_color->a - right_color.a) <= kValueEpsilon;
  }
  const auto& left_transform = std::get<paint::Transform>(left);
  const auto& right_transform = std::get<paint::Transform>(right);
  if (left_transform.operations.size() != right_transform.operations.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left_transform.operations.size(); ++index) {
    const paint::TransformOp& left_operation = left_transform.operations[index];
    const paint::TransformOp& right_operation = right_transform.operations[index];
    if (left_operation.kind != right_operation.kind ||
        left_operation.x.unit != right_operation.x.unit ||
        left_operation.y.unit != right_operation.y.unit ||
        std::abs(left_operation.x.value - right_operation.x.value) >
            kValueEpsilon ||
        std::abs(left_operation.y.value - right_operation.y.value) >
            kValueEpsilon ||
        std::abs(left_operation.angle_degrees - right_operation.angle_degrees) >
            kValueEpsilon) {
      return false;
    }
  }
  return true;
}

template <typename Parser, typename Value>
bool parseList(std::string_view source,
               std::string_view fallback,
               Parser parser,
               std::vector<Value>& output,
               std::string_view label,
               std::string* error) {
  if (trimView(source).empty()) {
    source = fallback;
  }
  std::vector<std::string_view> items;
  if (!splitTopLevel(source, ',', items, error)) {
    return false;
  }
  output.clear();
  output.reserve(items.size());
  for (const auto item : items) {
    if (item.empty()) {
      setError(error, "empty item in transition-" + std::string(label));
      return false;
    }
    const auto parsed = parser(item);
    if (!parsed) {
      setError(error, "invalid transition-" + std::string(label) + " item: " +
                          std::string(item));
      return false;
    }
    output.push_back(*parsed);
  }
  return !output.empty();
}

}  // namespace

std::optional<double> parseTimeSeconds(std::string_view source) {
  source = trimView(source);
  if (source.empty()) {
    return std::nullopt;
  }
  const std::string value = lower(source);
  double scale = 1.0;
  std::string_view number(value);
  if (value.size() > 2 && value.ends_with("ms")) {
    scale = 0.001;
    number.remove_suffix(2);
  } else if (value.size() > 1 && value.ends_with('s')) {
    number.remove_suffix(1);
  } else {
    double unitless = 0.0;
    if (!parseFiniteDouble(value, unitless) || !isZero(unitless)) {
      return std::nullopt;
    }
    return 0.0;
  }
  double parsed = 0.0;
  if (!parseFiniteDouble(number, parsed)) {
    return std::nullopt;
  }
  const double seconds = parsed * scale;
  return std::isfinite(seconds) ? std::optional<double>(seconds) : std::nullopt;
}

std::optional<Easing> parseEasing(std::string_view source) {
  const std::string value = lower(trimView(source));
  if (value == "linear") return Easing::Linear;
  if (value == "ease") return Easing::Ease;
  if (value == "ease-in") return Easing::EaseIn;
  if (value == "ease-out") return Easing::EaseOut;
  if (value == "ease-in-out") return Easing::EaseInOut;
  return std::nullopt;
}

double evaluateEasing(Easing easing, double progress) {
  progress = std::clamp(progress, 0.0, 1.0);
  switch (easing) {
    case Easing::Linear: return progress;
    case Easing::Ease: return cubicBezierAtX(progress, 0.25, 0.1, 0.25, 1.0);
    case Easing::EaseIn: return cubicBezierAtX(progress, 0.42, 0.0, 1.0, 1.0);
    case Easing::EaseOut: return cubicBezierAtX(progress, 0.0, 0.0, 0.58, 1.0);
    case Easing::EaseInOut:
      return cubicBezierAtX(progress, 0.42, 0.0, 0.58, 1.0);
  }
  return progress;
}

std::optional<MotionNumber> parseMotionNumber(std::string_view source) {
  source = trimView(source);
  if (source.empty()) {
    return std::nullopt;
  }
  const std::string value = lower(source);
  static constexpr std::array<std::pair<std::string_view, MotionUnit>, 9> units = {{
      {"rem", MotionUnit::Rem},
      {"deg", MotionUnit::Degrees},
      {"px", MotionUnit::Pixels},
      {"vw", MotionUnit::ViewportWidth},
      {"vh", MotionUnit::ViewportHeight},
      {"em", MotionUnit::Em},
      {"fr", MotionUnit::Fraction},
      {"%", MotionUnit::Percent},
      {"", MotionUnit::Number},
  }};
  for (const auto& [unit_suffix, unit] : units) {
    if (!value.ends_with(unit_suffix)) {
      continue;
    }
    const std::string_view number(value.data(), value.size() - unit_suffix.size());
    double parsed = 0.0;
    if (parseFiniteDouble(number, parsed)) {
      return MotionNumber{.value = parsed, .unit = unit};
    }
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<math::Color> parseMotionColor(std::string_view source) {
  const std::string value = lower(trimView(source));
  if (value.empty()) {
    return std::nullopt;
  }
  static const std::unordered_map<std::string, math::Color> named = {
      {"transparent", {0, 0, 0, 0}}, {"black", {0, 0, 0, 1}},
      {"white", {1, 1, 1, 1}},       {"red", {1, 0, 0, 1}},
      {"green", {0, 0.5f, 0, 1}},    {"blue", {0, 0, 1, 1}},
      {"gray", {0.5f, 0.5f, 0.5f, 1}},
      {"grey", {0.5f, 0.5f, 0.5f, 1}},
      {"yellow", {1, 1, 0, 1}},      {"magenta", {1, 0, 1, 1}},
      {"cyan", {0, 1, 1, 1}},
  };
  if (const auto found = named.find(value); found != named.end()) {
    return found->second;
  }

  if (value.front() == '#') {
    const std::string_view hex(value.data() + 1, value.size() - 1);
    auto nibble = [](char ch) -> int {
      if (ch >= '0' && ch <= '9') return ch - '0';
      if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
      return -1;
    };
    auto byte = [&](std::size_t offset) -> int {
      const int high = nibble(hex[offset]);
      const int low = nibble(hex[offset + 1]);
      return high < 0 || low < 0 ? -1 : high * 16 + low;
    };
    if (hex.size() == 3 || hex.size() == 4) {
      const int red = nibble(hex[0]);
      const int green = nibble(hex[1]);
      const int blue = nibble(hex[2]);
      const int alpha = hex.size() == 4 ? nibble(hex[3]) : 15;
      if (red >= 0 && green >= 0 && blue >= 0 && alpha >= 0) {
        return math::Color{red / 15.0f, green / 15.0f, blue / 15.0f,
                           alpha / 15.0f};
      }
    } else if (hex.size() == 6 || hex.size() == 8) {
      const int red = byte(0);
      const int green = byte(2);
      const int blue = byte(4);
      const int alpha = hex.size() == 8 ? byte(6) : 255;
      if (red >= 0 && green >= 0 && blue >= 0 && alpha >= 0) {
        return math::Color{red / 255.0f, green / 255.0f, blue / 255.0f,
                           alpha / 255.0f};
      }
    }
    return std::nullopt;
  }

  const bool rgba = value.starts_with("rgba(");
  if (!rgba && !value.starts_with("rgb(")) {
    return std::nullopt;
  }
  const std::size_t begin = value.find('(') + 1;
  const std::size_t end = value.rfind(')');
  if (end == std::string::npos || end != value.size() - 1 || end <= begin) {
    return std::nullopt;
  }
  std::vector<std::string_view> parts;
  if (!splitTopLevel(std::string_view(value).substr(begin, end - begin), ',', parts,
                     nullptr) ||
      parts.size() != (rgba ? 4u : 3u)) {
    return std::nullopt;
  }
  std::array<double, 4> channels{0.0, 0.0, 0.0, 1.0};
  for (std::size_t index = 0; index < parts.size(); ++index) {
    if (!parseFiniteDouble(parts[index], channels[index])) {
      return std::nullopt;
    }
  }
  const double scale = channels[0] > 1.0 || channels[1] > 1.0 || channels[2] > 1.0
                           ? 255.0
                           : 1.0;
  return math::Color{
      static_cast<float>(std::clamp(channels[0] / scale, 0.0, 1.0)),
      static_cast<float>(std::clamp(channels[1] / scale, 0.0, 1.0)),
      static_cast<float>(std::clamp(channels[2] / scale, 0.0, 1.0)),
      static_cast<float>(std::clamp(channels[3], 0.0, 1.0))};
}

std::optional<paint::Transform> parseMotionTransform(std::string_view source) {
  paint::Transform transform;
  if (!paint::parseTransform(source, transform)) {
    return std::nullopt;
  }
  return transform;
}

std::optional<MotionValue> parseMotionValue(std::string_view source) {
  if (const auto number = parseMotionNumber(source)) {
    return MotionValue(*number);
  }
  if (const auto color = parseMotionColor(source)) {
    return MotionValue(*color);
  }
  if (const auto transform = parseMotionTransform(source)) {
    return MotionValue(*transform);
  }
  return std::nullopt;
}

std::optional<MotionValue> interpolateMotionValue(const MotionValue& from,
                                                  const MotionValue& to,
                                                  double progress) {
  if (from.index() != to.index() || !std::isfinite(progress)) {
    return std::nullopt;
  }
  progress = std::clamp(progress, 0.0, 1.0);
  if (const auto* from_number = std::get_if<MotionNumber>(&from)) {
    const auto& to_number = std::get<MotionNumber>(to);
    if (from_number->unit != to_number.unit) {
      return std::nullopt;
    }
    return MotionValue(MotionNumber{
        .value = from_number->value + (to_number.value - from_number->value) * progress,
        .unit = from_number->unit});
  }
  if (const auto* from_color = std::get_if<math::Color>(&from)) {
    const auto& to_color = std::get<math::Color>(to);
    return MotionValue(math::Color{
        static_cast<float>(from_color->r + (to_color.r - from_color->r) * progress),
        static_cast<float>(from_color->g + (to_color.g - from_color->g) * progress),
        static_cast<float>(from_color->b + (to_color.b - from_color->b) * progress),
        static_cast<float>(from_color->a + (to_color.a - from_color->a) * progress)});
  }

  const auto& from_transform = std::get<paint::Transform>(from);
  const auto& to_transform = std::get<paint::Transform>(to);
  if (from_transform.operations.size() != to_transform.operations.size()) {
    return std::nullopt;
  }
  paint::Transform result;
  result.operations.reserve(from_transform.operations.size());
  for (std::size_t index = 0; index < from_transform.operations.size(); ++index) {
    const paint::TransformOp& from_operation = from_transform.operations[index];
    const paint::TransformOp& to_operation = to_transform.operations[index];
    if (from_operation.kind != to_operation.kind) {
      return std::nullopt;
    }
    paint::TransformOp operation = from_operation;
    switch (from_operation.kind) {
      case paint::TransformOpKind::Translate:
        if (from_operation.x.unit != to_operation.x.unit ||
            from_operation.y.unit != to_operation.y.unit) {
          return std::nullopt;
        }
        operation.x.value = static_cast<float>(
            from_operation.x.value +
            (to_operation.x.value - from_operation.x.value) * progress);
        operation.y.value = static_cast<float>(
            from_operation.y.value +
            (to_operation.y.value - from_operation.y.value) * progress);
        break;
      case paint::TransformOpKind::Scale:
        operation.x.value = static_cast<float>(
            from_operation.x.value +
            (to_operation.x.value - from_operation.x.value) * progress);
        operation.y.value = static_cast<float>(
            from_operation.y.value +
            (to_operation.y.value - from_operation.y.value) * progress);
        break;
      case paint::TransformOpKind::Rotate:
        operation.angle_degrees = static_cast<float>(
            from_operation.angle_degrees +
            (to_operation.angle_degrees - from_operation.angle_degrees) * progress);
        break;
    }
    result.operations.push_back(operation);
  }
  return MotionValue(std::move(result));
}

std::string serializeMotionValue(const MotionValue& value) {
  if (const auto* number = std::get_if<MotionNumber>(&value)) {
    return formatNumber(number->value) + std::string(suffix(number->unit));
  }
  if (const auto* color = std::get_if<math::Color>(&value)) {
    return "rgba(" +
           formatNumber(std::clamp(static_cast<double>(color->r), 0.0, 1.0) *
                        255.0) +
           ", " +
           formatNumber(std::clamp(static_cast<double>(color->g), 0.0, 1.0) *
                        255.0) +
           ", " +
           formatNumber(std::clamp(static_cast<double>(color->b), 0.0, 1.0) *
                        255.0) +
           ", " +
           formatNumber(std::clamp(static_cast<double>(color->a), 0.0, 1.0)) +
           ")";
  }

  const auto& transform = std::get<paint::Transform>(value);
  if (transform.operations.empty()) {
    return "none";
  }
  std::string serialized;
  for (const paint::TransformOp& operation : transform.operations) {
    if (!serialized.empty()) serialized.push_back(' ');
    switch (operation.kind) {
      case paint::TransformOpKind::Translate: {
        const auto length = [](paint::TransformLength component) {
          return formatNumber(component.value) +
                 (component.unit == paint::LengthUnit::Percent ? "%" : "px");
        };
        serialized += "translate(" + length(operation.x) + ", " +
                      length(operation.y) + ")";
        break;
      }
      case paint::TransformOpKind::Scale:
        serialized += "scale(" + formatNumber(operation.x.value) + ", " +
                      formatNumber(operation.y.value) + ")";
        break;
      case paint::TransformOpKind::Rotate:
        serialized += "rotate(" + formatNumber(operation.angle_degrees) + "deg)";
        break;
    }
  }
  return serialized;
}

bool parseTransitionShorthand(std::string_view source,
                              std::vector<TransitionSpec>& output,
                              std::string* error) {
  source = trimView(source);
  if (source.empty()) {
    setError(error, "transition shorthand is empty");
    return false;
  }
  if (lower(source) == "none") {
    output.clear();
    if (error != nullptr) error->clear();
    return true;
  }
  std::vector<std::string_view> items;
  if (!splitTopLevel(source, ',', items, error)) {
    return false;
  }
  std::vector<TransitionSpec> parsed;
  parsed.reserve(items.size());
  for (const auto item : items) {
    TransitionSpec spec;
    if (!parseTransitionItem(item, spec, error)) {
      return false;
    }
    parsed.push_back(std::move(spec));
  }
  output = std::move(parsed);
  if (error != nullptr) error->clear();
  return true;
}

bool parseTransitionLonghands(std::string_view properties,
                              std::string_view durations,
                              std::string_view easing_functions,
                              std::string_view delays,
                              std::vector<TransitionSpec>& output,
                              std::string* error) {
  if (trimView(properties).empty()) {
    properties = "all";
  }
  std::vector<std::string_view> property_items;
  if (!splitTopLevel(properties, ',', property_items, error)) {
    return false;
  }
  std::vector<std::string> parsed_properties;
  parsed_properties.reserve(property_items.size());
  for (const auto property : property_items) {
    if (!validProperty(property)) {
      setError(error, "invalid transition-property item: " + std::string(property));
      return false;
    }
    parsed_properties.emplace_back(property);
  }
  if (parsed_properties.size() == 1 && lower(parsed_properties.front()) == "none") {
    output.clear();
    if (error != nullptr) error->clear();
    return true;
  }
  if (std::any_of(parsed_properties.begin(), parsed_properties.end(), [](const auto& item) {
        return lower(item) == "none";
      })) {
    setError(error, "'none' cannot be combined with other transition properties");
    return false;
  }

  std::vector<double> parsed_durations;
  if (!parseList(durations, "0s", parseTimeSeconds, parsed_durations, "duration",
                 error)) {
    return false;
  }
  if (std::any_of(parsed_durations.begin(), parsed_durations.end(),
                  [](double duration) { return duration < 0.0; })) {
    setError(error, "transition duration cannot be negative");
    return false;
  }
  std::vector<Easing> parsed_easings;
  if (!parseList(easing_functions, "ease", parseEasing, parsed_easings,
                 "timing-function", error)) {
    return false;
  }
  std::vector<double> parsed_delays;
  if (!parseList(delays, "0s", parseTimeSeconds, parsed_delays, "delay", error)) {
    return false;
  }

  std::vector<TransitionSpec> parsed;
  parsed.reserve(parsed_properties.size());
  for (std::size_t index = 0; index < parsed_properties.size(); ++index) {
    parsed.push_back(TransitionSpec{
        .property = parsed_properties[index],
        .duration_seconds = parsed_durations[index % parsed_durations.size()],
        .delay_seconds = parsed_delays[index % parsed_delays.size()],
        .easing = parsed_easings[index % parsed_easings.size()]});
  }
  output = std::move(parsed);
  if (error != nullptr) error->clear();
  return true;
}

const TransitionSpec* findTransition(const std::vector<TransitionSpec>& transitions,
                                     std::string_view property) {
  const TransitionSpec* all = nullptr;
  for (auto iterator = transitions.rbegin(); iterator != transitions.rend(); ++iterator) {
    if (iterator->property == property) {
      return &*iterator;
    }
    if (lower(iterator->property) == "all" && all == nullptr) {
      all = &*iterator;
    }
  }
  return all;
}

TransitionTrack::TransitionTrack(MotionValue initial_value)
    : from_(initial_value), target_(std::move(initial_value)) {}

RetargetResult TransitionTrack::retarget(MotionValue target,
                                         const TransitionSpec& spec,
                                         double now_seconds,
                                         double motion_scale) {
  if (!std::isfinite(now_seconds) || !std::isfinite(motion_scale) ||
      motion_scale <= 0.0 || !std::isfinite(spec.duration_seconds) ||
      !std::isfinite(spec.delay_seconds) || spec.duration_seconds <= 0.0) {
    const bool unchanged = equalValues(target_, target) &&
                           equalValues(valueAt(now_seconds), target);
    reset(std::move(target));
    return unchanged ? RetargetResult::Unchanged : RetargetResult::AppliedImmediately;
  }
  if (equalValues(target_, target)) {
    return RetargetResult::Unchanged;
  }

  MotionValue current = valueAt(now_seconds);
  if (equalValues(current, target) ||
      !interpolateMotionValue(current, target, 0.0).has_value()) {
    reset(std::move(target));
    return equalValues(current, target) ? RetargetResult::Unchanged
                                        : RetargetResult::AppliedImmediately;
  }

  from_ = std::move(current);
  target_ = std::move(target);
  start_seconds_ = now_seconds;
  duration_seconds_ = spec.duration_seconds * motion_scale;
  delay_seconds_ = spec.delay_seconds * motion_scale;
  easing_ = spec.easing;
  running_ = std::isfinite(duration_seconds_) && std::isfinite(delay_seconds_) &&
             duration_seconds_ > 0.0 && delay_seconds_ + duration_seconds_ > 0.0;
  if (!running_) {
    from_ = target_;
    return RetargetResult::AppliedImmediately;
  }
  return RetargetResult::Started;
}

MotionValue TransitionTrack::valueAt(double now_seconds) const {
  if (!running_ || !std::isfinite(now_seconds)) {
    return target_;
  }
  const double progress =
      (now_seconds - start_seconds_ - delay_seconds_) / duration_seconds_;
  if (progress <= 0.0) {
    return from_;
  }
  if (progress >= 1.0) {
    return target_;
  }
  return interpolateMotionValue(from_, target_, evaluateEasing(easing_, progress))
      .value_or(target_);
}

bool TransitionTrack::active(double now_seconds) const {
  if (!running_ || !std::isfinite(now_seconds)) {
    return false;
  }
  return now_seconds < start_seconds_ + delay_seconds_ + duration_seconds_;
}

void TransitionTrack::reset(MotionValue value) {
  from_ = value;
  target_ = std::move(value);
  start_seconds_ = 0.0;
  duration_seconds_ = 0.0;
  delay_seconds_ = 0.0;
  easing_ = Easing::Ease;
  running_ = false;
}

namespace {

bool stripComments(std::string_view source, std::string& output, std::string* error) {
  output.clear();
  output.reserve(source.size());
  std::size_t cursor = 0;
  while (cursor < source.size()) {
    const std::size_t begin = source.find("/*", cursor);
    if (begin == std::string_view::npos) {
      output.append(source.substr(cursor));
      break;
    }
    output.append(source.substr(cursor, begin - cursor));
    const std::size_t end = source.find("*/", begin + 2);
    if (end == std::string_view::npos) {
      setError(error, "unterminated comment while parsing @keyframes");
      return false;
    }
    output.append(end + 2 - begin, ' ');
    cursor = end + 2;
  }
  return true;
}

std::size_t matchingBrace(std::string_view source, std::size_t open) {
  int depth = 0;
  char quote = '\0';
  bool escaped = false;
  for (std::size_t index = open; index < source.size(); ++index) {
    const char ch = source[index];
    if (quote != '\0') {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == quote) {
        quote = '\0';
      }
      continue;
    }
    if (ch == '\'' || ch == '"') {
      quote = ch;
    } else if (ch == '{') {
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0) return index;
      if (depth < 0) return std::string_view::npos;
    }
  }
  return std::string_view::npos;
}

std::optional<double> parseKeyframeOffset(std::string_view source) {
  const std::string value = lower(trimView(source));
  if (value == "from") return 0.0;
  if (value == "to") return 1.0;
  if (value.size() < 2 || !value.ends_with('%')) return std::nullopt;
  double percentage = 0.0;
  if (!parseFiniteDouble(std::string_view(value).substr(0, value.size() - 1),
                         percentage) ||
      percentage < 0.0 || percentage > 100.0) {
    return std::nullopt;
  }
  return percentage * 0.01;
}

bool parseFrameDeclarations(std::string_view source,
                            KeyframeDeclarations& output,
                            std::string* error) {
  KeyframeDeclarations parsed;
  std::size_t begin = 0;
  int parentheses = 0;
  char quote = '\0';
  bool escaped = false;
  for (std::size_t cursor = 0; cursor <= source.size(); ++cursor) {
    const char ch = cursor < source.size() ? source[cursor] : ';';
    if (quote != '\0') {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == quote) {
        quote = '\0';
      }
    } else if (ch == '\'' || ch == '"') {
      quote = ch;
    } else if (ch == '(') {
      ++parentheses;
    } else if (ch == ')') {
      --parentheses;
      if (parentheses < 0) {
        setError(error, "unexpected ')' in @keyframes declaration");
        return false;
      }
    }
    if (cursor != source.size() &&
        (ch != ';' || parentheses != 0 || quote != '\0')) {
      continue;
    }

    const std::string_view declaration = trimView(source.substr(begin, cursor - begin));
    begin = cursor + 1;
    if (declaration.empty()) continue;

    int value_parentheses = 0;
    char value_quote = '\0';
    bool value_escaped = false;
    std::size_t colon = std::string_view::npos;
    for (std::size_t index = 0; index < declaration.size(); ++index) {
      const char declaration_ch = declaration[index];
      if (value_quote != '\0') {
        if (value_escaped) value_escaped = false;
        else if (declaration_ch == '\\') value_escaped = true;
        else if (declaration_ch == value_quote) value_quote = '\0';
      } else if (declaration_ch == '\'' || declaration_ch == '"') {
        value_quote = declaration_ch;
      } else if (declaration_ch == '(') {
        ++value_parentheses;
      } else if (declaration_ch == ')') {
        --value_parentheses;
      } else if (declaration_ch == ':' && value_parentheses == 0) {
        colon = index;
        break;
      }
    }
    if (colon == std::string_view::npos) {
      setError(error, "@keyframes declaration is missing ':'");
      return false;
    }
    const std::string_view property_source = trimView(declaration.substr(0, colon));
    const std::string_view value_source = trimView(declaration.substr(colon + 1));
    if (!validProperty(property_source) || value_source.empty()) {
      setError(error, "invalid @keyframes declaration");
      return false;
    }
    const std::string property = lower(property_source);
    const std::string lowered_value = lower(value_source);
    if (lowered_value.size() >= 10 && lowered_value.ends_with("!important")) {
      setError(error, "!important is not valid inside @keyframes");
      return false;
    }
    std::optional<MotionValue> motion_value;
    if (property == "transform") {
      if (const auto transform = parseMotionTransform(value_source)) {
        motion_value = MotionValue(*transform);
      }
    } else if (const auto number = parseMotionNumber(value_source)) {
      motion_value = MotionValue(*number);
    } else if (const auto color = parseMotionColor(value_source)) {
      motion_value = MotionValue(*color);
    }
    parsed[property] = KeyframeDeclaration{
        .source_value = std::string(value_source),
        .motion_value = std::move(motion_value)};
  }
  if (quote != '\0' || parentheses != 0) {
    setError(error, "unterminated value in @keyframes declaration");
    return false;
  }
  output = std::move(parsed);
  return true;
}

bool parseKeyframeBlock(std::string_view name,
                        std::string_view block,
                        Keyframes& output,
                        std::string* error) {
  std::map<double, KeyframeDeclarations> normalized;
  std::size_t cursor = 0;
  while (cursor < block.size()) {
    while (cursor < block.size() &&
           std::isspace(static_cast<unsigned char>(block[cursor])) != 0) {
      ++cursor;
    }
    if (cursor == block.size()) break;
    const std::size_t open = block.find('{', cursor);
    if (open == std::string_view::npos) {
      setError(error, "@keyframes selector is missing a declaration block");
      return false;
    }
    const std::string_view selectors = trimView(block.substr(cursor, open - cursor));
    const std::size_t close = matchingBrace(block, open);
    if (close == std::string_view::npos) {
      setError(error, "unclosed declaration block inside @keyframes");
      return false;
    }
    KeyframeDeclarations declarations;
    if (!parseFrameDeclarations(block.substr(open + 1, close - open - 1), declarations,
                                error)) {
      return false;
    }
    std::vector<std::string_view> selector_items;
    if (!splitTopLevel(selectors, ',', selector_items, error) ||
        selector_items.empty()) {
      setError(error, "@keyframes frame requires an offset selector");
      return false;
    }
    for (const std::string_view selector : selector_items) {
      const auto offset = parseKeyframeOffset(selector);
      if (!offset) {
        setError(error, "invalid @keyframes offset: " + std::string(selector));
        return false;
      }
      auto& destination = normalized[*offset];
      for (const auto& [property, declaration] : declarations) {
        destination[property] = declaration;
      }
    }
    cursor = close + 1;
  }
  if (normalized.empty()) {
    setError(error, "@keyframes block contains no frames");
    return false;
  }
  if (normalized.begin()->first > 0.0) {
    normalized.emplace(0.0, normalized.begin()->second);
  }
  if (normalized.rbegin()->first < 1.0) {
    normalized.emplace(1.0, normalized.rbegin()->second);
  }

  Keyframes parsed;
  parsed.name = std::string(name);
  parsed.frames.reserve(normalized.size());
  for (auto& [offset, declarations] : normalized) {
    parsed.frames.push_back(
        Keyframe{.offset = offset, .declarations = std::move(declarations)});
  }
  output = std::move(parsed);
  return true;
}

bool reverseForIteration(AnimationDirection direction, double iteration) {
  const bool odd = std::fmod(std::max(0.0, iteration), 2.0) >= 1.0;
  switch (direction) {
    case AnimationDirection::Normal: return false;
    case AnimationDirection::Reverse: return true;
    case AnimationDirection::Alternate: return odd;
    case AnimationDirection::AlternateReverse: return !odd;
  }
  return false;
}

double directedOffset(AnimationDirection direction,
                      double iteration,
                      double iteration_progress) {
  iteration_progress = std::clamp(iteration_progress, 0.0, 1.0);
  return reverseForIteration(direction, iteration) ? 1.0 - iteration_progress
                                                    : iteration_progress;
}

std::uint64_t iterationIndex(double iteration) {
  if (!std::isfinite(iteration) || iteration <= 0.0) return 0;
  constexpr double maximum =
      static_cast<double>(std::numeric_limits<std::uint64_t>::max());
  return iteration >= maximum ? std::numeric_limits<std::uint64_t>::max()
                              : static_cast<std::uint64_t>(iteration);
}

struct AnimationPosition {
  double iteration = 0.0;
  double offset = 0.0;
};

AnimationPosition positionWithin(double elapsed,
                                 double duration,
                                 AnimationDirection direction) {
  const double overall = std::max(0.0, elapsed / duration);
  double whole = 0.0;
  double fraction = std::modf(overall, &whole);
  if (overall > 0.0 && std::abs(fraction) <= kValueEpsilon) {
    whole = std::max(0.0, whole - 1.0);
    fraction = 1.0;
  }
  return {.iteration = whole,
          .offset = directedOffset(direction, whole, fraction)};
}

AnimationPosition finalPosition(double iteration_count,
                                AnimationDirection direction,
                                bool infinite) {
  if (infinite) {
    return {.iteration = 0.0, .offset = directedOffset(direction, 0.0, 1.0)};
  }
  if (iteration_count <= 0.0) {
    return {.iteration = 0.0, .offset = directedOffset(direction, 0.0, 0.0)};
  }
  double whole = 0.0;
  double fraction = std::modf(iteration_count, &whole);
  if (std::abs(fraction) <= kValueEpsilon) {
    whole = std::max(0.0, whole - 1.0);
    fraction = 1.0;
  }
  return {.iteration = whole,
          .offset = directedOffset(direction, whole, fraction)};
}

bool fillsBackwards(AnimationFillMode mode) {
  return mode == AnimationFillMode::Backwards || mode == AnimationFillMode::Both;
}

bool fillsForwards(AnimationFillMode mode) {
  return mode == AnimationFillMode::Forwards || mode == AnimationFillMode::Both;
}

AnimationSample makeSample(const Keyframes& keyframes,
                           const AnimationSpec& spec,
                           AnimationPhase phase,
                           bool contributes,
                           bool finished,
                           const AnimationPosition& position) {
  AnimationSample sample;
  sample.phase = phase;
  sample.contributes = contributes;
  sample.finished = finished;
  sample.iteration = iterationIndex(position.iteration);
  sample.offset = position.offset;
  if (contributes) {
    sample.values = sampleKeyframesAtOffset(keyframes, position.offset, spec.easing);
  }
  return sample;
}

}  // namespace

bool parseKeyframes(std::string_view source,
                    std::vector<Keyframes>& output,
                    std::string* error) {
  std::string cleaned;
  if (!stripComments(source, cleaned, error)) return false;
  const std::string lowered = lower(cleaned);
  std::vector<Keyframes> parsed;
  std::size_t cursor = 0;
  while ((cursor = lowered.find("@keyframes", cursor)) != std::string::npos) {
    constexpr std::size_t keyword_size = 10;
    const std::size_t after_keyword = cursor + keyword_size;
    if (after_keyword < lowered.size() &&
        std::isspace(static_cast<unsigned char>(lowered[after_keyword])) == 0) {
      cursor = after_keyword;
      continue;
    }
    std::size_t name_begin = after_keyword;
    while (name_begin < cleaned.size() &&
           std::isspace(static_cast<unsigned char>(cleaned[name_begin])) != 0) {
      ++name_begin;
    }
    std::size_t name_end = name_begin;
    while (name_end < cleaned.size() &&
           std::isspace(static_cast<unsigned char>(cleaned[name_end])) == 0 &&
           cleaned[name_end] != '{') {
      ++name_end;
    }
    const std::string_view name = cleaned.empty()
                                      ? std::string_view{}
                                      : trimView(std::string_view(cleaned).substr(
                                            name_begin, name_end - name_begin));
    if (!validProperty(name) || lower(name) == "none") {
      setError(error, "invalid @keyframes name: " + std::string(name));
      return false;
    }
    std::size_t open = name_end;
    while (open < cleaned.size() &&
           std::isspace(static_cast<unsigned char>(cleaned[open])) != 0) {
      ++open;
    }
    if (open == cleaned.size() || cleaned[open] != '{') {
      setError(error, "@keyframes name must be followed by '{'");
      return false;
    }
    const std::size_t close = matchingBrace(cleaned, open);
    if (close == std::string_view::npos) {
      setError(error, "unclosed @keyframes block");
      return false;
    }
    Keyframes keyframes;
    if (!parseKeyframeBlock(name,
                            std::string_view(cleaned).substr(open + 1,
                                                             close - open - 1),
                            keyframes, error)) {
      return false;
    }
    parsed.push_back(std::move(keyframes));
    cursor = close + 1;
  }
  output = std::move(parsed);
  if (error != nullptr) error->clear();
  return true;
}

const Keyframes* findKeyframes(const std::vector<Keyframes>& keyframes,
                               std::string_view name) {
  for (auto iterator = keyframes.rbegin(); iterator != keyframes.rend(); ++iterator) {
    if (iterator->name == name) return &*iterator;
  }
  return nullptr;
}

MotionProperties sampleKeyframesAtOffset(const Keyframes& keyframes,
                                         double offset,
                                         Easing easing) {
  MotionProperties sampled;
  if (keyframes.frames.empty() || !std::isfinite(offset)) return sampled;
  offset = std::clamp(offset, 0.0, 1.0);

  std::unordered_map<std::string, bool> properties;
  for (const Keyframe& frame : keyframes.frames) {
    for (const auto& [property, declaration] : frame.declarations) {
      if (declaration.motion_value) properties[property] = true;
    }
  }
  for (const auto& [property, unused] : properties) {
    (void)unused;
    const MotionValue* before = nullptr;
    const MotionValue* after = nullptr;
    double before_offset = 0.0;
    double after_offset = 1.0;
    for (const Keyframe& frame : keyframes.frames) {
      const auto declaration = frame.declarations.find(property);
      if (declaration == frame.declarations.end() ||
          !declaration->second.motion_value) {
        continue;
      }
      if (frame.offset <= offset) {
        before = &*declaration->second.motion_value;
        before_offset = frame.offset;
      }
      if (frame.offset >= offset) {
        after = &*declaration->second.motion_value;
        after_offset = frame.offset;
        break;
      }
    }
    if (before == nullptr && after == nullptr) continue;
    if (before == nullptr) before = after;
    if (after == nullptr) after = before;
    if (before == after || std::abs(after_offset - before_offset) <= kValueEpsilon) {
      sampled[property] = *after;
      continue;
    }
    const double local = std::clamp((offset - before_offset) /
                                        (after_offset - before_offset),
                                    0.0, 1.0);
    const double eased = evaluateEasing(easing, local);
    if (const auto value = interpolateMotionValue(*before, *after, eased)) {
      sampled[property] = *value;
    } else {
      sampled[property] = eased < 0.5 ? *before : *after;
    }
  }
  return sampled;
}

std::optional<double> parseAnimationIterationCount(std::string_view source) {
  const std::string value = lower(trimView(source));
  if (value == "infinite") return std::numeric_limits<double>::infinity();
  double count = 0.0;
  if (!parseFiniteDouble(value, count) || count < 0.0) return std::nullopt;
  return count;
}

std::optional<AnimationDirection> parseAnimationDirection(std::string_view source) {
  const std::string value = lower(trimView(source));
  if (value == "normal") return AnimationDirection::Normal;
  if (value == "reverse") return AnimationDirection::Reverse;
  if (value == "alternate") return AnimationDirection::Alternate;
  if (value == "alternate-reverse") return AnimationDirection::AlternateReverse;
  return std::nullopt;
}

std::optional<AnimationFillMode> parseAnimationFillMode(std::string_view source) {
  const std::string value = lower(trimView(source));
  if (value == "none") return AnimationFillMode::None;
  if (value == "forwards") return AnimationFillMode::Forwards;
  if (value == "backwards") return AnimationFillMode::Backwards;
  if (value == "both") return AnimationFillMode::Both;
  return std::nullopt;
}

AnimationSample sampleAnimation(const Keyframes& keyframes,
                                const AnimationSpec& spec,
                                double start_seconds,
                                double now_seconds,
                                double motion_scale) {
  const bool infinite = std::isinf(spec.iteration_count) && spec.iteration_count > 0.0;
  const bool valid_count = infinite ||
                           (std::isfinite(spec.iteration_count) &&
                            spec.iteration_count >= 0.0);
  if (keyframes.frames.empty() || !valid_count || !std::isfinite(start_seconds) ||
      !std::isfinite(now_seconds) || !std::isfinite(spec.duration_seconds) ||
      spec.duration_seconds < 0.0 || !std::isfinite(spec.delay_seconds)) {
    AnimationSample invalid;
    invalid.finished = true;
    return invalid;
  }

  const AnimationPosition final =
      finalPosition(spec.iteration_count, spec.direction, infinite);
  if (!std::isfinite(motion_scale) || motion_scale <= 0.0) {
    return makeSample(keyframes, spec, AnimationPhase::After, true, true, final);
  }

  const double duration = spec.duration_seconds * motion_scale;
  const double delay = spec.delay_seconds * motion_scale;
  const double elapsed = now_seconds - start_seconds - delay;
  const AnimationPosition initial{
      .iteration = 0.0,
      .offset = directedOffset(spec.direction, 0.0, 0.0)};
  if (elapsed < 0.0) {
    return makeSample(keyframes, spec, AnimationPhase::Before,
                      fillsBackwards(spec.fill_mode), false, initial);
  }

  if (duration <= 0.0 || spec.iteration_count == 0.0) {
    return makeSample(keyframes, spec, AnimationPhase::After,
                      fillsForwards(spec.fill_mode), true, final);
  }
  const double active_duration =
      infinite ? std::numeric_limits<double>::infinity()
               : duration * spec.iteration_count;
  if (!infinite && elapsed >= active_duration) {
    return makeSample(keyframes, spec, AnimationPhase::After,
                      fillsForwards(spec.fill_mode), true, final);
  }

  const AnimationPosition current = positionWithin(elapsed, duration, spec.direction);
  return makeSample(keyframes, spec, AnimationPhase::Active, true, false, current);
}

}  // namespace karma::ui::native
