#pragma once

#include "features/ui/native/font_face.h"
#include "features/ui/native/motion_engine.h"
#include "features/ui/native/style_runtime.h"
#include "karma/ui.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace karma::ui::native {

using Declarations = std::unordered_map<std::string, std::string>;

struct StyleRule {
  std::string selector;
  Declarations declarations;
  /// Named style that produced this rule. Empty for widget defaults.
  std::string style_name;
  int specificity = 0;
  std::size_t order = 0;
  CompiledSelector compiled_selector;
};

struct ParsedTheme {
  std::vector<StyleRule> rules;
  std::vector<FontFaceDefinition> font_faces;
  std::vector<Keyframes> keyframes;
  std::vector<Diagnostic> diagnostics;
  std::vector<std::string> source_keys;
  std::vector<std::string> missing_source_keys;
};

struct ThemeSource {
  std::string source;
  std::string content_hash;
};

using ThemeSourceResolver =
    std::function<std::optional<ThemeSource>(std::string_view asset_key)>;

Value jsonValue(const nlohmann::json& value);
std::string jsonNumber(double value);
std::string jsonStyleValue(
    const nlohmann::json& source,
    const nlohmann::json& variables = nlohmann::json::object());

void appendLayoutDeclarations(
    const nlohmann::json& layout,
    Declarations& output,
    const nlohmann::json& variables = nlohmann::json::object());
void appendAppearanceDeclarations(
    const nlohmann::json& appearance,
    Declarations& output,
    const nlohmann::json& variables,
    const nlohmann::json* motions = nullptr);

std::string declarationsInline(const Declarations& declarations);
std::optional<std::string> bindingExpression(const nlohmann::json& value);
std::string normalizedNodeType(std::string type);

ParsedTheme parseThemeSource(std::string_view source,
                             std::string_view asset_key,
                             std::size_t& next_order);
ParsedTheme parseThemeGraph(std::string_view root_asset_key,
                            const ThemeSourceResolver& resolver,
                            std::size_t& next_order);

}  // namespace karma::ui::native
