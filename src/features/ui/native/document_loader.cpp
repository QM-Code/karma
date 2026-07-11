#include "features/ui/native/document_loader.h"

#include "content/assets/ui_json_profile.h"
#include "content/assets/ui_json_validation.h"
#include "features/ui/native/asset_reference.h"
#include "features/ui/native/authoring.h"
#include "features/ui/native/diagnostics.h"
#include "karma/assets.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <unordered_set>
#include <utility>

namespace karma::ui::native {
namespace {

using Json = nlohmann::json;
using runtime_dom::Node;

bool isBuiltInElement(std::string_view name) {
  static const std::unordered_set<std::string_view> elements = {
      "body",       "div",        "panel",      "text",       "img",
      "svg",        "button",     "toggle",     "slider",     "select",
      "option",     "progress",   "scroll",     "template",   "window",
      "tabs",       "tab",        "disclosure", "tree",       "tree-item",
      "splitter",   "separator",  "spacer",     "popup",      "menu",
      "menu-item",  "tooltip",    "list"};
  return elements.contains(name);
}

std::unique_ptr<Node> parseJsonNode(const Json& input,
                                    Node* parent,
                                    std::string_view asset_key,
                                    std::vector<Diagnostic>& diagnostics,
                                    std::string identity) {
  if (!input.is_object() || !input.contains("type") ||
      !input["type"].is_string()) {
    addDiagnostic(diagnostics, asset_key, "KUI2_NODE_TYPE",
                  "every UI node requires a string type");
    return nullptr;
  }
  const std::string tag = normalizedNodeType(input["type"].get<std::string>());
  if (!isBuiltInElement(tag)) {
    addDiagnostic(diagnostics, asset_key, "KUI2_UNKNOWN_NODE",
                  "unknown native UI node type: " + tag);
    return nullptr;
  }
  auto node = std::make_unique<Node>();
  node->tag = tag;
  node->parent = parent;
  node->template_node = tag == "template";
  node->identity = std::move(identity);
  if (const auto id = input.find("id"); id != input.end() && id->is_string()) {
    node->id = id->get<std::string>();
    node->identity = "#" + node->id;
  }
  if (const auto styles = input.find("styles");
      styles != input.end() && styles->is_array()) {
    for (const Json& style : *styles) {
      if (!style.is_string()) continue;
      const std::string name = style.get<std::string>();
      if (node->classes.insert(name).second) node->style_names.push_back(name);
    }
  }
  Declarations inline_style;
  if (input.contains("layout")) {
    appendLayoutDeclarations(input["layout"], inline_style);
    const native::AnchorParseResult anchor =
        native::parseAnchorSpec(input["layout"]);
    if (!anchor) {
      addDiagnostic(diagnostics, asset_key, "KUI2_ANCHOR", anchor.error);
    } else if (anchor.value) {
      node->anchor = *anchor.value;
      inline_style["position"] = "absolute";
      inline_style.erase("left");
      inline_style.erase("top");
      inline_style.erase("right");
      inline_style.erase("bottom");
    }
  }
  if (input.contains("appearance")) {
    appendAppearanceDeclarations(input["appearance"], inline_style,
                                 Json::object());
  }
  if (tag == "window") {
    inline_style["position"] = "absolute";
    if (!inline_style.contains("left")) inline_style["left"] = "0";
    if (!inline_style.contains("top")) inline_style["top"] = "0";
  }
  if (tag == "popup" || tag == "menu" || tag == "tooltip") {
    inline_style["position"] = "absolute";
    inline_style["z-index"] = "100000";
    if (!inline_style.contains("left")) inline_style["left"] = "0";
    if (!inline_style.contains("top")) inline_style["top"] = "0";
  }
  if (input.contains("when")) {
    if (const auto expression = bindingExpression(input["when"])) {
      node->attributes["k-if"] = *expression;
    }
  }
  if (input.contains("semantics") && input["semantics"].is_object()) {
    const Json& semantics = input["semantics"];
    if (semantics.contains("label") && semantics["label"].is_string()) {
      node->attributes["aria-label"] = semantics["label"].get<std::string>();
    }
    if (semantics.contains("description") &&
        semantics["description"].is_string()) {
      node->attributes["aria-description"] =
          semantics["description"].get<std::string>();
    }
    if (semantics.contains("tab_index") && semantics["tab_index"].is_number()) {
      node->attributes["tab-index"] =
          jsonNumber(semantics["tab_index"].get<double>());
    }
    if (semantics.contains("role") && semantics["role"].is_string()) {
      node->attributes["aria-role"] = semantics["role"].get<std::string>();
    }
  }
  if (input.contains("on") && input["on"].is_object()) {
    for (auto iterator = input["on"].begin(); iterator != input["on"].end();
         ++iterator) {
      if (!iterator.value().is_string()) continue;
      std::string event = iterator.key();
      std::replace(event.begin(), event.end(), '_', '-');
      node->attributes["on-" + event] = iterator.value().get<std::string>();
    }
  }
  const Json props = input.value("props", Json::object());
  if (props.is_object()) {
    if (const auto state = props.find("state"); state != props.end()) {
      if (const auto expression = bindingExpression(*state)) {
        node->attributes["bind-window-state"] = *expression;
      }
    }
    if (const auto title = props.find("title"); title != props.end()) {
      if (title->is_string()) {
        node->title = title->get<std::string>();
      } else if (const auto expression = bindingExpression(*title)) {
        node->attributes["bind-title"] = *expression;
      }
    }
    if (const auto text = props.find("text"); text != props.end()) {
      if (text->is_string()) {
        node->source_text = text->get<std::string>();
      } else if (const auto expression = bindingExpression(*text)) {
        node->attributes["bind-text"] = *expression;
      } else if (text->is_object() && text->contains("loc") &&
                 (*text)["loc"].is_string()) {
        node->attributes["loc"] = (*text)["loc"].get<std::string>();
        if (text->contains("args") && (*text)["args"].is_object()) {
          for (auto argument = (*text)["args"].begin();
               argument != (*text)["args"].end(); ++argument) {
            if (const auto expression = bindingExpression(argument.value())) {
              node->attributes["loc-arg-" + argument.key()] = *expression;
            }
          }
        }
      }
    }
    if (const auto source = props.find("src"); source != props.end() &&
        source->is_object() && source->contains("asset") &&
        (*source)["asset"].is_string()) {
      const std::string key = (*source)["asset"].get<std::string>();
      if (isSafeAssetReference(key)) node->image = ImageSource::asset(key);
    }
    if (const auto value = props.find("value"); value != props.end()) {
      if (const auto expression = bindingExpression(*value)) {
        node->attributes["bind-value"] = *expression;
      } else {
        node->control_value = jsonValue(*value);
        if (value->is_string() || value->is_number() || value->is_boolean()) {
          node->attributes["value"] = jsonStyleValue(*value);
        }
      }
    }
    for (const std::string name : {"min", "max", "step", "disabled", "checked",
                                   "expanded", "selected", "open", "collapsed",
                                   "orientation", "resizable", "closable",
                                   "collapsible"}) {
      const auto found = props.find(name);
      if (found == props.end()) continue;
      if (const auto expression = bindingExpression(*found)) {
        node->attributes["bind-" + name] = *expression;
      } else {
        node->attributes[name] = jsonStyleValue(*found);
      }
    }
    for (const std::string name : {"anchor", "placement", "delay_ms",
                                   "item_extent", "overscan"}) {
      const auto found = props.find(name);
      if (found != props.end()) {
        node->attributes[name] = jsonStyleValue(*found);
      }
    }
    if (props.contains("scrollbar_placement")) {
      inline_style["scrollbar-placement"] =
          jsonStyleValue(props["scrollbar_placement"]);
    }
    if (props.contains("scrollbar_visibility")) {
      inline_style["scrollbar-visibility"] =
          jsonStyleValue(props["scrollbar_visibility"]);
    }
    for (const auto& [property, declaration] :
         std::array<std::pair<std::string_view, std::string_view>, 6>{
             {{"scroll_x", "overflow-x"},
              {"scroll_y", "overflow-y"},
              {"pointer_events", "pointer-events"},
              {"sampling", "image-sampling"},
              {"object_fit", "object-fit"},
              {"object_position", "object-position"}}}) {
      if (const auto found = props.find(std::string(property));
          found != props.end()) {
        inline_style[std::string(declaration)] = jsonStyleValue(*found);
      }
    }
    const auto apply_pair = [&](std::string_view name,
                                std::string_view first,
                                std::string_view second) {
      const auto found = props.find(std::string(name));
      if (found == props.end() || !found->is_array() || found->size() != 2u ||
          !(*found)[0].is_number() || !(*found)[1].is_number()) {
        return;
      }
      inline_style[std::string(first)] = jsonStyleValue((*found)[0]);
      inline_style[std::string(second)] = jsonStyleValue((*found)[1]);
    };
    apply_pair("position", "left", "top");
    apply_pair("size", "width", "height");
    if (props.contains("position")) inline_style["position"] = "absolute";
    if (const auto z = props.find("z"); z != props.end() && z->is_number()) {
      inline_style["z-index"] = jsonStyleValue(*z);
    }
    if (tag == "scroll" || tag == "list") inline_style["overflow"] = "auto";
    if ((tag == "select" || tag == "popup" || tag == "menu") &&
        !node->attributes.contains("open") &&
        !node->attributes.contains("bind-open")) {
      node->attributes["open"] = "false";
    }
    if (tag == "template") {
      const auto items = props.find("items");
      const auto item = props.find("item");
      if (items != props.end() && item != props.end() && item->is_string()) {
        if (const auto expression = bindingExpression(*items)) {
          node->attributes["k-for"] =
              item->get<std::string>() + " in " + *expression;
        }
      }
      if (const auto key = props.find("key"); key != props.end()) {
        if (const auto expression = bindingExpression(*key)) {
          node->attributes["k-key"] = *expression;
        }
      }
      if (const auto prototype = props.find("template"); prototype != props.end()) {
        auto child = parseJsonNode(*prototype, node.get(), asset_key, diagnostics,
                                   node->identity + "/template");
        if (child) node->children.push_back(std::move(child));
      }
    }
    if (tag == "list") {
      const auto items = props.find("items");
      const auto item = props.find("item");
      const auto prototype = props.find("template");
      if (items != props.end() && item != props.end() && item->is_string() &&
          prototype != props.end()) {
        if (const auto expression = bindingExpression(*items)) {
          auto repeated = std::make_unique<Node>();
          repeated->tag = "template";
          repeated->parent = node.get();
          repeated->template_node = true;
          repeated->identity = node->identity + "/virtual-template";
          repeated->attributes["k-for"] =
              item->get<std::string>() + " in " + *expression;
          if (const auto key = props.find("key"); key != props.end()) {
            if (const auto key_expression = bindingExpression(*key)) {
              repeated->attributes["k-key"] = *key_expression;
            }
          }
          auto child = parseJsonNode(*prototype, repeated.get(), asset_key,
                                     diagnostics,
                                     repeated->identity + "/template");
          if (child) repeated->children.push_back(std::move(child));
          node->children.push_back(std::move(repeated));
        }
      }
    }
  }
  node->inline_style = std::move(inline_style);
  if (!node->inline_style.empty()) {
    node->attributes["style"] = declarationsInline(node->inline_style);
  }
  node->text = node->source_text;

  if (const auto children = input.find("children");
      children != input.end() && children->is_array()) {
    std::size_t index = 0u;
    for (const Json& child_input : *children) {
      auto child = parseJsonNode(child_input, node.get(), asset_key, diagnostics,
                                 node->identity + "/" + std::to_string(index++));
      if (child) node->children.push_back(std::move(child));
    }
  }
  return node;
}

}  // namespace

ParsedDocument parseDocumentSource(std::string_view source, std::string_view asset_key) {
  ParsedDocument output;
  const assets::detail::JsonProfileParseResult parsed =
      assets::detail::parseJsonProfile(source);
  if (!parsed) {
    const auto& error = *parsed.error;
    addDiagnostic(output.diagnostics, asset_key, "KUI2_JSON_SYNTAX", error.message,
                  error.location.line);
    if (!output.diagnostics.empty()) {
      output.diagnostics.back().column = error.location.column;
    }
    return output;
  }
  const auto validation = assets::detail::validateUiJsonProfile(
      *parsed.document, assets::detail::UiJsonKind::Document);
  for (const assets::detail::UiJsonValidationIssue& issue : validation) {
    const std::size_t previous_size = output.diagnostics.size();
    addDiagnostic(output.diagnostics, asset_key, issue.code, issue.message,
                  issue.line);
    if (output.diagnostics.size() > previous_size) {
      output.diagnostics.back().column = issue.column;
    }
  }
  if (!validation.empty()) return output;
  const Json& root = parsed.document->value;
  const auto canvas = root.find("canvas");
  const native::CanvasParseResult parsed_canvas = native::parseCanvasSpec(
      canvas == root.end() ? nullptr : &*canvas);
  if (!parsed_canvas) {
    addDiagnostic(output.diagnostics, asset_key, "KUI2_CANVAS",
                  parsed_canvas.error);
  } else {
    output.canvas = *parsed_canvas.value;
  }
  if (const auto themes = root.find("themes");
      themes != root.end() && themes->is_array()) {
    for (const Json& theme : *themes) {
      if (theme.is_object() && theme.contains("asset") &&
          theme["asset"].is_string()) {
        const std::string key = theme["asset"].get<std::string>();
        if (isSafeAssetReference(key)) output.stylesheet_keys.push_back(key);
      }
    }
  }
  if (const auto model = root.find("model"); model != root.end()) {
    output.model_defaults = jsonValue(*model);
  }
  const auto body = root.find("root");
  if (body == root.end()) {
    addDiagnostic(output.diagnostics, asset_key, "KUI2_ROOT",
                  "UI document requires a root node");
    return output;
  }
  output.body = parseJsonNode(*body, nullptr, asset_key, output.diagnostics, "root");
  return output;
}

}  // namespace karma::ui::native
