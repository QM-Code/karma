#include "content/assets/ui_json_validation.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <unordered_set>

namespace karma::assets::detail {
namespace {

using Json = nlohmann::json;

std::string pointerToken(std::string_view token) {
  std::string result;
  for (const char ch : token) {
    if (ch == '~') result += "~0";
    else if (ch == '/') result += "~1";
    else result.push_back(ch);
  }
  return result;
}

std::string childPointer(std::string_view parent, std::string_view child) {
  return std::string(parent) + "/" + pointerToken(child);
}

std::string indexPointer(std::string_view parent, std::size_t index) {
  return std::string(parent) + "/" + std::to_string(index);
}

class Validator {
 public:
  explicit Validator(const JsonProfileDocument& profile) : profile_(profile) {}

  std::vector<UiJsonValidationIssue> document() {
    const Json& root = profile_.value;
    if (!requireObject(root, "", "UI document root")) return std::move(issues_);
    unknownFields(root, "", {"$schema", "format", "version", "canvas", "themes",
                              "model", "root"});
    envelope(root, "karma.ui.document", "KUI2");
    if (const auto found = root.find("canvas"); found != root.end()) {
      canvas(*found, "/canvas");
    }
    if (const auto found = root.find("themes"); found != root.end()) {
      referenceArray(*found, "/themes", "document themes");
    }
    if (const auto found = root.find("model"); found != root.end() &&
        !found->is_object()) {
      typeIssue("document model must be an object", "/model");
    }
    const auto body = root.find("root");
    if (body == root.end()) {
      issue("KUI2_REQUIRED", "UI document requires field 'root'", "");
    } else {
      node(*body, "/root");
    }
    return std::move(issues_);
  }

  std::vector<UiJsonValidationIssue> theme() {
    const Json& root = profile_.value;
    if (!requireObject(root, "", "UI theme root")) return std::move(issues_);
    unknownFields(root, "", {"$schema", "format", "version", "imports",
                              "variables", "fonts", "motions", "defaults",
                              "styles"});
    envelope(root, "karma.ui.theme", "KSTYLE2");
    if (const auto found = root.find("imports"); found != root.end()) {
      referenceArray(*found, "/imports", "theme imports");
    }
    if (const auto found = root.find("variables"); found != root.end() &&
        !found->is_object()) {
      typeIssue("theme variables must be an object", "/variables");
    } else if (found != root.end()) {
      for (auto variable = found->begin(); variable != found->end(); ++variable) {
        styleValue(variable.value(),
                   childPointer("/variables", variable.key()),
                   "theme variable");
      }
    }
    if (const auto found = root.find("fonts"); found != root.end()) {
      fonts(*found, "/fonts");
    }
    if (const auto found = root.find("motions"); found != root.end()) {
      motions(*found, "/motions");
    }
    if (const auto found = root.find("defaults"); found != root.end()) {
      styleMap(*found, "/defaults", false);
    }
    if (const auto found = root.find("styles"); found != root.end()) {
      styleMap(*found, "/styles", true);
    }
    return std::move(issues_);
  }

 private:
  void issue(std::string code,
             std::string message,
             std::string pointer,
             bool key_location = false) {
    UiJsonValidationIssue result{.code = std::move(code),
                                 .message = std::move(message),
                                 .json_pointer = pointer};
    if (const JsonValueSource* source = profile_.sourceFor(pointer)) {
      const JsonSourceLocation location =
          key_location && source->key.has_value() ? source->key->begin
                                                  : source->value.begin;
      result.line = location.line;
      result.column = location.column;
    }
    issues_.push_back(std::move(result));
  }

  void typeIssue(std::string message, std::string_view pointer) {
    issue("UI_JSON_TYPE", std::move(message), std::string(pointer));
  }

  bool requireObject(const Json& value,
                     std::string_view pointer,
                     std::string_view description) {
    if (value.is_object()) return true;
    typeIssue(std::string(description) + " must be an object", pointer);
    return false;
  }

  void unknownFields(const Json& object,
                     std::string_view pointer,
                     std::initializer_list<std::string_view> allowed) {
    if (!object.is_object()) return;
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
      if (std::find(allowed.begin(), allowed.end(), iterator.key()) !=
          allowed.end()) {
        continue;
      }
      const std::string child = childPointer(pointer, iterator.key());
      issue("UI_JSON_UNKNOWN_FIELD",
            "unknown UI JSON field '" + iterator.key() + "'",
            child,
            true);
    }
  }

  void envelope(const Json& root,
                std::string_view expected_format,
                std::string_view code_prefix) {
    if (const auto schema = root.find("$schema");
        schema != root.end() && !schema->is_string()) {
      typeIssue("UI JSON $schema must be a string", "/$schema");
    }
    const auto format = root.find("format");
    if (format == root.end()) {
      issue(std::string(code_prefix) + "_FORMAT",
            "missing UI JSON format", "");
    } else if (!format->is_string() ||
               format->get_ref<const std::string&>() != expected_format) {
      issue(std::string(code_prefix) + "_FORMAT",
            "UI JSON format must be '" + std::string(expected_format) + "'",
            "/format");
    }
    const auto version = root.find("version");
    if (version == root.end()) {
      issue(std::string(code_prefix) + "_VERSION",
            "missing UI JSON version", "");
    } else if ((!version->is_number_integer() &&
                !version->is_number_unsigned()) || *version != 2) {
      issue(std::string(code_prefix) + "_VERSION",
            "UI JSON version must be integer 2", "/version");
    }
  }

  void reference(const Json& value,
                 std::string_view pointer,
                 std::string_view description) {
    if (!requireObject(value, pointer, description)) return;
    unknownFields(value, pointer, {"asset", "file", "kind"});
    const bool asset = value.contains("asset");
    const bool file = value.contains("file");
    if (asset == file) {
      issue("UI_JSON_REFERENCE",
            std::string(description) +
                " must contain exactly one of 'asset' or 'file'",
            std::string(pointer));
    }
    if (asset && !value["asset"].is_string()) {
      typeIssue(std::string(description) + " asset must be a string",
                childPointer(pointer, "asset"));
    } else if (asset && value["asset"].get_ref<const std::string&>().empty()) {
      issue("UI_JSON_REFERENCE", std::string(description) +
                " asset must not be empty",
            childPointer(pointer, "asset"));
    }
    if (file && !value["file"].is_string()) {
      typeIssue(std::string(description) + " file must be a string",
                childPointer(pointer, "file"));
    } else if (file && value["file"].get_ref<const std::string&>().empty()) {
      issue("UI_JSON_REFERENCE", std::string(description) +
                " file must not be empty",
            childPointer(pointer, "file"));
    }
    if (const auto kind = value.find("kind");
        kind != value.end()) {
      if (!kind->is_string()) {
        typeIssue(std::string(description) + " kind must be a string",
                  childPointer(pointer, "kind"));
      } else {
        static const std::unordered_set<std::string> kinds = {
            "theme", "ui_theme", "texture", "image", "font", "svg"};
        if (!kinds.contains(kind->get<std::string>())) {
          issue("UI_JSON_REFERENCE_KIND",
                "unsupported UI asset reference kind '" +
                    kind->get<std::string>() + "'",
                childPointer(pointer, "kind"));
        }
      }
    }
  }

  void referenceArray(const Json& value,
                      std::string_view pointer,
                      std::string_view description) {
    if (!value.is_array()) {
      typeIssue(std::string(description) + " must be an array", pointer);
      return;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
      reference(value[index], indexPointer(pointer, index), description);
    }
  }

  void canvas(const Json& value, std::string_view pointer) {
    if (!requireObject(value, pointer, "document canvas")) return;
    unknownFields(value, pointer, {"reference_size", "scale_mode", "safe_area"});
    if (const auto size = value.find("reference_size"); size != value.end()) {
      if (!size->is_array() || size->size() != 2u ||
          !(*size)[0].is_number() || !(*size)[1].is_number()) {
        typeIssue("canvas reference_size must be a two-number array",
                  childPointer(pointer, "reference_size"));
      } else {
        for (std::size_t index = 0; index < size->size(); ++index) {
          if ((*size)[index].get<double>() <= 0.0) {
            issue("KUI2_CANVAS_REFERENCE_SIZE",
                  "canvas reference_size dimensions must be greater than zero",
                  indexPointer(childPointer(pointer, "reference_size"), index));
          }
        }
      }
    }
    if (const auto mode = value.find("scale_mode");
        mode != value.end() && !mode->is_string()) {
      typeIssue("canvas scale_mode must be a string",
                childPointer(pointer, "scale_mode"));
    } else if (mode != value.end()) {
      static const std::unordered_set<std::string> scale_modes = {
          "logical", "fit", "fill", "stretch", "pixel-perfect",
          "pixel_perfect"};
      if (!scale_modes.contains(mode->get<std::string>())) {
        issue("KUI2_CANVAS_SCALE_MODE",
              "unsupported canvas scale_mode '" + mode->get<std::string>() +
                  "'",
              childPointer(pointer, "scale_mode"));
      }
    }
    if (const auto mode = value.find("scale_mode");
        mode != value.end() && mode->is_string() &&
        mode->get<std::string>() != "logical" &&
        (mode->get<std::string>() == "fit" ||
         mode->get<std::string>() == "fill" ||
         mode->get<std::string>() == "stretch" ||
         mode->get<std::string>() == "pixel-perfect" ||
         mode->get<std::string>() == "pixel_perfect") &&
        !value.contains("reference_size")) {
      issue("KUI2_CANVAS_REFERENCE_SIZE",
            "canvas reference_size is required for non-logical scale modes",
            std::string(pointer));
    }
    if (const auto safe = value.find("safe_area"); safe != value.end()) {
      const std::string path = childPointer(pointer, "safe_area");
      if (!safe->is_string() && !safe->is_boolean()) {
        typeIssue("canvas safe_area must be 'platform', 'none', or a boolean",
                  path);
      } else if (safe->is_string() && safe->get<std::string>() != "platform" &&
                 safe->get<std::string>() != "none") {
        issue("KUI2_CANVAS_SAFE_AREA",
              "canvas safe_area must be 'platform', 'none', or a boolean",
              path);
      }
    }
  }

  bool binding(const Json& value,
               std::string_view pointer,
               bool localization = false) {
    if (!value.is_object()) return false;
    const bool has_bind = value.contains("bind");
    const bool has_expr = value.contains("expr");
    const bool has_loc = value.contains("loc");
    if (static_cast<int>(has_bind) + static_cast<int>(has_expr) +
            static_cast<int>(has_loc) !=
        1) {
      return false;
    }
    if (has_bind) {
      unknownFields(value, pointer, {"bind", "mode"});
      if (!value["bind"].is_string()) {
        typeIssue("binding path must be a string", childPointer(pointer, "bind"));
      } else if (value["bind"].get_ref<const std::string&>().empty()) {
        issue("UI_JSON_BINDING", "binding path must not be empty",
              childPointer(pointer, "bind"));
      }
      if (const auto mode = value.find("mode");
          mode != value.end() && !mode->is_string()) {
        typeIssue("binding mode must be a string", childPointer(pointer, "mode"));
      } else if (mode != value.end() && mode->get<std::string>() != "one_way" &&
                 mode->get<std::string>() != "two_way") {
        issue("UI_JSON_BINDING_MODE",
              "binding mode must be 'one_way' or 'two_way'",
              childPointer(pointer, "mode"));
      }
    } else if (has_expr) {
      unknownFields(value, pointer, {"expr"});
      if (!value["expr"].is_string()) {
        typeIssue("binding expression must be a string",
                  childPointer(pointer, "expr"));
      } else if (value["expr"].get_ref<const std::string&>().empty()) {
        issue("UI_JSON_BINDING", "binding expression must not be empty",
              childPointer(pointer, "expr"));
      }
    } else {
      unknownFields(value, pointer, {"loc", "args"});
      if (!localization) {
        issue("UI_JSON_BINDING", "localization is not valid in this field",
              std::string(pointer));
      }
      if (!value["loc"].is_string()) {
        typeIssue("localization key must be a string", childPointer(pointer, "loc"));
      } else if (value["loc"].get_ref<const std::string&>().empty()) {
        issue("UI_JSON_BINDING", "localization key must not be empty",
              childPointer(pointer, "loc"));
      }
      if (const auto args = value.find("args"); args != value.end()) {
        const std::string args_path = childPointer(pointer, "args");
        if (!args->is_object()) {
          typeIssue("localization args must be an object", args_path);
        } else {
          for (auto argument = args->begin(); argument != args->end();
               ++argument) {
            const std::string argument_path =
                childPointer(args_path, argument.key());
            if (!binding(argument.value(), argument_path)) {
              typeIssue("localization args must be bindings or expressions",
                        argument_path);
            }
          }
        }
      }
    }
    return true;
  }

  void styleValue(const Json& value,
                  std::string_view pointer,
                  std::string_view description) {
    if (value.is_string() || value.is_number() || value.is_boolean()) return;
    if (value.is_array()) {
      for (std::size_t index = 0; index < value.size(); ++index) {
        styleValue(value[index], indexPointer(pointer, index), description);
      }
      return;
    }
    if (value.is_object()) {
      if (value.contains("asset") || value.contains("file")) {
        reference(value, pointer, description);
        return;
      }
      if (value.contains("var")) {
        unknownFields(value, pointer, {"var"});
        if (!value["var"].is_string()) {
          typeIssue(std::string(description) +
                        " variable name must be a string",
                    childPointer(pointer, "var"));
        } else if (value["var"].get_ref<const std::string&>().empty()) {
          issue("UI_JSON_STYLE_VALUE",
                std::string(description) + " variable name must not be empty",
                childPointer(pointer, "var"));
        }
        return;
      }
    }
    typeIssue(std::string(description) +
                  " must be a string, number, boolean, array, asset reference, "
                  "or variable reference",
              pointer);
  }

  void point(const Json& value,
             std::string_view pointer,
             std::string_view description) {
    const bool valid = value.is_array() && value.size() == 2u &&
        value[0].is_number() && value[1].is_number();
    if (!valid) {
      typeIssue(std::string(description) + " must be a two-number array",
                pointer);
    }
  }

  void semantics(const Json& value, std::string_view pointer) {
    if (!requireObject(value, pointer, "node semantics")) return;
    unknownFields(value, pointer,
                  {"label", "description", "role", "tab_index"});
    for (const std::string_view name : {"label", "description", "role"}) {
      if (const auto found = value.find(std::string(name));
          found != value.end() && !found->is_string()) {
        typeIssue("semantic " + std::string(name) + " must be a string",
                  childPointer(pointer, name));
      }
    }
    if (const auto role = value.find("role");
        role != value.end() && role->is_string()) {
      static const std::unordered_set<std::string> roles = {
          "document", "group",      "text",      "image",
          "button",   "toggle",     "slider",    "select",
          "option",   "progress",   "scroll",    "window",
          "tab-list", "tab",        "disclosure", "tree",
          "tree-item", "separator", "menu",      "menu-item",
          "tooltip"};
      if (!roles.contains(role->get<std::string>())) {
        issue("KUI2_SEMANTIC_ROLE",
              "unsupported semantic role '" + role->get<std::string>() + "'",
              childPointer(pointer, "role"));
      }
    }
    if (const auto tab = value.find("tab_index"); tab != value.end() &&
        !tab->is_number_integer()) {
      typeIssue("semantic tab_index must be an integer",
                childPointer(pointer, "tab_index"));
    }
  }

  void props(const Json& value, std::string_view pointer) {
    if (!requireObject(value, pointer, "node props")) return;
    unknownFields(value, pointer,
                  {"state", "title", "text", "src", "value", "min", "max",
                   "step", "disabled", "checked", "expanded", "selected",
                   "open", "collapsed", "orientation", "resizable", "closable",
                   "collapsible", "scrollbar_placement", "scrollbar_visibility",
                   "scroll_x", "scroll_y", "pointer_events", "sampling",
                   "object_fit", "object_position", "items", "item", "key",
                   "template", "overscan", "item_extent", "position", "size",
                   "z", "anchor", "placement", "delay_ms"});
    if (const auto source = value.find("src"); source != value.end()) {
      reference(*source, childPointer(pointer, "src"), "node source");
    }
    if (const auto text = value.find("text"); text != value.end() &&
        !text->is_string() && !binding(*text, childPointer(pointer, "text"), true)) {
      typeIssue("node text must be a string, binding, expression, or localization",
                childPointer(pointer, "text"));
    }
    if (const auto state = value.find("state"); state != value.end() &&
        !binding(*state, childPointer(pointer, "state"))) {
      typeIssue("window state must be a binding or expression",
                childPointer(pointer, "state"));
    }
    if (const auto title = value.find("title");
        title != value.end() && !title->is_string() &&
        !binding(*title, childPointer(pointer, "title"))) {
      typeIssue("node title must be a string, binding, or expression",
                childPointer(pointer, "title"));
    }
    for (const std::string_view name : {"min", "max", "step"}) {
      if (const auto found = value.find(std::string(name));
          found != value.end() && !found->is_number()) {
        typeIssue("node prop " + std::string(name) + " must be a number",
                  childPointer(pointer, name));
      }
    }
    for (const std::string_view name : {"disabled", "checked", "expanded",
                                        "selected", "open", "collapsed"}) {
      const auto found = value.find(std::string(name));
      if (found == value.end() || found->is_boolean()) continue;
      if (!binding(*found, childPointer(pointer, name))) {
        typeIssue("node prop " + std::string(name) +
                      " must be a boolean, binding, or expression",
                  childPointer(pointer, name));
      }
    }
    for (const std::string_view name : {"resizable", "closable",
                                        "collapsible"}) {
      if (const auto found = value.find(std::string(name));
          found != value.end() && !found->is_boolean()) {
        typeIssue("node prop " + std::string(name) + " must be a boolean",
                  childPointer(pointer, name));
      }
    }
    for (const std::string_view name : {"items", "key"}) {
      if (const auto found = value.find(std::string(name));
          found != value.end() &&
          !binding(*found, childPointer(pointer, name))) {
        typeIssue("node prop " + std::string(name) +
                      " must be a binding or expression",
                  childPointer(pointer, name));
      }
    }
    for (const std::string_view name : {"item", "anchor"}) {
      if (const auto found = value.find(std::string(name)); found != value.end()) {
        const std::string path = childPointer(pointer, name);
        if (!found->is_string()) {
          typeIssue("node prop " + std::string(name) + " must be a string",
                    path);
        } else if (found->get_ref<const std::string&>().empty()) {
          issue("KUI2_PROP_VALUE",
                "node prop " + std::string(name) + " must not be empty", path);
        }
      }
    }
    if (const auto prototype = value.find("template");
        prototype != value.end()) {
      node(*prototype, childPointer(pointer, "template"));
    }
    const auto enumProp = [&](std::string_view name,
                              const std::unordered_set<std::string>& allowed,
                              std::string_view expected) {
      const auto found = value.find(std::string(name));
      if (found == value.end()) return;
      const std::string path = childPointer(pointer, name);
      if (!found->is_string()) {
        typeIssue("node prop " + std::string(name) + " must be a string", path);
      } else if (!allowed.contains(found->get<std::string>())) {
        issue("KUI2_PROP_ENUM",
              "node prop " + std::string(name) + " must be " +
                  std::string(expected),
              path);
      }
    };
    static const std::unordered_set<std::string> overflow_values = {
        "visible", "hidden", "auto", "scroll"};
    enumProp("scroll_x", overflow_values,
             "'visible', 'hidden', 'auto', or 'scroll'");
    enumProp("scroll_y", overflow_values,
             "'visible', 'hidden', 'auto', or 'scroll'");
    static const std::unordered_set<std::string> orientation_values = {
        "horizontal", "vertical"};
    enumProp("orientation", orientation_values, "'horizontal' or 'vertical'");
    static const std::unordered_set<std::string> scrollbar_placement_values = {
        "gutter", "overlay"};
    enumProp("scrollbar_placement", scrollbar_placement_values,
             "'gutter' or 'overlay'");
    static const std::unordered_set<std::string> scrollbar_visibility_values = {
        "auto", "always", "hidden"};
    enumProp("scrollbar_visibility", scrollbar_visibility_values,
             "'auto', 'always', or 'hidden'");
    static const std::unordered_set<std::string> pointer_values = {
        "auto", "none"};
    enumProp("pointer_events", pointer_values, "'auto' or 'none'");
    static const std::unordered_set<std::string> sampling_values = {
        "nearest", "linear"};
    enumProp("sampling", sampling_values, "'nearest' or 'linear'");
    static const std::unordered_set<std::string> object_fit_values = {
        "fill", "contain", "cover", "none", "scale-down"};
    enumProp("object_fit", object_fit_values,
             "'fill', 'contain', 'cover', 'none', or 'scale-down'");
    static const std::unordered_set<std::string> placement_values = {
        "auto", "top", "bottom"};
    enumProp("placement", placement_values, "'auto', 'top', or 'bottom'");
    if (const auto position = value.find("object_position");
        position != value.end() && !position->is_string()) {
      typeIssue("node prop object_position must be a string",
                childPointer(pointer, "object_position"));
    }
    for (const std::string_view name : {"position", "size"}) {
      if (const auto found = value.find(std::string(name)); found != value.end()) {
        point(*found, childPointer(pointer, name),
              "node prop " + std::string(name));
      }
    }
    if (const auto z = value.find("z"); z != value.end() && !z->is_number()) {
      typeIssue("node prop z must be a number", childPointer(pointer, "z"));
    }
    if (const auto overscan = value.find("overscan"); overscan != value.end()) {
      const std::string path = childPointer(pointer, "overscan");
      if (!overscan->is_number_integer() && !overscan->is_number_unsigned()) {
        typeIssue("node prop overscan must be an integer", path);
      } else if (overscan->get<double>() < 0.0) {
        issue("KUI2_PROP_RANGE",
              "node prop overscan must not be negative", path);
      }
    }
    for (const std::string_view name : {"item_extent", "delay_ms"}) {
      if (const auto found = value.find(std::string(name)); found != value.end()) {
        const std::string path = childPointer(pointer, name);
        if (!found->is_number()) {
          typeIssue("node prop " + std::string(name) + " must be a number",
                    path);
        } else if (found->get<double>() < 0.0) {
          issue("KUI2_PROP_RANGE",
                "node prop " + std::string(name) + " must not be negative",
                path);
        }
      }
    }
  }

  void node(const Json& value, std::string_view pointer) {
    if (!requireObject(value, pointer, "UI node")) return;
    unknownFields(value, pointer,
                  {"type", "id", "styles", "appearance", "layout", "when",
                   "props", "on", "semantics", "children"});
    const auto type = value.find("type");
    if (type == value.end()) {
      issue("KUI2_NODE_TYPE", "UI node requires field 'type'", std::string(pointer));
    } else if (!type->is_string()) {
      typeIssue("UI node type must be a string", childPointer(pointer, "type"));
    } else {
      static const std::unordered_set<std::string> node_types = {
          "body",       "div",        "panel",      "text",
          "img",        "image",      "svg",        "button",
          "toggle",     "slider",     "select",     "option",
          "progress",   "scroll",     "template",   "repeat",
          "window",     "tabs",       "tab",        "disclosure",
          "tree",       "tree-item",  "splitter",   "separator",
          "spacer",     "popup",      "menu",       "menu-item",
          "tooltip",    "list"};
      if (!node_types.contains(type->get<std::string>())) {
        issue("KUI2_UNKNOWN_NODE",
              "unknown native UI node type '" + type->get<std::string>() + "'",
              childPointer(pointer, "type"));
      }
    }
    if (const auto id = value.find("id"); id != value.end() && !id->is_string()) {
      typeIssue("UI node id must be a string", childPointer(pointer, "id"));
    }
    if (const auto styles = value.find("styles"); styles != value.end()) {
      if (!styles->is_array()) {
        typeIssue("node styles must be an array", childPointer(pointer, "styles"));
      } else {
        for (std::size_t index = 0; index < styles->size(); ++index) {
          if (!(*styles)[index].is_string()) {
            typeIssue("every node style name must be a string",
                      indexPointer(childPointer(pointer, "styles"), index));
          }
        }
      }
    }
    if (const auto found = value.find("layout"); found != value.end()) {
      layout(*found, childPointer(pointer, "layout"));
    }
    if (const auto found = value.find("appearance"); found != value.end()) {
      appearance(*found, childPointer(pointer, "appearance"), false);
    }
    if (const auto found = value.find("when"); found != value.end() &&
        !binding(*found, childPointer(pointer, "when"))) {
      typeIssue("node when must be a binding or expression",
                childPointer(pointer, "when"));
    }
    if (const auto found = value.find("props"); found != value.end()) {
      props(*found, childPointer(pointer, "props"));
    }
    if (const auto actions = value.find("on"); actions != value.end()) {
      if (!actions->is_object()) {
        typeIssue("node on must be an object", childPointer(pointer, "on"));
      } else {
        static const std::unordered_set<std::string> action_events = {
            "click", "change", "cancel", "toggle", "close", "select"};
        for (auto action = actions->begin(); action != actions->end(); ++action) {
          const std::string path =
              childPointer(childPointer(pointer, "on"), action.key());
          if (!action_events.contains(action.key())) {
            issue("KUI2_ACTION_EVENT",
                  "unsupported authored action event '" + action.key() + "'",
                  path, true);
          }
          if (!action.value().is_string()) {
            typeIssue("node action names must be strings",
                      path);
          } else if (action.value().get_ref<const std::string&>().empty()) {
            issue("KUI2_ACTION_NAME",
                  "node action name must not be empty", path);
          }
        }
      }
    }
    if (const auto found = value.find("semantics"); found != value.end()) {
      semantics(*found, childPointer(pointer, "semantics"));
    }
    if (const auto children = value.find("children"); children != value.end()) {
      if (!children->is_array()) {
        typeIssue("node children must be an array",
                  childPointer(pointer, "children"));
      } else {
        for (std::size_t index = 0; index < children->size(); ++index) {
          node((*children)[index],
               indexPointer(childPointer(pointer, "children"), index));
        }
      }
    }
  }

  void layout(const Json& value, std::string_view pointer) {
    if (!requireObject(value, pointer, "layout")) return;
    static const std::unordered_set<std::string> allowed = {
        "mode", "width", "height", "min_width", "min_height", "max_width",
        "max_height", "left", "top", "right", "bottom", "margin", "padding",
        "gap", "row_gap", "column_gap", "grow", "shrink", "basis",
        "align_items", "align_self", "align_content", "justify_content",
        "justify_items", "justify_self", "z", "overflow", "position", "size",
        "anchors", "pivot", "offsets", "columns", "rows", "grid_column", "grid_row",
        "cursor"};
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      if (!allowed.contains(iterator.key())) {
        issue("UI_JSON_UNKNOWN_FIELD",
              "unknown layout field '" + iterator.key() + "'",
              childPointer(pointer, iterator.key()), true);
        continue;
      }
      const std::string path = childPointer(pointer, iterator.key());
      if (iterator.key() == "mode" || iterator.key() == "cursor") {
        if (!iterator.value().is_string()) {
          typeIssue("layout " + iterator.key() + " must be a string", path);
        }
      } else if (iterator.key() == "position" || iterator.key() == "pivot") {
        point(iterator.value(), path, "layout " + iterator.key());
      } else if (iterator.key() == "anchors") {
        if (!requireObject(iterator.value(), path, "layout anchors")) continue;
        unknownFields(iterator.value(), path, {"min", "max"});
        for (const std::string_view name : {"min", "max"}) {
          const auto endpoint = iterator.value().find(std::string(name));
          if (endpoint == iterator.value().end()) {
            issue("KUI2_ANCHORS_REQUIRED",
                  "layout anchors require 'min' and 'max'", path);
          } else {
            point(*endpoint, childPointer(path, name),
                  "layout anchor " + std::string(name));
          }
        }
      } else if (iterator.key() == "offsets") {
        if (iterator.value().is_array()) {
          const bool valid = iterator.value().size() == 4u &&
              std::all_of(iterator.value().begin(), iterator.value().end(),
                          [](const Json& item) { return item.is_number(); });
          if (!valid) {
            typeIssue("layout offsets must be a four-number array or object",
                      path);
          }
        } else if (iterator.value().is_object()) {
          unknownFields(iterator.value(), path,
                        {"left", "top", "right", "bottom"});
          for (auto offset = iterator.value().begin();
               offset != iterator.value().end(); ++offset) {
            if (!offset.value().is_number()) {
              typeIssue("layout offset must be a number",
                        childPointer(path, offset.key()));
            }
          }
        } else {
          typeIssue("layout offsets must be a four-number array or object",
                    path);
        }
      } else {
        styleValue(iterator.value(), path, "layout " + iterator.key());
      }
    }
  }

  void appearance(const Json& value,
                  std::string_view pointer,
                  bool allow_theme_overlays = true) {
    if (!requireObject(value, pointer, "appearance")) return;
    unknownFields(value, pointer,
                  {"box", "text", "parts", "transitions", "states", "motion",
                   "cursor"});
    if (const auto found = value.find("box"); found != value.end()) {
      box(*found, childPointer(pointer, "box"));
    }
    if (const auto found = value.find("text"); found != value.end()) {
      text(*found, childPointer(pointer, "text"));
    }
    if (const auto parts = value.find("parts"); parts != value.end()) {
      if (!parts->is_object()) {
        typeIssue("appearance parts must be an object",
                  childPointer(pointer, "parts"));
      } else {
        static const std::unordered_set<std::string> supported_parts = {
            "track",            "fill",             "thumb",
            "checkmark",        "arrow",            "chevron",
            "grip",             "vertical_track",   "vertical_thumb",
            "horizontal_track", "horizontal_thumb", "corner",
            "titlebar",         "close_button",     "collapse_button",
            "resize_grip",      "popup",            "option"};
        for (auto part = parts->begin(); part != parts->end(); ++part) {
          const std::string path =
              childPointer(childPointer(pointer, "parts"), part.key());
          if (!supported_parts.contains(part.key())) {
            issue("KSTYLE2_WIDGET_PART",
                  "unsupported appearance part '" + part.key() + "'",
                  path, true);
          }
          partDescriptor(part.value(), path, part.key());
        }
      }
    }
    if (const auto states = value.find("states"); states != value.end()) {
      if (!allow_theme_overlays) {
        issue("KUI2_INLINE_APPEARANCE_STATES",
              "inline node appearance may not define states; put state "
              "overrides in a theme default or named style",
              childPointer(pointer, "states"));
      }
      if (!states->is_object()) {
        typeIssue("appearance states must be an object",
                  childPointer(pointer, "states"));
      } else {
        static const std::unordered_set<std::string> state_names = {
            "checked", "selected", "expanded", "hover", "focus", "pressed",
            "disabled"};
        for (auto state = states->begin(); state != states->end(); ++state) {
          if (!state_names.contains(state.key())) {
            issue("UI_JSON_UNKNOWN_FIELD",
                  "unknown appearance state '" + state.key() + "'",
                  childPointer(childPointer(pointer, "states"), state.key()), true);
          }
          appearance(state.value(),
                     childPointer(childPointer(pointer, "states"), state.key()));
        }
      }
    }
    if (const auto transitions = value.find("transitions");
        transitions != value.end()) {
      if (!transitions->is_object()) {
        typeIssue("appearance transitions must be an object",
                  childPointer(pointer, "transitions"));
      } else {
        for (auto transition = transitions->begin();
             transition != transitions->end(); ++transition) {
          const std::string path = childPointer(
              childPointer(pointer, "transitions"), transition.key());
          if (!requireObject(transition.value(), path, "transition")) continue;
          unknownFields(transition.value(), path,
                        {"duration_ms", "easing", "delay_ms"});
          for (const std::string_view name : {"duration_ms", "delay_ms"}) {
            if (const auto member = transition.value().find(std::string(name));
                member != transition.value().end() && !member->is_number()) {
              typeIssue("transition " + std::string(name) +
                            " must be a number",
                        childPointer(path, name));
            }
          }
          if (const auto duration = transition.value().find("duration_ms");
              duration != transition.value().end() && duration->is_number() &&
              duration->get<double>() < 0.0) {
            issue("KSTYLE2_TRANSITION_DURATION",
                  "transition duration_ms must not be negative",
                  childPointer(path, "duration_ms"));
          }
          if (const auto easing = transition.value().find("easing");
              easing != transition.value().end() && !easing->is_string()) {
            typeIssue("transition easing must be a string",
                      childPointer(path, "easing"));
          }
        }
      }
    }
    if (const auto motion = value.find("motion");
        motion != value.end() && !motion->is_string()) {
      typeIssue("appearance motion must be a string",
                childPointer(pointer, "motion"));
    } else if (motion != value.end() && !allow_theme_overlays) {
      issue("KUI2_INLINE_APPEARANCE_MOTION",
            "inline node appearance may not reference motions; apply motion "
            "from a theme default or named style",
            childPointer(pointer, "motion"));
    } else if (motion != value.end() &&
               motion->get_ref<const std::string&>().empty()) {
      issue("KSTYLE2_MOTION_REFERENCE",
            "appearance motion name must not be empty",
            childPointer(pointer, "motion"));
    }
    if (const auto cursor = value.find("cursor");
        cursor != value.end() && !cursor->is_string()) {
      typeIssue("appearance cursor must be a string",
                childPointer(pointer, "cursor"));
    }
  }

  void box(const Json& value, std::string_view pointer) {
    if (!requireObject(value, pointer, "appearance box")) return;
    unknownFields(value, pointer,
                  {"background_color", "background", "background_image",
                   "border_color", "border_width", "border_radius", "opacity",
                   "image_sampling", "object_fit", "object_position",
                   "border_image"});
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      if (iterator.key() == "border_image") continue;
      styleValue(iterator.value(), childPointer(pointer, iterator.key()),
                 "appearance box " + iterator.key());
    }
    if (const auto border_image = value.find("border_image");
        border_image != value.end()) {
      const std::string path = childPointer(pointer, "border_image");
      if (!requireObject(*border_image, path, "border image")) return;
      unknownFields(*border_image, path,
                    {"source", "slice", "width", "repeat"});
      const auto source = border_image->find("source");
      if (source == border_image->end()) {
        issue("KSTYLE2_BORDER_IMAGE_SOURCE",
              "border image requires field 'source'", path);
      } else {
        reference(*source, childPointer(path, "source"), "border image source");
      }
      if (!border_image->contains("slice")) {
        issue("KSTYLE2_BORDER_IMAGE_SLICE",
              "border image requires field 'slice'", path);
      }
      for (const std::string_view name : {"slice", "width"}) {
        const auto values = border_image->find(std::string(name));
        if (values == border_image->end()) continue;
        const bool valid = values->is_array() && values->size() == 4u &&
            std::all_of(values->begin(), values->end(),
                        [](const Json& item) { return item.is_number(); });
        if (!valid) {
          typeIssue("border image " + std::string(name) +
                        " must be a four-number array",
                    childPointer(path, name));
        }
      }
      if (const auto repeat = border_image->find("repeat");
          repeat != border_image->end()) {
        const std::string repeat_path = childPointer(path, "repeat");
        const auto valid_mode = [](const Json& mode) {
          if (!mode.is_string()) return false;
          const std::string& value = mode.get_ref<const std::string&>();
          return value == "stretch" || value == "repeat" || value == "round";
        };
        if (repeat->is_string()) {
          if (!valid_mode(*repeat)) {
            issue("KSTYLE2_BORDER_IMAGE_REPEAT",
                  "border image repeat must be 'stretch', 'repeat', or 'round'",
                  repeat_path);
          }
        } else if (repeat->is_array() && repeat->size() == 2u) {
          for (std::size_t index = 0u; index < repeat->size(); ++index) {
            if (!(*repeat)[index].is_string()) {
              typeIssue("border image repeat entries must be strings",
                        indexPointer(repeat_path, index));
            } else if (!valid_mode((*repeat)[index])) {
              issue("KSTYLE2_BORDER_IMAGE_REPEAT",
                    "border image repeat entries must be 'stretch', 'repeat', "
                    "or 'round'",
                    indexPointer(repeat_path, index));
            }
          }
        } else {
          typeIssue("border image repeat must be a string or a two-string array",
                    repeat_path);
        }
      }
    }
  }

  void text(const Json& value, std::string_view pointer) {
    if (!requireObject(value, pointer, "appearance text")) return;
    unknownFields(value, pointer,
                  {"color", "font_family", "font_size", "font_weight",
                   "font_style", "line_height", "letter_spacing", "align",
                   "direction", "locale", "white_space"});
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      const std::string path = childPointer(pointer, iterator.key());
      if (iterator.key() == "font_family") {
        if (iterator.value().is_string()) {
          if (iterator.value().get_ref<const std::string&>().empty()) {
            issue("KSTYLE2_FONT_FAMILY",
                  "font_family must not be empty", path);
          }
        } else if (iterator.value().is_array()) {
          if (iterator.value().empty()) {
            issue("KSTYLE2_FONT_FAMILY",
                  "font_family fallback list must not be empty", path);
          }
          for (std::size_t index = 0; index < iterator.value().size(); ++index) {
            if (!iterator.value()[index].is_string()) {
              typeIssue("font_family entries must be strings",
                        indexPointer(path, index));
            } else if (iterator.value()[index]
                           .get_ref<const std::string&>().empty()) {
              issue("KSTYLE2_FONT_FAMILY",
                    "font_family entries must not be empty",
                    indexPointer(path, index));
            }
          }
        } else if (iterator.value().is_object() &&
                   iterator.value().contains("var")) {
          styleValue(iterator.value(), path, "appearance text font_family");
        } else {
          typeIssue("font_family must be a string, non-empty string array, or "
                    "variable reference", path);
        }
      } else {
        styleValue(iterator.value(), path, "appearance text " + iterator.key());
      }
    }
  }

  void partStateDescriptor(const Json& value, std::string_view pointer) {
    if (!requireObject(value, pointer, "widget part state")) return;
    unknownFields(value, pointer, {"box"});
    const auto box_value = value.find("box");
    if (box_value == value.end()) {
      issue("KSTYLE2_PART_STATE_VALUE",
            "widget part state requires a box background_color",
            std::string(pointer));
      return;
    }
    const std::string box_path = childPointer(pointer, "box");
    if (!requireObject(*box_value, box_path, "widget part state box")) return;
    unknownFields(*box_value, box_path, {"background_color"});
    if (const auto background = box_value->find("background_color");
        background != box_value->end()) {
      styleValue(*background, childPointer(box_path, "background_color"),
                 "widget part state background color");
    } else {
      issue("KSTYLE2_PART_STATE_VALUE",
            "widget part state box requires background_color", box_path);
    }
  }

  void partDescriptor(const Json& value,
                      std::string_view pointer,
                      std::string_view part_name) {
    if (!requireObject(value, pointer, "widget part")) return;
    unknownFields(value, pointer, {"box", "text", "metrics", "states"});
    if (const auto found = value.find("box"); found != value.end()) {
      box(*found, childPointer(pointer, "box"));
    }
    if (const auto found = value.find("text"); found != value.end()) {
      text(*found, childPointer(pointer, "text"));
    }
    if (const auto metrics = value.find("metrics"); metrics != value.end()) {
      if (!metrics->is_object()) {
        typeIssue("widget part metrics must be an object",
                  childPointer(pointer, "metrics"));
      } else {
        unknownFields(*metrics, childPointer(pointer, "metrics"),
                      {"width", "height", "min_length", "thickness", "size"});
        for (auto metric = metrics->begin(); metric != metrics->end(); ++metric) {
          styleValue(metric.value(),
                     childPointer(childPointer(pointer, "metrics"), metric.key()),
                     "widget part metric");
        }
      }
    }
    if (const auto states = value.find("states"); states != value.end()) {
      const bool supported = part_name == "vertical_thumb" ||
                             part_name == "horizontal_thumb" ||
                             part_name == "option";
      if (!supported) {
        issue("KSTYLE2_PART_STATES",
              "part '" + std::string(part_name) +
                  "' does not support nested states; use appearance.states",
              childPointer(pointer, "states"));
      }
      if (!states->is_object()) {
        typeIssue("widget part states must be an object",
                  childPointer(pointer, "states"));
      } else {
        for (auto state = states->begin(); state != states->end(); ++state) {
          const std::string state_path =
              childPointer(childPointer(pointer, "states"), state.key());
          if (state.key() != "hover" && state.key() != "pressed") {
            issue("KSTYLE2_PART_STATE",
                  "unsupported widget part state '" + state.key() + "'",
                  state_path);
          }
          partStateDescriptor(state.value(), state_path);
        }
      }
    }
  }

  void styleMap(const Json& value,
                std::string_view pointer,
                bool allows_extends) {
    if (!requireObject(value, pointer, "theme style map")) return;
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      styleEntry(iterator.value(), childPointer(pointer, iterator.key()),
                 allows_extends);
    }
  }

  void styleEntry(const Json& value,
                  std::string_view pointer,
                  bool allows_extends) {
    if (!requireObject(value, pointer, "theme style")) return;
    unknownFields(value, pointer,
                  allows_extends
                      ? std::initializer_list<std::string_view>{"extends", "layout",
                                                                "appearance"}
                      : std::initializer_list<std::string_view>{"layout",
                                                                "appearance"});
    if (const auto extends = value.find("extends"); extends != value.end()) {
      if (!allows_extends) {
        issue("UI_JSON_UNKNOWN_FIELD", "defaults may not extend named styles",
              childPointer(pointer, "extends"), true);
      } else if (!extends->is_string() && !extends->is_array()) {
        typeIssue("style extends must be a string or string array",
                  childPointer(pointer, "extends"));
      } else if (extends->is_array()) {
        for (std::size_t index = 0; index < extends->size(); ++index) {
          if (!(*extends)[index].is_string()) {
            typeIssue("every extended style name must be a string",
                      indexPointer(childPointer(pointer, "extends"), index));
          }
        }
      }
    }
    if (const auto found = value.find("layout"); found != value.end()) {
      layout(*found, childPointer(pointer, "layout"));
    }
    if (const auto found = value.find("appearance"); found != value.end()) {
      appearance(*found, childPointer(pointer, "appearance"));
    }
  }

  void fonts(const Json& value, std::string_view pointer) {
    if (!requireObject(value, pointer, "theme fonts")) return;
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      const std::string path = childPointer(pointer, iterator.key());
      if (!requireObject(iterator.value(), path, "font face")) continue;
      unknownFields(iterator.value(), path,
                    {"src", "weight", "style", "face_index"});
      const auto source = iterator.value().find("src");
      if (source == iterator.value().end()) {
        issue("KSTYLE2_FONT_SOURCE", "font face requires field 'src'", path);
      } else {
        reference(*source, childPointer(path, "src"), "font source");
      }
      if (const auto weight = iterator.value().find("weight");
          weight != iterator.value().end() && !weight->is_number_integer()) {
        typeIssue("font weight must be an integer", childPointer(path, "weight"));
      } else if (weight != iterator.value().end() &&
                 (weight->get<double>() < 1.0 ||
                  weight->get<double>() > 1000.0)) {
        issue("KSTYLE2_FONT_WEIGHT",
              "font weight must be between 1 and 1000",
              childPointer(path, "weight"));
      }
      if (const auto style = iterator.value().find("style");
          style != iterator.value().end() && !style->is_string()) {
        typeIssue("font style must be a string", childPointer(path, "style"));
      } else if (style != iterator.value().end() &&
                 style->get<std::string>() != "normal" &&
                 style->get<std::string>() != "italic" &&
                 style->get<std::string>() != "oblique") {
        issue("KSTYLE2_FONT_STYLE",
              "font style must be 'normal', 'italic', or 'oblique'",
              childPointer(path, "style"));
      }
      if (const auto face = iterator.value().find("face_index");
          face != iterator.value().end() && !face->is_number_unsigned() &&
          !face->is_number_integer()) {
        typeIssue("font face_index must be an integer",
                  childPointer(path, "face_index"));
      } else if (face != iterator.value().end() &&
                 face->is_number_integer() && face->get<std::int64_t>() < 0) {
        issue("KSTYLE2_FONT_FACE_INDEX",
              "font face_index must not be negative",
              childPointer(path, "face_index"));
      }
    }
  }

  void motions(const Json& value, std::string_view pointer) {
    if (!requireObject(value, pointer, "theme motions")) return;
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      const std::string path = childPointer(pointer, iterator.key());
      if (!requireObject(iterator.value(), path, "motion")) continue;
      unknownFields(iterator.value(), path,
                    {"duration_ms", "delay_ms", "easing", "iterations",
                     "direction", "keyframes"});
      if (const auto duration = iterator.value().find("duration_ms");
          duration != iterator.value().end() && !duration->is_number()) {
        typeIssue("motion duration_ms must be a number",
                  childPointer(path, "duration_ms"));
      } else if (duration != iterator.value().end() &&
                 duration->get<double>() < 0.0) {
        issue("KSTYLE2_MOTION_DURATION",
              "motion duration_ms must not be negative",
              childPointer(path, "duration_ms"));
      }
      if (const auto delay = iterator.value().find("delay_ms");
          delay != iterator.value().end() && !delay->is_number()) {
        typeIssue("motion delay_ms must be a number",
                  childPointer(path, "delay_ms"));
      }
      if (const auto easing = iterator.value().find("easing");
          easing != iterator.value().end() && !easing->is_string()) {
        typeIssue("motion easing must be a string",
                  childPointer(path, "easing"));
      }
      if (const auto iterations = iterator.value().find("iterations");
          iterations != iterator.value().end() && !iterations->is_number() &&
          !iterations->is_string()) {
        typeIssue("motion iterations must be a number or string",
                  childPointer(path, "iterations"));
      } else if (iterations != iterator.value().end() &&
                 ((iterations->is_number() &&
                   iterations->get<double>() < 0.0) ||
                  (iterations->is_string() &&
                   iterations->get<std::string>() != "infinite"))) {
        issue("KSTYLE2_MOTION_ITERATIONS",
              "motion iterations must be non-negative or 'infinite'",
              childPointer(path, "iterations"));
      }
      if (const auto direction = iterator.value().find("direction");
          direction != iterator.value().end() && !direction->is_string()) {
        typeIssue("motion direction must be a string",
                  childPointer(path, "direction"));
      } else if (direction != iterator.value().end()) {
        static const std::unordered_set<std::string> directions = {
            "normal", "reverse", "alternate", "alternate-reverse"};
        if (!directions.contains(direction->get<std::string>())) {
          issue("KSTYLE2_MOTION_DIRECTION",
                "motion direction must be 'normal', 'reverse', 'alternate', "
                "or 'alternate-reverse'",
                childPointer(path, "direction"));
        }
      }
      if (const auto frames = iterator.value().find("keyframes");
          frames != iterator.value().end()) {
        if (!frames->is_array()) {
          typeIssue("motion keyframes must be an array",
                    childPointer(path, "keyframes"));
        } else {
          for (std::size_t index = 0; index < frames->size(); ++index) {
            const std::string frame_path =
                indexPointer(childPointer(path, "keyframes"), index);
            const Json& frame = (*frames)[index];
            if (!requireObject(frame, frame_path, "motion keyframe")) continue;
            unknownFields(frame, frame_path, {"at", "appearance"});
            if (const auto at = frame.find("at"); at == frame.end()) {
              issue("KSTYLE2_MOTION_KEYFRAME_AT",
                    "motion keyframe requires field 'at'", frame_path);
            } else if (!at->is_number()) {
              typeIssue("motion keyframe at must be a number",
                        childPointer(frame_path, "at"));
            } else if (at->get<double>() < 0.0 || at->get<double>() > 1.0) {
              issue("KSTYLE2_MOTION_KEYFRAME_AT",
                    "motion keyframe at must be between 0 and 1",
                    childPointer(frame_path, "at"));
            }
            if (const auto appearance_value = frame.find("appearance");
                appearance_value != frame.end()) {
              appearance(*appearance_value,
                         childPointer(frame_path, "appearance"));
            } else {
              issue("KSTYLE2_MOTION_KEYFRAME_APPEARANCE",
                    "motion keyframe requires field 'appearance'", frame_path);
            }
          }
        }
      } else {
        issue("KSTYLE2_MOTION_KEYFRAMES",
              "motion requires field 'keyframes'", path);
      }
    }
  }

  const JsonProfileDocument& profile_;
  std::vector<UiJsonValidationIssue> issues_;
};

}  // namespace

std::vector<UiJsonValidationIssue> validateUiJsonProfile(
    const JsonProfileDocument& profile,
    UiJsonKind kind) {
  Validator validator(profile);
  return kind == UiJsonKind::Document ? validator.document()
                                      : validator.theme();
}

}  // namespace karma::assets::detail
