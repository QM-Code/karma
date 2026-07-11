#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace karma::assets::detail {

/// A position in the original UTF-8 source. Lines and columns are one-based;
/// offset is a zero-based byte offset. Columns count Unicode scalar values,
/// rather than UTF-8 code units.
struct JsonSourceLocation {
  std::size_t offset = 0;
  std::size_t line = 1;
  std::size_t column = 1;

  friend constexpr bool operator==(JsonSourceLocation,
                                   JsonSourceLocation) = default;
};

/// A half-open source range: begin is inclusive and end is exclusive.
struct JsonSourceSpan {
  JsonSourceLocation begin;
  JsonSourceLocation end;

  friend constexpr bool operator==(const JsonSourceSpan&,
                                   const JsonSourceSpan&) = default;
};

/// Source ranges associated with one value. Object members also retain the
/// range of the quoted or unquoted source key that introduced the value.
struct JsonValueSource {
  JsonSourceSpan value;
  std::optional<JsonSourceSpan> key;
};

struct JsonProfileDocument {
  nlohmann::json value;

  /// Deterministic, minified, strict JSON. Object keys are sorted by the
  /// nlohmann::json object representation and every emitted number is finite.
  std::string canonical_json;

  /// Values indexed by RFC 6901 JSON Pointer. The root uses the empty string.
  std::unordered_map<std::string, JsonValueSource> source_map;

  [[nodiscard]] const JsonValueSource* sourceFor(
      std::string_view json_pointer) const;
};

struct JsonProfileParseError {
  std::string message;
  JsonSourceLocation location;
};

struct JsonProfileParseResult {
  std::optional<JsonProfileDocument> document;
  std::optional<JsonProfileParseError> error;

  [[nodiscard]] explicit operator bool() const noexcept {
    return document.has_value();
  }
};

/// Parses Karma's deterministic JSON5 authoring profile.
///
/// The accepted conveniences are line/block comments, trailing commas,
/// single-quoted strings, and unquoted ASCII identifier keys. All values and
/// escapes otherwise follow strict JSON: NaN/Infinity, hexadecimal numbers,
/// leading plus signs, leading/trailing decimal points, numeric separators,
/// JSON5 escape extensions, and duplicate object keys are rejected. A UTF-8
/// BOM is accepted only at the beginning of the source.
[[nodiscard]] JsonProfileParseResult parseJsonProfile(std::string_view source);

}  // namespace karma::assets::detail
