#include "features/ui/native/authoring.h"

#include "content/assets/ui_json_profile.h"
#include "content/assets/ui_json_validation.h"
#include "features/ui/native/diagnostics.h"
#include "features/ui/native/string_utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace karma::ui::native {
namespace {

using Json = nlohmann::json;

using string_utils::lower;

int selectorSpecificity(std::string_view selector) {
  int ids = 0;
  int classes = 0;
  int types = 0;
  bool type_candidate = true;
  for (std::size_t i = 0; i < selector.size(); ++i) {
    const char ch = selector[i];
    if (ch == '#') {
      ++ids;
      type_candidate = false;
    } else if (ch == '.' || ch == ':') {
      ++classes;
      type_candidate = false;
    } else if (ch == '>' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
      type_candidate = true;
    } else if (type_candidate && (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '*')) {
      if (ch != '*') ++types;
      type_candidate = false;
    }
  }
  return ids * 100 + classes * 10 + types;
}

}  // namespace

Value jsonValue(const Json& value) {
  if (value.is_null()) return {};
  if (value.is_boolean()) return Value(value.get<bool>());
  if (value.is_number_integer()) return Value(value.get<std::int64_t>());
  if (value.is_number_unsigned()) return Value(value.get<std::uint64_t>());
  if (value.is_number_float()) return Value(value.get<double>());
  if (value.is_string()) return Value(value.get<std::string>());
  if (value.is_array()) {
    Value::Array result;
    result.reserve(value.size());
    for (const Json& item : value) result.push_back(jsonValue(item));
    return Value(std::move(result));
  }
  Value::Object result;
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    result.emplace(iterator.key(), jsonValue(iterator.value()));
  }
  return Value(std::move(result));
}

std::string jsonNumber(double value) {
  if (!std::isfinite(value)) return "0";
  std::string result = std::to_string(value);
  while (result.size() > 1u && result.back() == '0' &&
         result.find('.') != std::string::npos) {
    result.pop_back();
  }
  if (!result.empty() && result.back() == '.') result.pop_back();
  return result;
}

static const Json* resolveThemeValue(const Json& value,
                                     const Json& variables,
                                     int depth = 0) {
  if (depth > 16 || !value.is_object() || value.size() != 1u ||
      !value.contains("var") || !value["var"].is_string()) {
    return &value;
  }
  const auto found = variables.find(value["var"].get<std::string>());
  return found == variables.end() ? nullptr
                                  : resolveThemeValue(*found, variables, depth + 1);
}

std::string jsonStyleValue(const Json& source, const Json& variables) {
  const Json* resolved = resolveThemeValue(source, variables);
  if (resolved == nullptr) return {};
  const Json& value = *resolved;
  if (value.is_string()) return value.get<std::string>();
  if (value.is_number()) return jsonNumber(value.get<double>());
  if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
  if (value.is_array()) {
    std::string result;
    for (const Json& item : value) {
      if (!result.empty()) result.push_back(' ');
      result += jsonStyleValue(item, variables);
    }
    return result;
  }
  if (value.is_object() && value.contains("asset") && value["asset"].is_string()) {
    return "asset(\"" + value["asset"].get<std::string>() + "\")";
  }
  return {};
}

void appendLayoutDeclarations(const Json& layout,
                              Declarations& output,
                              const Json& variables) {
  if (!layout.is_object()) return;
  auto append = [&](std::string_view source_name, std::string_view target_name) {
    const auto found = layout.find(std::string(source_name));
    if (found == layout.end()) return;
    const std::string value = jsonStyleValue(*found, variables);
    if (!value.empty()) output[std::string(target_name)] = value;
  };
  if (const auto mode = layout.find("mode"); mode != layout.end() && mode->is_string()) {
    const std::string value = lower(mode->get<std::string>());
    if (value == "row" || value == "column") {
      output["display"] = "flex";
      output["flex-direction"] = value;
    } else if (value == "flex") {
      output["display"] = "flex";
    } else if (value == "grid") {
      output["display"] = "grid";
    } else if (value == "overlay" || value == "anchor") {
      output["display"] = "block";
    }
  }
  for (const auto& [source_name, target_name] :
       std::array<std::pair<std::string_view, std::string_view>, 26>{{
           {"width", "width"}, {"height", "height"},
           {"min_width", "min-width"}, {"min_height", "min-height"},
           {"max_width", "max-width"}, {"max_height", "max-height"},
           {"left", "left"}, {"top", "top"}, {"right", "right"},
           {"bottom", "bottom"}, {"margin", "margin"},
           {"padding", "padding"}, {"gap", "gap"},
           {"row_gap", "row-gap"}, {"column_gap", "column-gap"},
           {"grow", "flex-grow"}, {"shrink", "flex-shrink"},
           {"basis", "flex-basis"}, {"align_items", "align-items"},
           {"align_self", "align-self"}, {"align_content", "align-content"},
           {"justify_content", "justify-content"},
           {"justify_items", "justify-items"},
           {"justify_self", "justify-self"}, {"z", "z-index"},
           {"overflow", "overflow"},
       }}) {
    append(source_name, target_name);
  }
  append("cursor", "cursor");
  auto tracks = [&](std::string_view name, std::string_view target) {
    const auto found = layout.find(std::string(name));
    if (found == layout.end()) return;
    output[std::string(target)] = jsonStyleValue(*found, variables);
  };
  tracks("columns", "grid-template-columns");
  tracks("rows", "grid-template-rows");
  append("grid_column", "grid-column");
  append("grid_row", "grid-row");
  if (layout.contains("position")) output["position"] = "absolute";
  if (const auto position = layout.find("position");
      position != layout.end() && position->is_array() && position->size() == 2u) {
    output["left"] = jsonStyleValue((*position)[0], variables);
    output["top"] = jsonStyleValue((*position)[1], variables);
  }
}

static void appendBoxDeclarations(const Json& box,
                                  Declarations& output,
                                  const Json& variables) {
  if (!box.is_object()) return;
  static const std::unordered_map<std::string, std::string> names = {
      {"background_color", "background-color"},
      {"background", "background"},
      {"background_image", "background-image"},
      {"border_color", "border-color"},
      {"border_width", "border-width"},
      {"border_radius", "border-radius"},
      {"opacity", "opacity"},
      {"image_sampling", "image-sampling"},
      {"object_fit", "object-fit"},
      {"object_position", "object-position"},
  };
  for (auto iterator = box.begin(); iterator != box.end(); ++iterator) {
    if (const auto found = names.find(iterator.key()); found != names.end()) {
      const std::string value = jsonStyleValue(iterator.value(), variables);
      if (!value.empty()) output[found->second] = value;
    }
  }
  if (const auto border_image = box.find("border_image");
      border_image != box.end() && border_image->is_object()) {
    if (const auto source = border_image->find("source");
        source != border_image->end()) {
      output["border-image-source"] = jsonStyleValue(*source, variables);
    }
    if (const auto slice = border_image->find("slice");
        slice != border_image->end()) {
      output["border-image-slice"] = jsonStyleValue(*slice, variables);
    }
    if (const auto width = border_image->find("width");
        width != border_image->end()) {
      output["border-image-width"] = jsonStyleValue(*width, variables);
    }
    if (const auto repeat = border_image->find("repeat");
        repeat != border_image->end()) {
      output["border-image-repeat"] = jsonStyleValue(*repeat, variables);
    }
  }
}

static void appendTextDeclarations(const Json& text,
                                   Declarations& output,
                                   const Json& variables) {
  if (!text.is_object()) return;
  static const std::unordered_map<std::string, std::string> names = {
      {"color", "color"}, {"font_family", "font-family"},
      {"font_size", "font-size"}, {"font_weight", "font-weight"},
      {"font_style", "font-style"}, {"line_height", "line-height"},
      {"letter_spacing", "letter-spacing"}, {"align", "text-align"},
      {"direction", "direction"}, {"locale", "locale"},
      {"white_space", "white-space"},
  };
  for (auto iterator = text.begin(); iterator != text.end(); ++iterator) {
    if (const auto found = names.find(iterator.key()); found != names.end()) {
      std::string value = jsonStyleValue(iterator.value(), variables);
      if (iterator.key() == "font_family" && iterator.value().is_array()) {
        value.clear();
        for (const Json& family : iterator.value()) {
          if (!value.empty()) value += ", ";
          value += "\"" + family.get<std::string>() + "\"";
        }
      }
      if (!value.empty()) output[found->second] = std::move(value);
    }
  }
}

static void appendPartDeclarations(const Json& parts,
                                   Declarations& output,
                                   const Json& variables) {
  if (!parts.is_object()) return;
  auto color = [&](std::string_view part, std::string_view property) {
    const auto found = parts.find(std::string(part));
    if (found == parts.end() || !found->is_object()) return;
    const Json* box = found->contains("box") ? &(*found)["box"] : nullptr;
    if (box != nullptr && box->contains("background_color")) {
      output[std::string(property)] =
          jsonStyleValue((*box)["background_color"], variables);
    }
  };
  color("vertical_track", "scrollbar-vertical-track-color");
  color("horizontal_track", "scrollbar-horizontal-track-color");
  color("vertical_thumb", "scrollbar-vertical-thumb-color");
  color("horizontal_thumb", "scrollbar-horizontal-thumb-color");
  color("corner", "scrollbar-corner-color");
  color("titlebar", "window-titlebar-color");
  color("close_button", "window-close-button-color");
  color("collapse_button", "window-collapse-button-color");
  color("resize_grip", "window-resize-grip-color");
  auto part_box = [&](std::string_view part_name,
                      std::string_view prefix) {
    const auto part = parts.find(std::string(part_name));
    if (part == parts.end() || !part->is_object() ||
        !part->contains("box") || !(*part)["box"].is_object()) {
      return;
    }
    const Json& box = (*part)["box"];
    const std::array<std::pair<std::string_view, std::string_view>, 5> names{{
        {"background_color", "color"},
        {"border_color", "border-color"},
        {"border_width", "border-width"},
        {"border_radius", "radius"},
        {"opacity", "opacity"},
    }};
    for (const auto& [source, suffix] : names) {
      if (const auto found = box.find(std::string(source)); found != box.end()) {
        output[std::string(prefix) + "-" + std::string(suffix)] =
            jsonStyleValue(*found, variables);
      }
    }
    if (const auto image = box.find("border_image");
        image != box.end() && image->is_object()) {
      for (const auto& [source, suffix] :
           std::array<std::pair<std::string_view, std::string_view>, 4>{{
               {"source", "border-image-source"},
               {"slice", "border-image-slice"},
               {"width", "border-image-width"},
               {"repeat", "border-image-repeat"},
           }}) {
        if (const auto found = image->find(std::string(source));
            found != image->end()) {
          output[std::string(prefix) + "-" + std::string(suffix)] =
              jsonStyleValue(*found, variables);
        }
      }
    }
  };
  part_box("track", "control-track");
  part_box("fill", "control-fill");
  part_box("thumb", "control-thumb");
  part_box("arrow", "select-arrow");
  part_box("checkmark", "toggle-checkmark");
  part_box("chevron", "disclosure-chevron");
  part_box("grip", "splitter-grip");
  part_box("vertical_track", "scrollbar-vertical-track");
  part_box("horizontal_track", "scrollbar-horizontal-track");
  part_box("vertical_thumb", "scrollbar-vertical-thumb");
  part_box("horizontal_thumb", "scrollbar-horizontal-thumb");
  part_box("corner", "scrollbar-corner");
  part_box("titlebar", "window-titlebar");
  part_box("close_button", "window-close-button");
  part_box("collapse_button", "window-collapse-button");
  part_box("resize_grip", "window-resize-grip");
  part_box("popup", "select-popup");
  part_box("option", "select-option");

  auto text_color = [&](std::string_view part_name,
                        std::string_view property) {
    const auto part = parts.find(std::string(part_name));
    if (part == parts.end() || !part->is_object() ||
        !part->contains("text") || !(*part)["text"].is_object() ||
        !(*part)["text"].contains("color")) {
      return;
    }
    output[std::string(property)] =
        jsonStyleValue((*part)["text"]["color"], variables);
  };
  text_color("checkmark", "toggle-checkmark-color");
  text_color("arrow", "select-arrow-color");
  text_color("chevron", "disclosure-chevron-color");
  text_color("option", "select-option-text-color");

  auto metric = [&](std::string_view part_name,
                    std::string_view metric_name,
                    std::string_view property) {
    const auto part = parts.find(std::string(part_name));
    if (part == parts.end() || !part->is_object() ||
        !part->contains("metrics") || !(*part)["metrics"].is_object() ||
        !(*part)["metrics"].contains(std::string(metric_name))) {
      return;
    }
    output[std::string(property)] = jsonStyleValue(
        (*part)["metrics"][std::string(metric_name)], variables);
  };
  metric("track", "thickness", "control-track-thickness");
  metric("thumb", "width", "control-thumb-width");
  metric("thumb", "height", "control-thumb-height");
  metric("checkmark", "thickness", "toggle-checkmark-thickness");
  metric("checkmark", "size", "toggle-checkmark-size");
  metric("arrow", "width", "select-arrow-size");
  metric("arrow", "size", "select-arrow-size");
  metric("chevron", "size", "disclosure-chevron-size");
  metric("grip", "thickness", "splitter-grip-thickness");
  metric("grip", "size", "splitter-grip-size");
  for (const std::string part_name : {"vertical_track", "horizontal_track"}) {
    const auto part = parts.find(part_name);
    if (part != parts.end() && part->contains("metrics")) {
      const Json& metrics = (*part)["metrics"];
      if (part_name == "vertical_track" && metrics.contains("width")) {
        output["scrollbar-vertical-width"] =
            jsonStyleValue(metrics["width"], variables);
      }
      if (part_name == "horizontal_track" && metrics.contains("height")) {
        output["scrollbar-horizontal-height"] =
            jsonStyleValue(metrics["height"], variables);
      }
    }
  }
  for (const std::string part_name :
       {"vertical_thumb", "horizontal_thumb", "option"}) {
    const auto part = parts.find(part_name);
    if (part == parts.end() || !part->is_object()) continue;
    if (part_name != "option" && part->contains("metrics") &&
        (*part)["metrics"].contains("min_length")) {
      output[part_name == "vertical_thumb"
                 ? "scrollbar-vertical-min-thumb"
                 : "scrollbar-horizontal-min-thumb"] =
          jsonStyleValue((*part)["metrics"]["min_length"], variables);
    }
    if (!part->contains("states")) continue;
    const Json& states = (*part)["states"];
    if (states.contains("hover") && states["hover"].contains("box") &&
        states["hover"]["box"].contains("background_color")) {
      output[part_name == "vertical_thumb"
                 ? "scrollbar-vertical-thumb-hover-color"
                 : part_name == "horizontal_thumb"
                       ? "scrollbar-horizontal-thumb-hover-color"
                       : "select-option-hover-color"] = jsonStyleValue(
          states["hover"]["box"]["background_color"], variables);
    }
    if (states.contains("pressed") && states["pressed"].contains("box") &&
        states["pressed"]["box"].contains("background_color")) {
      output[part_name == "vertical_thumb"
                 ? "scrollbar-vertical-thumb-active-color"
                 : part_name == "horizontal_thumb"
                       ? "scrollbar-horizontal-thumb-active-color"
                       : "select-option-active-color"] = jsonStyleValue(
          states["pressed"]["box"]["background_color"], variables);
    }
  }
  if (const auto titlebar = parts.find("titlebar");
      titlebar != parts.end() && titlebar->contains("metrics") &&
      (*titlebar)["metrics"].contains("height")) {
    output["window-titlebar-height"] =
        jsonStyleValue((*titlebar)["metrics"]["height"], variables);
  }
  if (const auto grip = parts.find("resize_grip");
      grip != parts.end() && grip->contains("metrics") &&
      (*grip)["metrics"].contains("size")) {
    output["window-resize-grip"] =
        jsonStyleValue((*grip)["metrics"]["size"], variables);
  }
  if (const auto fill = parts.find("fill"); fill != parts.end() &&
      fill->contains("box") && (*fill)["box"].contains("background_color")) {
    output["accent-color"] =
        jsonStyleValue((*fill)["box"]["background_color"], variables);
  }
}

void appendAppearanceDeclarations(const Json& appearance,
                                  Declarations& output,
                                  const Json& variables,
                                  const Json* motions) {
  if (!appearance.is_object()) return;
  if (appearance.contains("box")) {
    appendBoxDeclarations(appearance["box"], output, variables);
  }
  if (appearance.contains("text")) {
    appendTextDeclarations(appearance["text"], output, variables);
  }
  if (appearance.contains("parts")) {
    appendPartDeclarations(appearance["parts"], output, variables);
  }
  if (const auto cursor = appearance.find("cursor");
      cursor != appearance.end() && cursor->is_string()) {
    output["cursor"] = cursor->get<std::string>();
  }
  if (appearance.contains("transitions") && appearance["transitions"].is_object()) {
    std::string transition;
    for (auto iterator = appearance["transitions"].begin();
         iterator != appearance["transitions"].end(); ++iterator) {
      if (!iterator.value().is_object()) continue;
      if (!transition.empty()) transition += ", ";
      std::string property = iterator.key();
      std::replace(property.begin(), property.end(), '_', '-');
      transition += property + " " +
                    jsonNumber(iterator.value().value("duration_ms", 0.0)) + "ms " +
                    iterator.value().value("easing", std::string{"ease"});
      if (iterator.value().contains("delay_ms")) {
        transition += " " +
                      jsonNumber(iterator.value().value("delay_ms", 0.0)) + "ms";
      }
    }
    if (!transition.empty()) output["transition"] = std::move(transition);
  }
  if (motions != nullptr && appearance.contains("motion") &&
      appearance["motion"].is_string()) {
    const std::string name = appearance["motion"].get<std::string>();
    const auto motion = motions->find(name);
    if (motion != motions->end() && motion->is_object()) {
      output["animation"] = name + " " +
          jsonNumber(motion->value("duration_ms", 0.0)) + "ms " +
          motion->value("easing", std::string{"ease"}) + " " +
          jsonNumber(motion->value("delay_ms", 0.0)) + "ms " +
          (motion->contains("iterations") && (*motion)["iterations"].is_string()
               ? (*motion)["iterations"].get<std::string>()
               : jsonNumber(motion->value("iterations", 1.0))) + " " +
          motion->value("direction", std::string{"normal"}) + " both";
    }
  }
}

static Declarations styleEntryDeclarations(const Json& entry,
                                           const Json& variables,
                                           const Json* motions = nullptr) {
  Declarations result;
  if (!entry.is_object()) return result;
  if (entry.contains("layout")) {
    appendLayoutDeclarations(entry["layout"], result, variables);
  }
  if (entry.contains("appearance")) {
    appendAppearanceDeclarations(entry["appearance"], result, variables, motions);
  }
  return result;
}

std::string declarationsInline(const Declarations& declarations) {
  std::vector<std::pair<std::string, std::string>> ordered(declarations.begin(),
                                                           declarations.end());
  std::sort(ordered.begin(), ordered.end());
  std::string result;
  for (const auto& [name, value] : ordered) {
    result += name + ":" + value + ";";
  }
  return result;
}

std::optional<std::string> bindingExpression(const Json& value) {
  if (!value.is_object()) return std::nullopt;
  if (value.contains("bind") && value["bind"].is_string()) {
    return value["bind"].get<std::string>();
  }
  if (value.contains("expr") && value["expr"].is_string()) {
    return value["expr"].get<std::string>();
  }
  return std::nullopt;
}

std::string normalizedNodeType(std::string type) {
  std::replace(type.begin(), type.end(), '_', '-');
  if (type == "image") return "img";
  if (type == "repeat") return "template";
  return type;
}


static void mergeThemeJson(Json& destination, const Json& source) {
  if (!destination.is_object() || !source.is_object()) {
    destination = source;
    return;
  }
  for (auto iterator = source.begin(); iterator != source.end(); ++iterator) {
    if (iterator.key() == "extends") continue;
    if (destination.contains(iterator.key()) &&
        destination[iterator.key()].is_object() && iterator.value().is_object()) {
      mergeThemeJson(destination[iterator.key()], iterator.value());
    } else {
      destination[iterator.key()] = iterator.value();
    }
  }
}

static std::string themeStatePseudo(std::string state) {
  if (state == "pressed") return "active";
  return state;
}

static void appendThemeEntryRules(const std::string& selector,
                                  std::string_view style_name,
                                  const Json& entry,
                                  const Json& variables,
                                  const Json& motions,
                                  std::size_t& next_order,
                                  std::vector<StyleRule>& output) {
  Declarations base = styleEntryDeclarations(entry, variables, &motions);
  if (!base.empty()) {
    output.push_back(StyleRule{.selector = selector,
                               .declarations = std::move(base),
                               .style_name = std::string(style_name),
                               .specificity = selectorSpecificity(selector),
                               .order = next_order++,
                               .compiled_selector = compileSelector(selector)});
  }
  if (!entry.is_object() || !entry.contains("appearance") ||
      !entry["appearance"].is_object() ||
      !entry["appearance"].contains("states") ||
      !entry["appearance"]["states"].is_object()) {
    return;
  }
  const Json& states = entry["appearance"]["states"];
  static const std::array<std::string_view, 7> state_order = {
      "checked", "selected", "expanded", "hover", "focus", "pressed",
      "disabled"};
  for (std::string_view state : state_order) {
    const auto found = states.find(std::string(state));
    if (found == states.end() || !found->is_object()) continue;
    Declarations declarations;
    appendAppearanceDeclarations(*found, declarations, variables, &motions);
    if (declarations.empty()) continue;
    const std::string state_selector =
        selector + ":" + themeStatePseudo(std::string(state));
    output.push_back(StyleRule{.selector = state_selector,
                               .declarations = std::move(declarations),
                               .style_name = std::string(style_name),
                               .specificity = selectorSpecificity(state_selector),
                               .order = next_order++,
                               .compiled_selector =
                                   compileSelector(state_selector)});
  }
}

ParsedTheme parseThemeSource(std::string_view source,
                             std::string_view asset_key,
                             std::size_t& next_order) {
  ParsedTheme output;
  const auto parsed = assets::detail::parseJsonProfile(source);
  if (!parsed) {
    addDiagnostic(output.diagnostics, asset_key, "KSTYLE2_JSON_SYNTAX",
                  parsed.error->message, parsed.error->location.line);
    if (!output.diagnostics.empty()) {
      output.diagnostics.back().column = parsed.error->location.column;
    }
    return output;
  }
  const auto validation = assets::detail::validateUiJsonProfile(
      *parsed.document, assets::detail::UiJsonKind::Theme);
  for (const assets::detail::UiJsonValidationIssue& issue : validation) {
    addDiagnostic(output.diagnostics, asset_key, issue.code, issue.message,
                  issue.line);
    if (!output.diagnostics.empty()) {
      output.diagnostics.back().column = issue.column;
    }
  }
  if (!validation.empty()) return output;
  output.source_keys.push_back(std::string(asset_key));
  const Json& root = parsed.document->value;
  const Json variables = root.value("variables", Json::object());
  const Json motions = root.value("motions", Json::object());

  if (const auto fonts = root.find("fonts");
      fonts != root.end() && fonts->is_object()) {
    for (auto iterator = fonts->begin(); iterator != fonts->end(); ++iterator) {
      if (!iterator.value().is_object() || !iterator.value().contains("src")) continue;
      const Json& src = iterator.value()["src"];
      if (!src.is_object() || !src.contains("asset") || !src["asset"].is_string()) {
        continue;
      }
      output.font_faces.push_back({
          .family = lower(iterator.key()),
          .asset_key = src["asset"].get<std::string>(),
          .weight = iterator.value().value("weight", 400),
          .style = parseFontFaceStyle(
              iterator.value().value("style", std::string{"normal"})),
          .face_index = iterator.value().value("face_index", 0u),
          .source_order = output.font_faces.size(),
      });
    }
  }

  if (motions.is_object()) {
    for (auto iterator = motions.begin(); iterator != motions.end(); ++iterator) {
      if (!iterator.value().is_object() ||
          !iterator.value().contains("keyframes") ||
          !iterator.value()["keyframes"].is_array()) {
        continue;
      }
      Keyframes definition;
      definition.name = iterator.key();
      for (const Json& frame_source : iterator.value()["keyframes"]) {
        if (!frame_source.is_object() || !frame_source.contains("appearance")) continue;
        Keyframe frame;
        frame.offset = std::clamp(frame_source.value("at", 0.0), 0.0, 1.0);
        Declarations declarations;
        appendAppearanceDeclarations(frame_source["appearance"], declarations,
                                     variables, nullptr);
        for (auto& [property, value] : declarations) {
          frame.declarations.emplace(
              property,
              KeyframeDeclaration{
                  .source_value = value,
                  .motion_value = parseMotionValue(value),
              });
        }
        definition.frames.push_back(std::move(frame));
      }
      std::stable_sort(definition.frames.begin(), definition.frames.end(),
                       [](const Keyframe& left,
                          const Keyframe& right) {
                         return left.offset < right.offset;
                       });
      if (!definition.frames.empty()) output.keyframes.push_back(std::move(definition));
    }
  }

  if (const auto defaults = root.find("defaults");
      defaults != root.end() && defaults->is_object()) {
    for (auto iterator = defaults->begin(); iterator != defaults->end(); ++iterator) {
      appendThemeEntryRules(normalizedNodeType(iterator.key()), {}, iterator.value(),
                            variables, motions, next_order, output.rules);
    }
  }

  const Json styles = root.value("styles", Json::object());
  std::unordered_map<std::string, Json> resolved_styles;
  std::unordered_set<std::string> resolving;
  std::function<std::optional<Json>(const std::string&)> resolve_style =
      [&](const std::string& name) -> std::optional<Json> {
    if (const auto found = resolved_styles.find(name); found != resolved_styles.end()) {
      return found->second;
    }
    const auto source_style = styles.find(name);
    if (source_style == styles.end() || !source_style->is_object()) {
      addDiagnostic(output.diagnostics, asset_key, "KSTYLE2_MISSING_BASE",
                    "unknown extended style: " + name);
      return std::nullopt;
    }
    if (!resolving.insert(name).second) {
      addDiagnostic(output.diagnostics, asset_key, "KSTYLE2_EXTENDS_CYCLE",
                    "style inheritance cycle at: " + name);
      return std::nullopt;
    }
    Json resolved = Json::object();
    if (source_style->contains("extends")) {
      const Json& bases = (*source_style)["extends"];
      if (bases.is_string()) {
        if (auto base = resolve_style(bases.get<std::string>())) {
          mergeThemeJson(resolved, *base);
        }
      } else if (bases.is_array()) {
        for (const Json& base_name : bases) {
          if (!base_name.is_string()) continue;
          if (auto base = resolve_style(base_name.get<std::string>())) {
            mergeThemeJson(resolved, *base);
          }
        }
      }
    }
    mergeThemeJson(resolved, *source_style);
    resolving.erase(name);
    resolved_styles[name] = resolved;
    return resolved;
  };
  if (styles.is_object()) {
    for (auto iterator = styles.begin(); iterator != styles.end(); ++iterator) {
      if (auto resolved = resolve_style(iterator.key())) {
        appendThemeEntryRules("." + iterator.key(), iterator.key(), *resolved,
                              variables, motions,
                              next_order, output.rules);
      }
    }
  }
  return output;
}

namespace {

void mergeThemeDocuments(Json& destination, const Json& source) {
  if (!destination.is_object() || !source.is_object()) {
    destination = source;
    return;
  }
  for (auto iterator = source.begin(); iterator != source.end(); ++iterator) {
    if (destination.contains(iterator.key()) &&
        destination[iterator.key()].is_object() && iterator.value().is_object()) {
      mergeThemeDocuments(destination[iterator.key()], iterator.value());
    } else {
      destination[iterator.key()] = iterator.value();
    }
  }
}

class ThemeGraphComposer {
 public:
  explicit ThemeGraphComposer(const ThemeSourceResolver& resolver)
      : resolver_(resolver) {}

  std::optional<Json> compose(std::string_view key) {
    return composeImpl(std::string(key), nullptr, {}, {});
  }

  ParsedTheme takeResult() { return std::move(result_); }

 private:
  enum class VisitState { Visiting, Complete };

  void locatedDiagnostic(std::string_view asset_key,
                         std::string code,
                         std::string message,
                         const assets::detail::JsonProfileDocument* profile,
                         std::string_view pointer) {
    std::size_t line = 0u;
    std::size_t column = 0u;
    if (profile != nullptr) {
      if (const assets::detail::JsonValueSource* source =
              profile->sourceFor(pointer)) {
        line = source->value.begin.line;
        column = source->value.begin.column;
      }
    }
    const std::size_t previous_size = result_.diagnostics.size();
    addDiagnostic(result_.diagnostics, asset_key, std::move(code),
                  std::move(message), line);
    if (result_.diagnostics.size() > previous_size) {
      result_.diagnostics.back().column = column;
    }
  }

  std::optional<Json> composeImpl(
      const std::string& key,
      const assets::detail::JsonProfileDocument* referring_profile,
      std::string_view referring_key,
      std::string_view referring_pointer) {
    if (const auto state = states_.find(key); state != states_.end()) {
      if (state->second == VisitState::Visiting) {
        locatedDiagnostic(referring_key.empty() ? key : referring_key,
                          "KSTYLE2_IMPORT_CYCLE",
                          "theme import cycle reaches: " + key,
                          referring_profile, referring_pointer);
        return std::nullopt;
      }
      return composed_.at(key);
    }

    const std::optional<ThemeSource> source = resolver_(key);
    if (!source.has_value()) {
      if (std::find(result_.missing_source_keys.begin(),
                    result_.missing_source_keys.end(), key) ==
          result_.missing_source_keys.end()) {
        result_.missing_source_keys.push_back(key);
      }
      locatedDiagnostic(referring_key.empty() ? key : referring_key,
                        referring_key.empty() ? "UI_THEME_NOT_FOUND"
                                              : "KSTYLE2_IMPORT_MISSING",
                        referring_key.empty()
                            ? "referenced UI theme was not found: " + key
                            : "imported UI theme was not found: " + key,
                        referring_profile, referring_pointer);
      return std::nullopt;
    }

    const assets::detail::JsonProfileParseResult parsed =
        assets::detail::parseJsonProfile(source->source);
    if (!parsed) {
      addDiagnostic(result_.diagnostics, key, "KSTYLE2_JSON_SYNTAX",
                    parsed.error->message, parsed.error->location.line);
      if (!result_.diagnostics.empty()) {
        result_.diagnostics.back().column = parsed.error->location.column;
      }
      return std::nullopt;
    }
    const auto validation = assets::detail::validateUiJsonProfile(
        *parsed.document, assets::detail::UiJsonKind::Theme);
    if (!validation.empty()) {
      for (const assets::detail::UiJsonValidationIssue& issue : validation) {
        const std::size_t previous_size = result_.diagnostics.size();
        addDiagnostic(result_.diagnostics, key, issue.code, issue.message,
                      issue.line);
        if (result_.diagnostics.size() > previous_size) {
          result_.diagnostics.back().column = issue.column;
        }
      }
      return std::nullopt;
    }

    states_[key] = VisitState::Visiting;
    Json merged = Json::object();
    bool valid = true;
    const Json& root = parsed.document->value;
    if (const auto imports = root.find("imports"); imports != root.end()) {
      for (std::size_t index = 0u; index < imports->size(); ++index) {
        const Json& reference = (*imports)[index];
        const std::string pointer = "/imports/" + std::to_string(index);
        if (reference.contains("file")) {
          locatedDiagnostic(
              key, "KSTYLE2_UNRESOLVED_FILE_IMPORT",
              "theme file imports must be rewritten by the development loader",
              &*parsed.document, pointer);
          valid = false;
          continue;
        }
        const std::string dependency = reference.value("asset", std::string{});
        if (dependency.empty()) {
          valid = false;
          continue;
        }
        if (auto imported =
                composeImpl(dependency, &*parsed.document, key, pointer)) {
          mergeThemeDocuments(merged, *imported);
        } else {
          valid = false;
        }
      }
    }

    Json local = root;
    local.erase("imports");
    mergeThemeDocuments(merged, local);
    if (!valid) {
      states_.erase(key);
      return std::nullopt;
    }
    states_[key] = VisitState::Complete;
    composed_[key] = merged;
    result_.source_keys.push_back(key);
    return merged;
  }

  const ThemeSourceResolver& resolver_;
  std::unordered_map<std::string, VisitState> states_;
  std::unordered_map<std::string, Json> composed_;
  ParsedTheme result_;
};

}  // namespace

ParsedTheme parseThemeGraph(std::string_view root_asset_key,
                            const ThemeSourceResolver& resolver,
                            std::size_t& next_order) {
  ThemeGraphComposer composer(resolver);
  const std::optional<Json> composed = composer.compose(root_asset_key);
  ParsedTheme graph = composer.takeResult();
  if (!composed.has_value()) return graph;

  ParsedTheme parsed =
      parseThemeSource(composed->dump(), root_asset_key, next_order);
  parsed.diagnostics.insert(
      parsed.diagnostics.begin(),
      std::make_move_iterator(graph.diagnostics.begin()),
      std::make_move_iterator(graph.diagnostics.end()));
  parsed.source_keys = std::move(graph.source_keys);
  parsed.missing_source_keys = std::move(graph.missing_source_keys);
  return parsed;
}


}  // namespace karma::ui::native
