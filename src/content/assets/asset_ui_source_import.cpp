#include "asset_ui_source_import.h"
#include "ui_json_profile.h"
#include "ui_json_validation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace karma::assets::detail {

namespace {

struct XmlAttribute {
  std::string name;
  std::string value;
};

struct XmlOpenTag {
  std::string name;
  std::vector<XmlAttribute> attributes;
  bool self_closing = false;
};

using XmlOpenCallback = std::function<bool(const XmlOpenTag&,
                                           const std::vector<std::string>&,
                                           std::string*)>;

bool fail(std::string* diagnostic, std::string message) {
  if (diagnostic != nullptr) {
    *diagnostic = std::move(message);
  }
  return false;
}

std::string lowercase(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return result;
}

std::string_view trim(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1u);
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.remove_suffix(1u);
  }
  return value;
}

bool startsWithInsensitive(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  for (std::size_t i = 0u; i < prefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(value[i])) !=
        std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

bool containsInsensitive(std::string_view value, std::string_view needle) {
  const std::string lower_value = lowercase(value);
  const std::string lower_needle = lowercase(needle);
  return lower_value.find(lower_needle) != std::string::npos;
}

bool isValidUtf8(std::string_view source, std::string* diagnostic) {
  std::size_t offset = 0u;
  while (offset < source.size()) {
    const uint8_t first = static_cast<uint8_t>(source[offset]);
    uint32_t codepoint = 0u;
    std::size_t length = 0u;
    if (first <= 0x7fu) {
      codepoint = first;
      length = 1u;
    } else if (first >= 0xc2u && first <= 0xdfu) {
      codepoint = first & 0x1fu;
      length = 2u;
    } else if (first >= 0xe0u && first <= 0xefu) {
      codepoint = first & 0x0fu;
      length = 3u;
    } else if (first >= 0xf0u && first <= 0xf4u) {
      codepoint = first & 0x07u;
      length = 4u;
    } else {
      return fail(diagnostic, "source is not valid UTF-8");
    }
    if (offset + length > source.size()) {
      return fail(diagnostic, "source ends inside a UTF-8 sequence");
    }
    for (std::size_t index = 1u; index < length; ++index) {
      const uint8_t next = static_cast<uint8_t>(source[offset + index]);
      if ((next & 0xc0u) != 0x80u) {
        return fail(diagnostic, "source contains an invalid UTF-8 continuation byte");
      }
      codepoint = (codepoint << 6u) | (next & 0x3fu);
    }
    if ((length == 3u && codepoint < 0x800u) ||
        (length == 4u && codepoint < 0x10000u) ||
        codepoint > 0x10ffffu ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
      return fail(diagnostic, "source contains a non-canonical UTF-8 sequence");
    }
    if (codepoint == 0u ||
        (codepoint < 0x20u && codepoint != '\t' && codepoint != '\n' &&
         codepoint != '\r')) {
      return fail(diagnostic, "source contains a forbidden control character");
    }
    offset += length;
  }
  return true;
}

bool isNameStart(char value) {
  const unsigned char byte = static_cast<unsigned char>(value);
  return std::isalpha(byte) || value == '_' || value == ':';
}

bool isNameCharacter(char value) {
  const unsigned char byte = static_cast<unsigned char>(value);
  return isNameStart(value) || std::isdigit(byte) || value == '-' || value == '.';
}

bool readXmlName(std::string_view source, std::size_t& offset, std::string& name) {
  if (offset >= source.size() || !isNameStart(source[offset])) {
    return false;
  }
  const std::size_t begin = offset++;
  while (offset < source.size() && isNameCharacter(source[offset])) {
    ++offset;
  }
  name.assign(source.substr(begin, offset - begin));
  return true;
}

void skipWhitespace(std::string_view source, std::size_t& offset) {
  while (offset < source.size() &&
         std::isspace(static_cast<unsigned char>(source[offset]))) {
    ++offset;
  }
}

bool validXmlEntities(std::string_view source, std::string* diagnostic) {
  std::size_t offset = 0u;
  while ((offset = source.find('&', offset)) != std::string_view::npos) {
    const std::size_t end = source.find(';', offset + 1u);
    if (end == std::string_view::npos) {
      return fail(diagnostic, "XML entity reference is unterminated");
    }
    const std::string_view entity = source.substr(offset + 1u, end - offset - 1u);
    if (entity == "amp" || entity == "lt" || entity == "gt" ||
        entity == "quot" || entity == "apos") {
      offset = end + 1u;
      continue;
    }
    if (entity.size() >= 2u && entity.front() == '#') {
      std::size_t digit = 1u;
      int base = 10;
      if (digit < entity.size() && (entity[digit] == 'x' || entity[digit] == 'X')) {
        base = 16;
        ++digit;
      }
      if (digit == entity.size()) {
        return fail(diagnostic, "XML numeric entity has no digits");
      }
      uint32_t codepoint = 0u;
      for (; digit < entity.size(); ++digit) {
        const char c = entity[digit];
        uint32_t value = 0u;
        if (c >= '0' && c <= '9') {
          value = static_cast<uint32_t>(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
          value = 10u + static_cast<uint32_t>(c - 'a');
        } else if (base == 16 && c >= 'A' && c <= 'F') {
          value = 10u + static_cast<uint32_t>(c - 'A');
        } else {
          return fail(diagnostic, "XML numeric entity contains an invalid digit");
        }
        if (codepoint > (0x10ffffu - value) / static_cast<uint32_t>(base)) {
          return fail(diagnostic, "XML numeric entity is out of range");
        }
        codepoint = codepoint * static_cast<uint32_t>(base) + value;
      }
      if (codepoint == 0u || codepoint > 0x10ffffu ||
          (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
        return fail(diagnostic, "XML numeric entity is not a valid Unicode scalar");
      }
      offset = end + 1u;
      continue;
    }
    return fail(diagnostic, "external or unknown XML entity is forbidden: &" +
                                std::string(entity) + ";");
  }
  return true;
}

std::string localXmlName(std::string_view name) {
  const std::size_t colon = name.rfind(':');
  return lowercase(colon == std::string_view::npos ? name : name.substr(colon + 1u));
}

bool parseXml(std::string_view source,
              std::string_view expected_root,
              const XmlOpenCallback& callback,
              std::string* diagnostic) {
  if (!isValidUtf8(source, diagnostic)) {
    return false;
  }
  std::vector<std::string> stack;
  bool root_seen = false;
  bool root_closed = false;
  bool xml_declaration_seen = false;
  std::size_t offset = 0u;
  if (source.size() >= 3u && static_cast<uint8_t>(source[0]) == 0xefu &&
      static_cast<uint8_t>(source[1]) == 0xbbu &&
      static_cast<uint8_t>(source[2]) == 0xbfu) {
    offset = 3u;
  }

  while (offset < source.size()) {
    const std::size_t tag_start = source.find('<', offset);
    const std::size_t text_end =
        tag_start == std::string_view::npos ? source.size() : tag_start;
    const std::string_view text = source.substr(offset, text_end - offset);
    if (!validXmlEntities(text, diagnostic)) {
      return false;
    }
    if (stack.empty() && !trim(text).empty()) {
      return fail(diagnostic, "non-whitespace text is not allowed outside the XML root");
    }
    if (tag_start == std::string_view::npos) {
      offset = source.size();
      break;
    }
    offset = tag_start;

    if (source.substr(offset, 4u) == "<!--") {
      const std::size_t end = source.find("-->", offset + 4u);
      if (end == std::string_view::npos) {
        return fail(diagnostic, "XML comment is unterminated");
      }
      if (source.substr(offset + 4u, end - offset - 4u).find("--") !=
          std::string_view::npos) {
        return fail(diagnostic, "XML comment contains forbidden '--'");
      }
      offset = end + 3u;
      continue;
    }
    if (source.substr(offset, 2u) == "<?") {
      const std::size_t end = source.find("?>", offset + 2u);
      if (end == std::string_view::npos) {
        return fail(diagnostic, "XML processing instruction is unterminated");
      }
      const std::string_view instruction = trim(source.substr(offset + 2u, end - offset - 2u));
      const bool xml_declaration =
          startsWithInsensitive(instruction, "xml") &&
          (instruction.size() == 3u ||
           std::isspace(static_cast<unsigned char>(instruction[3])));
      if (root_seen || xml_declaration_seen || !xml_declaration ||
          (containsInsensitive(instruction, "encoding") &&
           !containsInsensitive(instruction, "utf-8") &&
           !containsInsensitive(instruction, "utf8"))) {
        return fail(diagnostic, "only an optional UTF-8 XML declaration is allowed");
      }
      xml_declaration_seen = true;
      offset = end + 2u;
      continue;
    }
    if (source.substr(offset, 2u) == "<!") {
      return fail(diagnostic, "XML declarations, DTDs, CDATA, and entities are forbidden");
    }

    ++offset;
    if (offset < source.size() && source[offset] == '/') {
      ++offset;
      skipWhitespace(source, offset);
      std::string name;
      if (!readXmlName(source, offset, name)) {
        return fail(diagnostic, "XML closing tag has an invalid name");
      }
      skipWhitespace(source, offset);
      if (offset >= source.size() || source[offset] != '>') {
        return fail(diagnostic, "XML closing tag is malformed");
      }
      ++offset;
      if (stack.empty() || stack.back() != name) {
        return fail(diagnostic, "XML closing tag does not match the open element: " + name);
      }
      stack.pop_back();
      if (stack.empty()) {
        root_closed = true;
      }
      continue;
    }

    if (root_closed) {
      return fail(diagnostic, "XML document contains more than one root element");
    }
    XmlOpenTag tag;
    if (!readXmlName(source, offset, tag.name)) {
      return fail(diagnostic, "XML opening tag has an invalid name");
    }
    std::unordered_set<std::string> attribute_names;
    while (true) {
      skipWhitespace(source, offset);
      if (offset >= source.size()) {
        return fail(diagnostic, "XML opening tag is unterminated");
      }
      if (source[offset] == '>') {
        ++offset;
        break;
      }
      if (source[offset] == '/' && offset + 1u < source.size() &&
          source[offset + 1u] == '>') {
        tag.self_closing = true;
        offset += 2u;
        break;
      }
      XmlAttribute attribute;
      if (!readXmlName(source, offset, attribute.name)) {
        return fail(diagnostic, "XML attribute has an invalid name");
      }
      if (!attribute_names.insert(attribute.name).second) {
        return fail(diagnostic, "duplicate XML attribute: " + attribute.name);
      }
      skipWhitespace(source, offset);
      if (offset >= source.size() || source[offset] != '=') {
        return fail(diagnostic, "XML attribute requires '=': " + attribute.name);
      }
      ++offset;
      skipWhitespace(source, offset);
      if (offset >= source.size() || (source[offset] != '\'' && source[offset] != '"')) {
        return fail(diagnostic, "XML attribute values must be quoted");
      }
      const char quote = source[offset++];
      const std::size_t value_start = offset;
      const std::size_t value_end = source.find(quote, value_start);
      if (value_end == std::string_view::npos) {
        return fail(diagnostic, "XML attribute value is unterminated");
      }
      if (source.substr(value_start, value_end - value_start).find('<') !=
          std::string_view::npos) {
        return fail(diagnostic, "XML attribute value contains '<'");
      }
      attribute.value.assign(source.substr(value_start, value_end - value_start));
      if (!validXmlEntities(attribute.value, diagnostic)) {
        return false;
      }
      offset = value_end + 1u;
      tag.attributes.push_back(std::move(attribute));
    }

    if (!root_seen) {
      if (localXmlName(tag.name) != lowercase(expected_root)) {
        return fail(diagnostic, "XML root must be <" + std::string(expected_root) + ">");
      }
      root_seen = true;
    }
    if (!callback(tag, stack, diagnostic)) {
      return false;
    }
    if (!tag.self_closing) {
      stack.push_back(tag.name);
    } else if (stack.empty()) {
      root_closed = true;
    }
  }

  if (!root_seen) {
    return fail(diagnostic, "XML document has no root element");
  }
  if (!stack.empty()) {
    return fail(diagnostic, "XML document has unclosed element: " + stack.back());
  }
  return true;
}

void addDependency(std::vector<UiAssetDependency>& dependencies,
                   UiAssetDependencyKind kind,
                   std::string key) {
  const UiAssetDependency candidate{.kind = kind, .key = std::move(key)};
  if (std::find(dependencies.begin(), dependencies.end(), candidate) ==
      dependencies.end()) {
    dependencies.push_back(candidate);
  }
}

bool addValidatedDependency(std::vector<UiAssetDependency>& dependencies,
                            UiAssetDependencyKind kind,
                            std::string_view key,
                            std::string* diagnostic) {
  const std::string_view value = trim(key);
  if (!AssetRegistry::isValidAssetKey(value)) {
    return fail(diagnostic,
                "invalid UI asset reference '" + std::string(value) + "': " +
                    AssetRegistry::assetKeyValidationError(value));
  }
  addDependency(dependencies, kind, std::string(value));
  return true;
}

bool hasExpectedSuffix(const std::filesystem::path& path,
                       std::string_view suffix) {
  const std::string filename = lowercase(path.filename().string());
  const std::string expected = lowercase(suffix);
  return filename.size() >= expected.size() &&
         filename.compare(filename.size() - expected.size(),
                          expected.size(),
                          expected) == 0;
}

bool hasExpectedExtension(const std::filesystem::path& path,
                          std::initializer_list<std::string_view> extensions) {
  const std::string extension = lowercase(path.extension().string());
  return std::find(extensions.begin(), extensions.end(), extension) !=
         extensions.end();
}

std::optional<std::vector<uint8_t>> readFileBytes(const std::filesystem::path& path,
                                                  std::string* diagnostic) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    fail(diagnostic, "failed to open UI asset source: " + path.string());
    return std::nullopt;
  }
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size < 0) {
    fail(diagnostic, "failed to determine UI asset source size: " + path.string());
    return std::nullopt;
  }
  stream.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  if (!stream && size > 0) {
    fail(diagnostic, "failed to read UI asset source: " + path.string());
    return std::nullopt;
  }
  return bytes;
}

std::optional<std::string> readUtf8File(const std::filesystem::path& path,
                                        std::string* diagnostic) {
  auto bytes = readFileBytes(path, diagnostic);
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  std::string source;
  if (!bytes->empty()) {
    source.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
  }
  if (!isValidUtf8(source, diagnostic)) {
    return std::nullopt;
  }
  return source;
}

using Json = nlohmann::json;

std::optional<UiAssetDependencyKind> dependencyKindFromName(
    std::string_view value) {
  const std::string kind = lowercase(value);
  if (kind == "theme" || kind == "ui_theme") {
    return UiAssetDependencyKind::UiTheme;
  }
  if (kind == "texture" || kind == "image") {
    return UiAssetDependencyKind::Texture;
  }
  if (kind == "font") {
    return UiAssetDependencyKind::Font;
  }
  if (kind == "svg") {
    return UiAssetDependencyKind::Svg;
  }
  return std::nullopt;
}

std::optional<UiAssetDependencyKind> dependencyKindForField(
    std::string_view field) {
  const std::string name = lowercase(field);
  if (name == "themes" || name == "imports" || name == "theme") {
    return UiAssetDependencyKind::UiTheme;
  }
  if (name == "font" || name == "fonts" || name == "font_face" ||
      name == "font_faces") {
    return UiAssetDependencyKind::Font;
  }
  if (name == "svg") {
    return UiAssetDependencyKind::Svg;
  }
  if (name == "src" || name == "source" || name == "image" ||
      name == "images" || name == "texture" || name == "textures" ||
      name == "background_image" || name == "border_image") {
    return UiAssetDependencyKind::Texture;
  }
  return std::nullopt;
}

bool validDevelopmentFileReference(std::string_view file,
                                   std::string* diagnostic) {
  if (file.empty()) {
    return fail(diagnostic, "UI development file reference must not be empty");
  }
  const std::filesystem::path path{std::string(file)};
  if (path.is_absolute() || file.front() == '/' ||
      file.find('\\') != std::string_view::npos ||
      file.find(':') != std::string_view::npos) {
    return fail(diagnostic,
                "UI development file reference must be a relative sandbox path: " +
                    std::string(file));
  }
  for (const auto& component : path) {
    if (component == "..") {
      return fail(diagnostic,
                  "UI development file reference must not escape its asset root: " +
                      std::string(file));
    }
  }
  return true;
}

bool collectAssetReferences(
    const Json& value,
    std::optional<UiAssetDependencyKind> inherited_kind,
    std::vector<UiAssetDependency>& dependencies,
    std::string* diagnostic) {
  if (value.is_array()) {
    for (const Json& entry : value) {
      if (!collectAssetReferences(entry,
                                  inherited_kind,
                                  dependencies,
                                  diagnostic)) {
        return false;
      }
    }
    return true;
  }
  if (!value.is_object()) {
    return true;
  }

  std::optional<UiAssetDependencyKind> object_kind = inherited_kind;
  const auto asset_it = value.find("asset");
  const auto file_it = value.find("file");
  if (asset_it != value.end() && file_it != value.end()) {
    return fail(diagnostic,
                "UI asset reference must contain either 'asset' or 'file', not both");
  }
  if (asset_it != value.end() || file_it != value.end()) {
    if (const auto kind_it = value.find("kind"); kind_it != value.end()) {
      if (!kind_it->is_string()) {
        return fail(diagnostic, "UI asset reference kind must be a string");
      }
      object_kind = dependencyKindFromName(kind_it->get_ref<const std::string&>());
      if (!object_kind.has_value()) {
        return fail(diagnostic,
                    "unsupported UI asset reference kind: " +
                        kind_it->get<std::string>());
      }
    }
  }
  if (asset_it != value.end() && object_kind.has_value()) {
    if (!asset_it->is_string()) {
      return fail(diagnostic, "UI asset reference 'asset' must be a string");
    }
    if (!addValidatedDependency(dependencies,
                                *object_kind,
                                asset_it->get_ref<const std::string&>(),
                                diagnostic)) {
      return false;
    }
  }
  if (file_it != value.end()) {
    if (!file_it->is_string() ||
        !validDevelopmentFileReference(file_it->get_ref<const std::string&>(),
                                       diagnostic)) {
      return false;
    }
    return fail(diagnostic,
                "UI development file references must be resolved by the "
                "sandboxed loose-file loader before asset import");
  }

  for (auto it = value.begin(); it != value.end(); ++it) {
    if (it.key() == "asset" || it.key() == "file" || it.key() == "kind") {
      continue;
    }
    const auto field_kind = dependencyKindForField(it.key());
    const bool generic_source_field = it.key() == "src" || it.key() == "source";
    const auto child_kind = generic_source_field && object_kind.has_value()
                                ? object_kind
                                : (field_kind.has_value() ? field_kind
                                                          : object_kind);
    if (!collectAssetReferences(*it,
                                child_kind,
                                dependencies,
                                diagnostic)) {
      return false;
    }
  }
  return true;
}

bool parseUiJson(std::string_view source,
                 bool document,
                 std::string& canonical_json_utf8,
                 std::vector<UiAssetDependency>& dependencies,
                 std::string* diagnostic) {
  canonical_json_utf8.clear();
  dependencies.clear();
  if (!isValidUtf8(source, diagnostic)) {
    return false;
  }

  JsonProfileParseResult parse_result = parseJsonProfile(source);
  if (!parse_result) {
    if (parse_result.error.has_value()) {
      const JsonProfileParseError& error = *parse_result.error;
      return fail(diagnostic,
                  "invalid UI JSON at line " +
                      std::to_string(error.location.line) + ", column " +
                      std::to_string(error.location.column) + ": " + error.message);
    }
    return fail(diagnostic, "invalid UI JSON");
  }
  JsonProfileDocument profile = std::move(*parse_result.document);
  const Json& parsed = profile.value;
  const std::vector<UiJsonValidationIssue> issues = validateUiJsonProfile(
      profile, document ? UiJsonKind::Document : UiJsonKind::Theme);
  if (!issues.empty()) {
    const UiJsonValidationIssue& issue = issues.front();
    std::string message = issue.code + ": " + issue.message;
    if (issue.line != 0u) {
      message += " at line " + std::to_string(issue.line) + ", column " +
                 std::to_string(issue.column);
    }
    return fail(diagnostic, std::move(message));
  }
  if (document) {
    const auto themes_it = parsed.find("themes");
    if (themes_it != parsed.end() &&
        !collectAssetReferences(*themes_it,
                                UiAssetDependencyKind::UiTheme,
                                dependencies,
                                diagnostic)) {
      dependencies.clear();
      return false;
    }
  } else {
    const auto imports_it = parsed.find("imports");
    if (imports_it != parsed.end() && !imports_it->is_array()) {
      return fail(diagnostic, "UI theme 'imports' must be an array");
    }
    if (imports_it != parsed.end() &&
        !collectAssetReferences(*imports_it,
                                UiAssetDependencyKind::UiTheme,
                                dependencies,
                                diagnostic)) {
      dependencies.clear();
      return false;
    }
  }
  if (!collectAssetReferences(parsed, std::nullopt, dependencies, diagnostic)) {
    dependencies.clear();
    return false;
  }
  canonical_json_utf8 = std::move(profile.canonical_json);
  return true;
}

bool validateSvgUrls(std::string_view source, std::string* diagnostic) {
  if (containsInsensitive(source, "@import") ||
      containsInsensitive(source, "javascript:") ||
      containsInsensitive(source, "file:") ||
      containsInsensitive(source, "data:")) {
    return fail(diagnostic, "SVG external resources and executable URLs are forbidden");
  }
  const std::string lower = lowercase(source);
  std::size_t offset = 0u;
  while ((offset = lower.find("url(", offset)) != std::string::npos) {
    const std::size_t close = source.find(')', offset + 4u);
    if (close == std::string_view::npos) {
      return fail(diagnostic, "SVG url() reference is unterminated");
    }
    std::string_view value = trim(source.substr(offset + 4u, close - offset - 4u));
    if (value.size() >= 2u && (value.front() == '\'' || value.front() == '"') &&
        value.back() == value.front()) {
      value = trim(value.substr(1u, value.size() - 2u));
    }
    if (value.empty() || value.front() != '#') {
      return fail(diagnostic, "SVG url() may reference only a local fragment");
    }
    offset = close + 1u;
  }
  return true;
}

uint16_t readBigU16(const std::vector<uint8_t>& bytes, std::size_t offset) {
  return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8u) |
                               static_cast<uint16_t>(bytes[offset + 1u]));
}

uint32_t readBigU32(const std::vector<uint8_t>& bytes, std::size_t offset) {
  return (static_cast<uint32_t>(bytes[offset]) << 24u) |
         (static_cast<uint32_t>(bytes[offset + 1u]) << 16u) |
         (static_cast<uint32_t>(bytes[offset + 2u]) << 8u) |
         static_cast<uint32_t>(bytes[offset + 3u]);
}

bool validSfntAt(const std::vector<uint8_t>& bytes, std::size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 12u) {
    return false;
  }
  const uint32_t signature = readBigU32(bytes, offset);
  if (signature != 0x00010000u && signature != 0x4f54544fu &&
      signature != 0x74727565u && signature != 0x74797031u) {
    return false;
  }
  const uint16_t table_count = readBigU16(bytes, offset + 4u);
  return table_count > 0u &&
         static_cast<std::size_t>(table_count) <= (bytes.size() - offset - 12u) / 16u;
}

}  // namespace

bool validateUiDocumentJson(
    std::string_view source,
    std::string& canonical_json_utf8,
    std::vector<UiAssetDependency>& dependencies,
    std::string* diagnostic) {
  return parseUiJson(source,
                     true,
                     canonical_json_utf8,
                     dependencies,
                     diagnostic);
}

bool validateUiThemeJson(
    std::string_view source,
    std::string& canonical_json_utf8,
    std::vector<UiAssetDependency>& dependencies,
    std::string* diagnostic) {
  return parseUiJson(source,
                     false,
                     canonical_json_utf8,
                     dependencies,
                     diagnostic);
}

bool validateFontBytes(const std::vector<uint8_t>& bytes,
                       std::string* diagnostic) {
  if (bytes.size() < 12u) {
    return fail(diagnostic, "font asset is too small to be a TTF/OTF/TTC/OTC file");
  }
  if (readBigU32(bytes, 0u) == 0x74746366u) {
    const uint32_t face_count = readBigU32(bytes, 8u);
    if (face_count == 0u || face_count > 4096u ||
        static_cast<std::size_t>(face_count) > (bytes.size() - 12u) / 4u) {
      return fail(diagnostic, "font collection face table is malformed");
    }
    for (uint32_t face = 0u; face < face_count; ++face) {
      const std::size_t face_offset = readBigU32(bytes, 12u + face * 4u);
      if (!validSfntAt(bytes, face_offset)) {
        return fail(diagnostic, "font collection contains an invalid face");
      }
    }
    return true;
  }
  if (!validSfntAt(bytes, 0u)) {
    return fail(diagnostic, "font asset is not a supported TTF/OTF/TTC/OTC payload");
  }
  return true;
}

bool validateSvgSource(std::string_view source, std::string* diagnostic) {
  if (!validateSvgUrls(source, diagnostic)) {
    return false;
  }
  static const std::unordered_set<std::string> forbidden_elements{
      "script", "foreignobject", "iframe", "object", "embed", "audio", "video",
      "animate", "animatemotion", "animatetransform", "set", "filter",
  };
  return parseXml(
      source,
      "svg",
      [&](const XmlOpenTag& tag,
          const std::vector<std::string>&,
          std::string* callback_diagnostic) {
        const std::string name = localXmlName(tag.name);
        if (forbidden_elements.contains(name)) {
          return fail(callback_diagnostic, "unsupported or unsafe SVG element: " + tag.name);
        }
        for (const XmlAttribute& attribute : tag.attributes) {
          const std::string attribute_name = lowercase(attribute.name);
          if (startsWithInsensitive(attribute_name, "on") && attribute_name.size() > 2u) {
            return fail(callback_diagnostic, "SVG event handler attributes are forbidden");
          }
          if (attribute_name == "href" || attribute_name == "xlink:href") {
            const std::string_view value = trim(attribute.value);
            if (value.empty() || value.front() != '#') {
              return fail(callback_diagnostic,
                          "SVG href references may target only local fragments");
            }
          }
          if (attribute_name == "xml:base") {
            return fail(callback_diagnostic, "SVG xml:base is forbidden");
          }
        }
        return true;
      },
      diagnostic);
}

bool importUiDocumentAsset(AssetRegistry& assets,
                           const std::string& key,
                           const std::filesystem::path& path,
                           std::string* diagnostic) {
  if (!hasExpectedSuffix(path, ".kui.json5")) {
    return fail(diagnostic,
                "ui_document source must use the .kui.json5 extension");
  }
  auto source = readUtf8File(path, diagnostic);
  if (!source.has_value()) {
    return false;
  }
  UiDocumentAsset asset;
  if (!validateUiDocumentJson(*source,
                              asset.canonical_json_utf8,
                              asset.dependencies,
                              diagnostic) ||
      !assets.registerUiDocumentAsset(key, std::move(asset))) {
    return diagnostic != nullptr && !diagnostic->empty()
               ? false
               : fail(diagnostic, "failed to register UI document asset: " + key);
  }
  UiSourceMetadataAccess::setDocument(assets, key, path);
  return true;
}

bool importUiThemeAsset(AssetRegistry& assets,
                        const std::string& key,
                        const std::filesystem::path& path,
                        std::string* diagnostic) {
  if (!hasExpectedSuffix(path, ".kstyle.json5")) {
    return fail(diagnostic,
                "ui_theme source must use the .kstyle.json5 extension");
  }
  auto source = readUtf8File(path, diagnostic);
  if (!source.has_value()) {
    return false;
  }
  UiThemeAsset asset;
  if (!validateUiThemeJson(*source,
                           asset.canonical_json_utf8,
                           asset.dependencies,
                           diagnostic) ||
      !assets.registerUiThemeAsset(key, std::move(asset))) {
    return diagnostic != nullptr && !diagnostic->empty()
               ? false
               : fail(diagnostic, "failed to register UI theme asset: " + key);
  }
  UiSourceMetadataAccess::setTheme(assets, key, path);
  return true;
}

bool importFontAsset(AssetRegistry& assets,
                     const std::string& key,
                     const std::filesystem::path& path,
                     std::string* diagnostic) {
  if (!hasExpectedExtension(path, {".ttf", ".otf", ".ttc", ".otc"})) {
    return fail(diagnostic, "font source must use a .ttf, .otf, .ttc, or .otc extension");
  }
  auto bytes = readFileBytes(path, diagnostic);
  if (!bytes.has_value() || !validateFontBytes(*bytes, diagnostic)) {
    return false;
  }
  if (!assets.registerFontAsset(key, FontAsset{.bytes = std::move(*bytes)})) {
    return fail(diagnostic, "failed to register font asset: " + key);
  }
  return true;
}

bool importSvgAsset(AssetRegistry& assets,
                    const std::string& key,
                    const std::filesystem::path& path,
                    std::string* diagnostic) {
  if (!hasExpectedExtension(path, {".svg"})) {
    return fail(diagnostic, "svg source must use the .svg extension");
  }
  auto source = readUtf8File(path, diagnostic);
  if (!source.has_value() || !validateSvgSource(*source, diagnostic)) {
    return false;
  }
  if (!assets.registerSvgAsset(key, SvgAsset{.source_utf8 = std::move(*source)})) {
    return fail(diagnostic, "failed to register SVG asset: " + key);
  }
  return true;
}

}  // namespace karma::assets::detail
