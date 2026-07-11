#include "content/assets/ui_json_profile.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace karma::assets::detail {
namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumNestingDepth = 256;

struct Utf8DecodeResult {
  bool valid = false;
  std::uint32_t codepoint = 0;
  std::size_t width = 1;
};

Utf8DecodeResult decodeUtf8(std::string_view source, std::size_t offset) {
  if (offset >= source.size()) {
    return {};
  }

  const auto byte = [&](std::size_t index) {
    return static_cast<std::uint8_t>(source[index]);
  };
  const std::uint8_t lead = byte(offset);
  if (lead <= 0x7fU) {
    return {.valid = true, .codepoint = lead, .width = 1};
  }

  std::size_t width = 0;
  std::uint32_t codepoint = 0;
  std::uint32_t minimum = 0;
  if (lead >= 0xc2U && lead <= 0xdfU) {
    width = 2;
    codepoint = lead & 0x1fU;
    minimum = 0x80U;
  } else if (lead >= 0xe0U && lead <= 0xefU) {
    width = 3;
    codepoint = lead & 0x0fU;
    minimum = 0x800U;
  } else if (lead >= 0xf0U && lead <= 0xf4U) {
    width = 4;
    codepoint = lead & 0x07U;
    minimum = 0x10000U;
  } else {
    return {};
  }

  if (offset + width > source.size()) {
    return {};
  }
  for (std::size_t index = 1; index < width; ++index) {
    const std::uint8_t continuation = byte(offset + index);
    if ((continuation & 0xc0U) != 0x80U) {
      return {};
    }
    codepoint = (codepoint << 6U) | (continuation & 0x3fU);
  }

  if (codepoint < minimum || codepoint > 0x10ffffU ||
      (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
    return {};
  }
  return {.valid = true, .codepoint = codepoint, .width = width};
}

JsonSourceLocation locationAt(std::string_view source,
                              std::size_t requested_offset) {
  JsonSourceLocation location;
  const std::size_t target = std::min(requested_offset, source.size());
  while (location.offset < target) {
    const unsigned char ch =
        static_cast<unsigned char>(source[location.offset]);
    if (ch == '\r') {
      ++location.offset;
      if (location.offset < target && source[location.offset] == '\n') {
        ++location.offset;
      }
      ++location.line;
      location.column = 1;
      continue;
    }
    if (ch == '\n') {
      ++location.offset;
      ++location.line;
      location.column = 1;
      continue;
    }

    const Utf8DecodeResult decoded = decodeUtf8(source, location.offset);
    location.offset += decoded.valid ? decoded.width : 1;
    ++location.column;
  }
  location.offset = target;
  return location;
}

std::optional<JsonProfileParseError> validateUtf8(std::string_view source) {
  std::size_t offset = 0;
  while (offset < source.size()) {
    const Utf8DecodeResult decoded = decodeUtf8(source, offset);
    if (!decoded.valid) {
      return JsonProfileParseError{
          .message = "source is not valid UTF-8",
          .location = locationAt(source, offset),
      };
    }
    offset += decoded.width;
  }
  return std::nullopt;
}

bool isIdentifierStart(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' ||
         ch == '$';
}

bool isIdentifierContinue(char ch) {
  return isIdentifierStart(ch) || (ch >= '0' && ch <= '9');
}

bool isJsonWhitespace(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool isDigit(char ch) { return ch >= '0' && ch <= '9'; }

bool isHexDigit(char ch) {
  return isDigit(ch) || (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}

std::uint32_t hexValue(char ch) {
  if (isDigit(ch)) {
    return static_cast<std::uint32_t>(ch - '0');
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10U + static_cast<std::uint32_t>(ch - 'a');
  }
  return 10U + static_cast<std::uint32_t>(ch - 'A');
}

void appendUtf8(std::uint32_t codepoint, std::string& output) {
  if (codepoint <= 0x7fU) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ffU) {
    output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
    output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
  } else if (codepoint <= 0xffffU) {
    output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
    output.push_back(
        static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
  } else {
    output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
    output.push_back(
        static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
    output.push_back(
        static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
  }
}

std::string appendJsonPointer(std::string_view parent, std::string_view token) {
  std::string result(parent);
  result.push_back('/');
  for (const char ch : token) {
    if (ch == '~') {
      result += "~0";
    } else if (ch == '/') {
      result += "~1";
    } else {
      result.push_back(ch);
    }
  }
  return result;
}

class Parser {
 public:
  explicit Parser(std::string_view source) : source_(source) {
    if (source_.size() >= 3 &&
        static_cast<unsigned char>(source_[0]) == 0xefU &&
        static_cast<unsigned char>(source_[1]) == 0xbbU &&
        static_cast<unsigned char>(source_[2]) == 0xbfU) {
      offset_ = 3;
      location_.offset = 3;
    }
  }

  JsonProfileParseResult parse() {
    if (!skipTrivia()) {
      return failureResult();
    }
    if (atEnd()) {
      fail("expected a JSON value");
      return failureResult();
    }

    Json root;
    if (!parseValue(root, "", 0)) {
      return failureResult();
    }
    if (!skipTrivia()) {
      return failureResult();
    }
    if (!atEnd()) {
      fail("unexpected characters after the root value");
      return failureResult();
    }

    JsonProfileDocument document;
    document.value = std::move(root);
    document.canonical_json = document.value.dump();
    document.source_map = std::move(source_map_);
    return JsonProfileParseResult{
        .document = std::move(document),
        .error = std::nullopt,
    };
  }

 private:
  bool parseValue(Json& output,
                  const std::string& pointer,
                  std::size_t depth) {
    if (!skipTrivia()) {
      return false;
    }
    if (atEnd()) {
      return fail("expected a JSON value");
    }
    if (depth > kMaximumNestingDepth) {
      return fail("maximum JSON nesting depth exceeded");
    }

    const JsonSourceLocation begin = location_;
    bool parsed = false;
    const char ch = peek();
    if (ch == '{') {
      parsed = parseObject(output, pointer, depth);
    } else if (ch == '[') {
      parsed = parseArray(output, pointer, depth);
    } else if (ch == '"' || ch == '\'') {
      std::string value;
      parsed = parseString(value);
      if (parsed) {
        output = std::move(value);
      }
    } else if (ch == '-' || isDigit(ch)) {
      parsed = parseNumber(output);
    } else if (ch == 't') {
      parsed = parseLiteral("true");
      if (parsed) {
        output = true;
      }
    } else if (ch == 'f') {
      parsed = parseLiteral("false");
      if (parsed) {
        output = false;
      }
    } else if (ch == 'n') {
      parsed = parseLiteral("null");
      if (parsed) {
        output = nullptr;
      }
    } else if (ch == '+') {
      return fail("leading '+' is not supported in JSON numbers");
    } else if (ch == '.') {
      return fail("JSON numbers require an integer part before the decimal point");
    } else if (isIdentifierStart(ch)) {
      return fail(
          "unsupported value identifier; only true, false, and null are allowed");
    } else {
      return fail("expected a JSON value");
    }

    if (!parsed) {
      return false;
    }
    source_map_[pointer].value = {.begin = begin, .end = location_};
    return true;
  }

  bool parseObject(Json& output,
                   const std::string& pointer,
                   std::size_t depth) {
    advanceAscii();  // {
    output = Json::object();
    if (!skipTrivia()) {
      return false;
    }
    if (consume('}')) {
      return true;
    }

    std::unordered_map<std::string, JsonSourceLocation> keys;
    while (true) {
      if (atEnd()) {
        return fail("unterminated object; expected a member key or '}'");
      }

      const JsonSourceLocation key_begin = location_;
      std::string key;
      if (peek() == '"' || peek() == '\'') {
        if (!parseString(key)) {
          return false;
        }
      } else if (isIdentifierStart(peek())) {
        key = parseIdentifier();
      } else {
        return fail(
            "expected a quoted string or unquoted identifier as an object key");
      }
      const JsonSourceSpan key_span{.begin = key_begin, .end = location_};

      const auto [first, inserted] = keys.emplace(key, key_begin);
      if (!inserted) {
        return failAt(key_begin,
                      "duplicate object key '" + key +
                          "' (first declared at line " +
                          std::to_string(first->second.line) + ", column " +
                          std::to_string(first->second.column) + ")");
      }

      if (!skipTrivia()) {
        return false;
      }
      if (!consume(':')) {
        return fail("expected ':' after object key");
      }

      const std::string child_pointer = appendJsonPointer(pointer, key);
      Json value;
      if (!parseValue(value, child_pointer, depth + 1)) {
        return false;
      }
      source_map_[child_pointer].key = key_span;
      output[std::move(key)] = std::move(value);

      if (!skipTrivia()) {
        return false;
      }
      if (consume('}')) {
        return true;
      }
      if (!consume(',')) {
        return fail("expected ',' or '}' after object member");
      }
      if (!skipTrivia()) {
        return false;
      }
      if (consume('}')) {
        return true;  // Trailing comma.
      }
    }
  }

  bool parseArray(Json& output,
                  const std::string& pointer,
                  std::size_t depth) {
    advanceAscii();  // [
    output = Json::array();
    if (!skipTrivia()) {
      return false;
    }
    if (consume(']')) {
      return true;
    }

    std::size_t index = 0;
    while (true) {
      Json value;
      const std::string child_pointer =
          appendJsonPointer(pointer, std::to_string(index));
      if (!parseValue(value, child_pointer, depth + 1)) {
        return false;
      }
      output.push_back(std::move(value));
      ++index;

      if (!skipTrivia()) {
        return false;
      }
      if (consume(']')) {
        return true;
      }
      if (!consume(',')) {
        return fail("expected ',' or ']' after array element");
      }
      if (!skipTrivia()) {
        return false;
      }
      if (consume(']')) {
        return true;  // Trailing comma.
      }
    }
  }

  bool parseString(std::string& output) {
    const char quote = peek();
    advanceAscii();
    output.clear();

    while (!atEnd()) {
      const char ch = peek();
      if (ch == quote) {
        advanceAscii();
        return true;
      }
      if (static_cast<unsigned char>(ch) < 0x20U) {
        return fail("unescaped control character in string");
      }
      if (ch != '\\') {
        const Utf8DecodeResult decoded = decodeUtf8(source_, offset_);
        output.append(source_.substr(offset_, decoded.width));
        advanceCodepoint();
        continue;
      }

      const JsonSourceLocation escape_location = location_;
      advanceAscii();  // backslash
      if (atEnd()) {
        return failAt(escape_location, "unterminated escape sequence in string");
      }

      const char escaped = peek();
      advanceAscii();
      switch (escaped) {
        case '"':
          output.push_back('"');
          break;
        case '\'':
          if (quote != '\'') {
            return failAt(escape_location,
                          "\\' is only valid in a single-quoted string");
          }
          output.push_back('\'');
          break;
        case '\\':
          output.push_back('\\');
          break;
        case '/':
          output.push_back('/');
          break;
        case 'b':
          output.push_back('\b');
          break;
        case 'f':
          output.push_back('\f');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        case 'u': {
          std::uint32_t codepoint = 0;
          if (!parseUnicodeEscapeDigits(codepoint, escape_location)) {
            return false;
          }
          if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
            if (atEnd() || peek() != '\\' || peek(1) != 'u') {
              return failAt(escape_location,
                            "high surrogate must be followed by a low surrogate");
            }
            advanceAscii();
            advanceAscii();
            std::uint32_t low = 0;
            if (!parseUnicodeEscapeDigits(low, escape_location) ||
                low < 0xdc00U || low > 0xdfffU) {
              if (!error_.has_value()) {
                failAt(escape_location,
                       "high surrogate must be followed by a low surrogate");
              }
              return false;
            }
            codepoint = 0x10000U + ((codepoint - 0xd800U) << 10U) +
                        (low - 0xdc00U);
          } else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
            return failAt(escape_location,
                          "low surrogate cannot appear without a high surrogate");
          }
          appendUtf8(codepoint, output);
          break;
        }
        default:
          return failAt(escape_location,
                        "unsupported string escape; JSON5 escape extensions are "
                        "not accepted");
      }
    }
    return fail("unterminated string");
  }

  bool parseUnicodeEscapeDigits(std::uint32_t& output,
                                JsonSourceLocation escape_location) {
    output = 0;
    for (int index = 0; index < 4; ++index) {
      if (atEnd() || !isHexDigit(peek())) {
        return failAt(escape_location,
                      "Unicode escapes require exactly four hexadecimal digits");
      }
      output = (output << 4U) | hexValue(peek());
      advanceAscii();
    }
    return true;
  }

  bool parseNumber(Json& output) {
    const std::size_t begin = offset_;
    if (consume('-') && atEnd()) {
      return fail("expected a digit after '-'");
    }

    if (peek() == '0') {
      advanceAscii();
      if (!atEnd() && (peek() == 'x' || peek() == 'X')) {
        return fail("hexadecimal numbers are not supported");
      }
      if (!atEnd() && isDigit(peek())) {
        return fail("leading zeros are not allowed in JSON numbers");
      }
    } else if (peek() >= '1' && peek() <= '9') {
      do {
        advanceAscii();
      } while (!atEnd() && isDigit(peek()));
    } else {
      return fail("expected a digit in JSON number");
    }

    if (!atEnd() && peek() == '.') {
      advanceAscii();
      if (atEnd() || !isDigit(peek())) {
        return fail("JSON numbers require digits after the decimal point");
      }
      do {
        advanceAscii();
      } while (!atEnd() && isDigit(peek()));
    }

    if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
      advanceAscii();
      if (!atEnd() && (peek() == '+' || peek() == '-')) {
        advanceAscii();
      }
      if (atEnd() || !isDigit(peek())) {
        return fail("JSON exponents require at least one digit");
      }
      do {
        advanceAscii();
      } while (!atEnd() && isDigit(peek()));
    }

    if (!atEnd() && !isValueDelimiter()) {
      return fail("unsupported characters in JSON number");
    }

    const std::string_view token = source_.substr(begin, offset_ - begin);
    Json parsed = Json::parse(token, nullptr, false, true);
    if (parsed.is_discarded() || !parsed.is_number() ||
        (parsed.is_number_float() &&
         !std::isfinite(parsed.get<Json::number_float_t>()))) {
      return failAt(locationAt(source_, begin),
                    "JSON number is invalid or outside the finite range");
    }
    output = std::move(parsed);
    return true;
  }

  bool parseLiteral(std::string_view literal) {
    const JsonSourceLocation begin = location_;
    for (const char expected : literal) {
      if (atEnd() || peek() != expected) {
        return failAt(begin, "invalid JSON literal; expected '" +
                                 std::string(literal) + "'");
      }
      advanceAscii();
    }
    if (!atEnd() && !isValueDelimiter()) {
      return failAt(begin, "invalid JSON literal; expected '" +
                               std::string(literal) + "'");
    }
    return true;
  }

  std::string parseIdentifier() {
    const std::size_t begin = offset_;
    advanceAscii();
    while (!atEnd() && isIdentifierContinue(peek())) {
      advanceAscii();
    }
    return std::string(source_.substr(begin, offset_ - begin));
  }

  bool skipTrivia() {
    while (!atEnd()) {
      if (isJsonWhitespace(peek())) {
        advanceCodepoint();
        continue;
      }
      if (peek() != '/' || (peek(1) != '/' && peek(1) != '*')) {
        return true;
      }

      const JsonSourceLocation comment_begin = location_;
      if (peek(1) == '/') {
        advanceAscii();
        advanceAscii();
        while (!atEnd() && peek() != '\r' && peek() != '\n') {
          advanceCodepoint();
        }
        continue;
      }

      advanceAscii();
      advanceAscii();
      bool terminated = false;
      while (!atEnd()) {
        if (peek() == '*' && peek(1) == '/') {
          advanceAscii();
          advanceAscii();
          terminated = true;
          break;
        }
        advanceCodepoint();
      }
      if (!terminated) {
        return failAt(comment_begin, "unterminated block comment");
      }
    }
    return true;
  }

  bool isValueDelimiter() const {
    if (atEnd()) {
      return true;
    }
    const char ch = peek();
    return isJsonWhitespace(ch) || ch == ',' || ch == ']' || ch == '}' ||
           (ch == '/' && (peek(1) == '/' || peek(1) == '*'));
  }

  bool atEnd() const { return offset_ >= source_.size(); }

  char peek(std::size_t lookahead = 0) const {
    const std::size_t index = offset_ + lookahead;
    return index < source_.size() ? source_[index] : '\0';
  }

  bool consume(char expected) {
    if (atEnd() || peek() != expected) {
      return false;
    }
    advanceAscii();
    return true;
  }

  void advanceAscii() {
    ++offset_;
    ++location_.offset;
    ++location_.column;
  }

  void advanceCodepoint() {
    if (peek() == '\r') {
      ++offset_;
      ++location_.offset;
      if (!atEnd() && peek() == '\n') {
        ++offset_;
        ++location_.offset;
      }
      ++location_.line;
      location_.column = 1;
      return;
    }
    if (peek() == '\n') {
      ++offset_;
      ++location_.offset;
      ++location_.line;
      location_.column = 1;
      return;
    }

    const Utf8DecodeResult decoded = decodeUtf8(source_, offset_);
    offset_ += decoded.width;
    location_.offset += decoded.width;
    ++location_.column;
  }

  bool fail(std::string message) {
    return failAt(location_, std::move(message));
  }

  bool failAt(JsonSourceLocation location, std::string message) {
    if (!error_.has_value()) {
      error_ = JsonProfileParseError{
          .message = std::move(message),
          .location = location,
      };
    }
    return false;
  }

  JsonProfileParseResult failureResult() {
    if (!error_.has_value()) {
      error_ = JsonProfileParseError{
          .message = "unknown JSON profile parse error",
          .location = location_,
      };
    }
    return JsonProfileParseResult{
        .document = std::nullopt,
        .error = std::move(error_),
    };
  }

  std::string_view source_;
  std::size_t offset_ = 0;
  JsonSourceLocation location_;
  std::unordered_map<std::string, JsonValueSource> source_map_;
  std::optional<JsonProfileParseError> error_;
};

}  // namespace

const JsonValueSource* JsonProfileDocument::sourceFor(
    std::string_view json_pointer) const {
  const auto found = source_map.find(std::string(json_pointer));
  return found == source_map.end() ? nullptr : &found->second;
}

JsonProfileParseResult parseJsonProfile(std::string_view source) {
  if (auto error = validateUtf8(source); error.has_value()) {
    return JsonProfileParseResult{
        .document = std::nullopt,
        .error = std::move(error),
    };
  }
  return Parser(source).parse();
}

}  // namespace karma::assets::detail
