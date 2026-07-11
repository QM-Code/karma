#include "content/assets/ui_json_profile.h"

#include <cassert>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using karma::assets::detail::JsonProfileParseResult;
using karma::assets::detail::parseJsonProfile;

JsonProfileParseResult parseFailure(std::string_view source) {
  JsonProfileParseResult result = parseJsonProfile(source);
  assert(!result);
  assert(!result.document.has_value());
  assert(result.error.has_value());
  assert(!result.error->message.empty());
  return result;
}

void testAuthoringProfileAndCanonicalJson() {
  const std::string source =
      "\xef\xbb\xbf/* authoring document */\n"
      "{\n"
      "  format: 'karma.ui.document',\n"
      "  version: 2,\n"
      "  enabled: true,\n"
      "  scale: -12.5e+2,\n"
      "  title: 'It\\'s ready',\n"
      "  escaped: \"line\\n\\uD83D\\uDE03\",\n"
      "  values: [null, false, 18446744073709551615,], // trailing comma\n"
      "}\n";

  const JsonProfileParseResult result = parseJsonProfile(source);
  assert(result);
  assert(result.document.has_value());
  assert(!result.error.has_value());
  const auto& document = *result.document;
  assert(document.value["format"] == "karma.ui.document");
  assert(document.value["version"] == 2);
  assert(document.value["enabled"] == true);
  assert(document.value["scale"] == -1250.0);
  assert(document.value["title"] == "It's ready");
  assert(document.value["escaped"] == "line\n\xf0\x9f\x98\x83");
  assert(document.value["values"].size() == 3);
  assert(document.value["values"][2].is_number_unsigned());

  // Canonical output is strict JSON and round-trips without relaxed parsing.
  const nlohmann::json reparsed =
      nlohmann::json::parse(document.canonical_json);
  assert(reparsed == document.value);
  assert(document.canonical_json == document.value.dump());
  assert(document.canonical_json.find("/*") == std::string::npos);
  assert(document.canonical_json.find("//") == std::string::npos);
}

void testSourceMap() {
  const JsonProfileParseResult result = parseJsonProfile(
      "{\n"
      " title: 'hello',\n"
      " list: [1, 2],\n"
      " 'a/b~c': true,\n"
      "}\n");
  assert(result);
  const auto& document = *result.document;

  const auto* root = document.sourceFor("");
  assert(root != nullptr);
  assert(root->value.begin.line == 1);
  assert(root->value.begin.column == 1);
  assert(root->value.end.line == 5);
  assert(root->value.end.column == 2);
  assert(!root->key.has_value());

  const auto* title = document.sourceFor("/title");
  assert(title != nullptr && title->key.has_value());
  assert(title->key->begin.line == 2);
  assert(title->key->begin.column == 2);
  assert(title->value.begin.line == 2);
  assert(title->value.begin.column == 9);

  const auto* second = document.sourceFor("/list/1");
  assert(second != nullptr);
  assert(second->value.begin.line == 3);
  assert(second->value.begin.column == 12);

  // JSON Pointer escaping is applied to source-map keys.
  assert(document.sourceFor("/a~1b~0c") != nullptr);
}

void testDuplicateKeys() {
  const auto result = parseFailure(
      "{\n"
      " dup: 1,\n"
      " \"dup\": 2\n"
      "}\n");
  assert(result.error->location.line == 3);
  assert(result.error->location.column == 2);
  assert(result.error->message.find("duplicate object key") !=
         std::string::npos);
  assert(result.error->message.find("line 2, column 2") != std::string::npos);
}

void testStrictUtf8() {
  const std::vector<std::vector<unsigned char>> invalid_sequences = {
      {0xc0, 0xaf},              // Overlong encoding.
      {0x80},                    // Stray continuation byte.
      {0xe2, 0x82},              // Truncated sequence.
      {0xe2, 0x28, 0xa1},        // Invalid continuation byte.
      {0xed, 0xa0, 0x80},        // UTF-16 surrogate code point.
      {0xf4, 0x90, 0x80, 0x80},  // Beyond U+10FFFF.
  };
  for (const auto& bytes : invalid_sequences) {
    std::string invalid = "{ value: '";
    for (const unsigned char byte : bytes) {
      invalid.push_back(static_cast<char>(byte));
    }
    invalid += "' }";
    const auto invalid_result = parseFailure(invalid);
    assert(invalid_result.error->message.find("UTF-8") != std::string::npos);
    assert(invalid_result.error->location.line == 1);
    assert(invalid_result.error->location.column == 11);
  }

  const auto unicode = parseJsonProfile("{ label: 'κόσμε' }");
  assert(unicode);
  assert(unicode.document->value["label"] == "κόσμε");

  // Source columns count Unicode scalars, not bytes.
  const auto unicode_span = parseJsonProfile("{ first: 'é', second: 2 }");
  assert(unicode_span);
  const auto* second = unicode_span.document->sourceFor("/second");
  assert(second != nullptr && second->key.has_value());
  assert(second->key->begin.column == 15);
}

void testNumericExtensionsAreRejected() {
  const std::vector<std::string_view> invalid_values = {
      "NaN",      "Infinity", "-Infinity", "+1",   "0x10",
      "-0Xf",     ".5",       "1.",        "01",   "1_000",
      "1e",       "1e+",      "1e400",     "-1e9999",
  };
  for (const std::string_view value : invalid_values) {
    const std::string source = "{ value: " + std::string(value) + " }";
    parseFailure(source);
  }

  const auto valid = parseJsonProfile(
      "[-0, 0, -12, 12, 0.25, -0.25, 1e3, 1E-3, 1e+3]");
  assert(valid);
  assert(valid.document->value.size() == 9);
}

void testJson5ExtensionsOutsideTheProfileAreRejected() {
  const std::vector<std::string_view> invalid_sources = {
      "{ value: '\\x41' }",        // Hex escape.
      "{ value: '\\v' }",          // JSON5 vertical-tab escape.
      "{ value: 'line\\\nnext' }",  // JSON5 line continuation.
      "{ value: \"\\'\" }",        // Single-quote escape in double quotes.
      "{ value: '\\uD800' }",      // Lone high surrogate.
      "{ value: '\\uDC00' }",      // Lone low surrogate.
      "{ value: 'unterminated }",
      "{ value: 1 } trailing",
      "{ value: 1 /* open }",
      "{ value: 1, value: 2 }",
  };
  for (const std::string_view source : invalid_sources) {
    parseFailure(source);
  }

  std::string control = "{ value: 'a";
  control.push_back('\x01');
  control += "b' }";
  parseFailure(control);
}

void testStandardWhitespaceAndLocations() {
  const auto block_comment = parseFailure(
      "// first line\r\n"
      "{\r\n"
      " value: 1,\r\n"
      " /* unfinished");
  assert(block_comment.error->location.line == 4);
  assert(block_comment.error->location.column == 2);

  const auto vertical_tab = parseFailure("{\vvalue: 1}");
  assert(vertical_tab.error->location.line == 1);
  assert(vertical_tab.error->location.column == 2);
}

void testOnlyObjectKeysMayBeUnquoted() {
  assert(parseJsonProfile("{ $schema: 'ui', _private2: null }"));
  parseFailure("{ dashed-key: 1 }");
  parseFailure("{ κλειδί: 1 }");
  parseFailure("{ value: unquoted }");
}

}  // namespace

int main() {
  testAuthoringProfileAndCanonicalJson();
  testSourceMap();
  testDuplicateKeys();
  testStrictUtf8();
  testNumericExtensionsAreRejected();
  testJson5ExtensionsOutsideTheProfileAreRejected();
  testStandardWhitespaceAndLocations();
  testOnlyObjectKeysMayBeUnquoted();
  std::cout << "ui json profile tests passed\n";
  return 0;
}
