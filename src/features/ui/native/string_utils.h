#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace karma::ui::native::string_utils {

[[nodiscard]] inline std::string trim(std::string_view input) {
  std::size_t first = 0u;
  while (first < input.size() &&
         std::isspace(static_cast<unsigned char>(input[first])) != 0) {
    ++first;
  }
  std::size_t last = input.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(input[last - 1u])) != 0) {
    --last;
  }
  return std::string(input.substr(first, last - first));
}

[[nodiscard]] inline std::string lower(std::string_view input) {
  std::string output(input);
  std::transform(output.begin(), output.end(), output.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return output;
}

[[nodiscard]] inline std::string unquote(std::string_view input) {
  std::string output = trim(input);
  if (output.size() >= 2u &&
      ((output.front() == '"' && output.back() == '"') ||
       (output.front() == '\'' && output.back() == '\''))) {
    output = output.substr(1u, output.size() - 2u);
  }
  return output;
}

/// Parses one complete, finite, whitespace-trimmed number. Failure never
/// changes caller state, unlike the former repeated out-parameter helpers.
[[nodiscard]] inline std::optional<double> parseFiniteDouble(
    std::string_view input) {
  const std::string owned = trim(input);
  if (owned.empty()) return std::nullopt;
  char* end = nullptr;
  const double value = std::strtod(owned.c_str(), &end);
  return end == owned.c_str() + owned.size() && std::isfinite(value)
             ? std::optional<double>{value}
             : std::nullopt;
}

}  // namespace karma::ui::native::string_utils
